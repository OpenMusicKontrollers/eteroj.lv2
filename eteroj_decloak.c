/*
 * Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the Artistic License 2.0 as published by
 * The Perl Foundation.
 *
 * This source is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Artistic License 2.0 for more details.
 *
 * You should have received a copy of the Artistic License 2.0
 * along the source as a COPYING file. If not, obtain it from
 * http://www.perlfoundation.org/artistic_license_2_0.
 */

#include <stdio.h>
#include <stdlib.h>

#include <eteroj.h>
#include <osc.h>
#include <lv2_osc.h>

#define BUF_SIZE 2048

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_URID_Unmap *unmap;
	struct {
		LV2_URID midi_MidiEvent;
	} uris;

	struct {
		int64_t frames;
		osc_data_t buf [BUF_SIZE];
		osc_data_t *end;
	} data;

	const LV2_Atom_Sequence *midi_in;
	LV2_Atom_Sequence *osc_out;
	LV2_Atom_Forge forge;
	osc_forge_t oforge;
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate, const char *bundle_path, const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	for(unsigned i=0; features[i]; i++)
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = (LV2_URID_Map *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_URID__unmap))
			handle->unmap = (LV2_URID_Unmap *)features[i]->data;

	if(!handle->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	handle->uris.midi_MidiEvent = handle->map->map(handle->map->handle, LV2_MIDI__MidiEvent);
	lv2_atom_forge_init(&handle->forge, handle->map);
	osc_forge_init(&handle->oforge, handle->map);

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	plughandle_t *handle = (plughandle_t *)instance;

	switch(port)
	{
		case 0:
			handle->midi_in = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->osc_out = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

static inline uint32_t
_cloaked_size(uint32_t size)
{
	uint32_t cloaked_size = 0;

	while(size)
	{
		if(size % 12 == 0)
		{
			cloaked_size += 14;
			size -= 12;
		}
		else
		{
			cloaked_size += 5;
			size -= 4;
		}
	}

	return cloaked_size;
}

static inline void
_decloak_chunk(volatile uint8_t *to, volatile const uint8_t *from,
	const uint8_t *end, uint32_t size)
{
	while(size && (from < end))
	{
		volatile const uint8_t loc = from[0];
		if(loc & 0x40) // decode 6 bytes
		{
			to[0] = from[1] | ( (loc << 7) & 0x80);
			to[1] = from[2] | ( (loc << 6) & 0x80);
			to[2] = from[3] | ( (loc << 5) & 0x80);
			to[3] = from[4] | ( (loc << 4) & 0x80);
			to[4] = from[5] | ( (loc << 3) & 0x80);
			to[5] = from[6] | ( (loc << 2) & 0x80);

			from += 7;
			to += 6;
			size -= 7;
		}
		else // decode 4 bytes
		{
			to[0] = from[1] | ( (loc << 7) & 0x80);
			to[1] = from[2] | ( (loc << 6) & 0x80);
			to[2] = from[3] | ( (loc << 5) & 0x80);
			to[3] = from[4] | ( (loc << 4) & 0x80);

			from += 5;
			to += 4;
			size -= 5;
		}
	}
}

// inline unpacking of OSC from MIDI sysex
static uint32_t
_osc_sysex2raw(uint8_t *dst, const uint8_t *src, uint32_t size)
{
	uint32_t count = 0;

	volatile const uint8_t *from = src;
	const uint8_t *end = src + size;
	volatile uint8_t *to = dst;

	from++; // skip SYSEX_START

	uint32_t path_len = osc_strlen((const char *)from);
	count += path_len;

	if(count > size-6) // SYSEX_START + MIN(fmt_len) + SYSEX_END
		return 0;

	const uint8_t *fmt = (const uint8_t *)(from + path_len);
	uint32_t fmt_len = osc_strlen((const char *)fmt);
	count += fmt_len;

	if(count > size-2) // SYSEX_START + SYSEX_END
		return 0;
	
	memmove((uint8_t *)to, (const uint8_t *)from, count);
	to += count;
	from += count;

	count = 0;
	for(const uint8_t *type=fmt; *type; type++)
	{
		switch(*type)
		{
			case 'i':
			case 'f':
			case 'm':
				count += 4;
				break;

			case 'h':
			case 'd':
			case 't':
				count += 8;
				break;

			case 's':
			case 'S':
				if(count)
				{
					uint32_t cloaked_count = _cloaked_size(count);

					_decloak_chunk(to, from, end, cloaked_count);

					from += cloaked_count;
					to += count;
				}

				{
					count = osc_strlen((const char *)from);

					memmove((uint8_t *)to, (const uint8_t *)from, count);

					from += count;
					to += count;

					count = 0;
				}
				break;

			case 'b':
				if(count)
				{
					uint32_t cloaked_count = _cloaked_size(count);

					_decloak_chunk(to, from, end, cloaked_count);

					from += cloaked_count;
					to += count;
				}

				{
					union {
						int32_t i;
						uint8_t b [6];
					} blobsize;
					_decloak_chunk(blobsize.b, from, end, from[0] & 0x40 ? 7 : 5);
					count = osc_bloblen(blobsize.b);

					uint32_t cloaked_count = _cloaked_size(count);

					_decloak_chunk(to, from, end, cloaked_count);

					from += cloaked_count;
					to += count;

					count = 0;
				}
				break;
		}
	}

	if(count)
	{
		uint32_t cloaked_count = _cloaked_size(count);

		_decloak_chunk(to, from, end, cloaked_count);

		from += cloaked_count;
		to += count;
	}

	from++; // skip SYSEX_END

	if(from == end)
		return to - dst;

	return 0;
}

// rt
static int
_message(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Frame frame [2];

	const osc_data_t *ptr = buf;

	osc_forge_message_push(&handle->oforge, forge, frame, path, fmt);

	for(const char *type = fmt; *type; type++)
		switch(*type)
		{
			case 'i':
			{
				int32_t i;
				ptr = osc_get_int32(ptr, &i);
				osc_forge_int32(&handle->oforge, forge, i);
				break;
			}
			case 'f':
			{
				float f;
				ptr = osc_get_float(ptr, &f);
				osc_forge_float(&handle->oforge, forge, f);
				break;
			}
			case 's':
			case 'S':
			{
				const char *s;
				ptr = osc_get_string(ptr, &s);
				osc_forge_string(&handle->oforge, forge, s);
				break;
			}
			case 'b':
			{
				osc_blob_t b;
				ptr = osc_get_blob(ptr, &b);
				osc_forge_blob(&handle->oforge, forge, b.size, b.payload);
				break;
			}

			case 'h':
			{
				int64_t h;
				ptr = osc_get_int64(ptr, &h);
				osc_forge_int64(&handle->oforge, forge, h);
				break;
			}
			case 'd':
			{
				double d;
				ptr = osc_get_double(ptr, &d);
				osc_forge_double(&handle->oforge, forge, d);
				break;
			}
			case 't':
			{
				uint64_t t;
				ptr = osc_get_timetag(ptr, &t);
				osc_forge_timestamp(&handle->oforge, forge, t);
				break;
			}

			case 'T':
			case 'F':
			case 'N':
			case 'I':
			{
				break;
			}

			case 'c':
			{
				char c;
				ptr = osc_get_char(ptr, &c);
				osc_forge_char(&handle->oforge, forge, c);
				break;
			}
			case 'm':
			{
				const uint8_t *m;
				ptr = osc_get_midi(ptr, &m);
				osc_forge_midi(&handle->oforge, forge, 3, m + 1); // skip port byte
				break;
			}
		}

	osc_forge_message_pop(&handle->oforge, forge, frame);

	return 1;
}

static const osc_method_t methods [] = {
	{NULL, NULL, _message},

	{NULL, NULL, NULL}
};

static void
_decloak_event(plughandle_t *handle, int64_t frames, const uint8_t *arg, uint32_t size)
{
	LV2_Atom_Forge *forge = &handle->forge;

	if(size)
	{
		memcpy(handle->data.buf, arg, size);

		size = _osc_sysex2raw(handle->data.buf, handle->data.buf, size);
		if(size && osc_check_packet(handle->data.buf, size))
		{
			lv2_atom_forge_frame_time(forge, frames);
			osc_dispatch_method(handle->data.buf, size, methods, NULL, NULL, handle);
		}
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = (plughandle_t *)instance;

	// prepare osc atom forge
	const uint32_t capacity = handle->osc_out->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->osc_out, capacity);
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_sequence_head(forge, &frame, 0);
	
	const uint8_t sysex_start = 0xf0;
	const uint8_t sysex_end = 0xf7;
	
	LV2_ATOM_SEQUENCE_FOREACH(handle->midi_in, ev)
	{
		int64_t frames = ev->time.frames;
		size_t size = ev->body.size;
		const uint8_t *buf = LV2_ATOM_CONTENTS_CONST(LV2_Atom_Event, ev);

		if(ev->body.type == handle->uris.midi_MidiEvent)
		{
			if( (buf[0] == sysex_start) && (buf[size-1] == sysex_end) )
			{
				_decloak_event(handle, frames, buf, size);
			}
		}
	}

	lv2_atom_forge_pop(forge, &frame);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = (plughandle_t *)instance;

	free(handle);
}

const LV2_Descriptor eteroj_decloak = {
	.URI						= ETEROJ_DECLOAK_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= NULL
};
