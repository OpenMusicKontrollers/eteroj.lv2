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

	const LV2_Atom_Sequence *osc_in;
	LV2_Atom_Sequence *midi_out;
	LV2_Atom_Forge forge;
	osc_forge_t oforge;
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
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

	handle->data.end = handle->data.buf + BUF_SIZE;

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	plughandle_t *handle = (plughandle_t *)instance;

	switch(port)
	{
		case 0:
			handle->osc_in = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->midi_out = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

#define SYSEX_START 0xf0
#define SYSEX_END 0xf7

typedef struct _stack_t stack_t;

struct _stack_t {
	uint32_t size;
	uint32_t cloaked_size;
};

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

// inline packing of OSC into MIDI sysex
static uint32_t
_osc_raw2sysex(uint8_t *dst, const uint8_t *src, uint32_t size)
{
	stack_t stack [32]; //TODO check for overflow
	stack_t *ptr = stack;	
	uint32_t count = 0;

	volatile const uint8_t *from = src;
	volatile uint8_t *to = dst;
	
	to += 2; // SYSEX_START + SYSEX_END

	const uint32_t path_len = osc_strlen((const char *)from);
	count += path_len;
	from += path_len;
	to += path_len;

	const uint8_t *fmt = (const uint8_t *)from;
	const uint32_t fmt_len = osc_strlen((const char *)from);
	count += fmt_len;
	from += fmt_len;
	to += fmt_len;
	
	ptr->size = count;
	ptr->cloaked_size = count;
	ptr++;

	count = 0;
	for(const uint8_t *type=fmt+1; *type; type++)
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

					from += count;
					to += cloaked_count;

					ptr->size = count;
					ptr->cloaked_size = cloaked_count;
					ptr++;
				}

				{
					count = osc_strlen((const char *)from);

					from += count;
					to += count;

					ptr->size = count;
					ptr->cloaked_size = count;
					ptr++;

					count = 0;
				}
				break;

			case 'b':
				if(count)
				{
					uint32_t cloaked_count = _cloaked_size(count);

					from += count;
					to += cloaked_count;

					ptr->size = count;
					ptr->cloaked_size = cloaked_count;
					ptr++;
				}

				{
					count = osc_bloblen((const uint8_t *)from);
					uint32_t cloaked_count = _cloaked_size(count);

					from += count;
					to += cloaked_count;

					ptr->size = count;
					ptr->cloaked_size = cloaked_count;
					ptr++;

					count = 0;
				}
				break;
		}
	}

	if(count)
	{
		uint32_t cloaked_count = _cloaked_size(count);

		from += count;
		to += cloaked_count;

		ptr->size = count;
		ptr->cloaked_size = cloaked_count;
		ptr++;
	}

	const uint32_t written = to - dst;
	
	to[-1] = SYSEX_END;
	to--;

	for(ptr--; ptr>=stack; ptr--)
	{
		if(ptr->size != ptr->cloaked_size)
		{
			uint32_t rem = ptr->size;

			while(rem)
			{
				if(rem % 12 == 0)
				{
					for(int i=0; i<2; i++)
					{
						volatile const uint8_t merge = 0x40
							| ( (from[-6] & 0x80) >> 7)
							| ( (from[-5] & 0x80) >> 6)
							| ( (from[-4] & 0x80) >> 5)
							| ( (from[-3] & 0x80) >> 4)
							| ( (from[-2] & 0x80) >> 3)
							| ( (from[-1] & 0x80) >> 2);
						to[-1] = from[-1] & 0x7f;
						to[-2] = from[-2] & 0x7f;
						to[-3] = from[-3] & 0x7f;
						to[-4] = from[-4] & 0x7f;
						to[-5] = from[-5] & 0x7f;
						to[-6] = from[-6] & 0x7f;
						to[-7] = merge;

						from -= 6;
						to -= 7;
						rem -= 6;
					}
				}
				else
				{
					volatile const uint8_t merge = 0x00
						| ( (from[-4] & 0x80) >> 7)
						| ( (from[-3] & 0x80) >> 6)
						| ( (from[-2] & 0x80) >> 5)
						| ( (from[-1] & 0x80) >> 4);
					to[-1] = from[-1] & 0x7f;
					to[-2] = from[-2] & 0x7f;
					to[-3] = from[-3] & 0x7f;
					to[-4] = from[-4] & 0x7f;
					to[-5] = merge;

					from -= 4;
					to -= 5;
					rem -= 4;
				}
			}
		}
		else
		{
			from -= ptr->size;
			to -= ptr->size;
			memmove((uint8_t *)to, (const uint8_t *)from, ptr->size);
		}
	}

	to[-1] = SYSEX_START;

	if(to-1 == dst)
		return written;

	return 0;
}

static void
_cloak_message(const char *path, const char *fmt,
	const LV2_Atom_Tuple *body, void *data)
{
	plughandle_t *handle = data;

	osc_data_t *ptr = handle->data.buf;
	const osc_data_t *end = handle->data.end;

	ptr = osc_set_path(ptr, end, path);
	ptr = osc_set_fmt(ptr, end, fmt);

	const LV2_Atom *itr = lv2_atom_tuple_begin(body);
	for(const char *type = fmt;
		*type && !lv2_atom_tuple_is_end(LV2_ATOM_BODY(body), body->atom.size, itr);
		type++, itr = lv2_atom_tuple_next(itr))
	{
		switch(*type)
		{
			case 'i':
			{
				ptr = osc_set_int32(ptr, end, ((const LV2_Atom_Int *)itr)->body);
				break;
			}
			case 'f':
			{
				ptr = osc_set_float(ptr, end, ((const LV2_Atom_Float *)itr)->body);
				break;
			}
			case 's':
			case 'S':
			{
				ptr = osc_set_string(ptr, end, LV2_ATOM_BODY_CONST(itr));
				break;
			}
			case 'b':
			{
				ptr = osc_set_blob(ptr, end, itr->size, LV2_ATOM_BODY_CONST(itr));
				break;
			}

			case 'h':
			{
				ptr = osc_set_int64(ptr, end, ((const LV2_Atom_Long *)itr)->body);
				break;
			}
			case 'd':
			{
				ptr = osc_set_double(ptr, end, ((const LV2_Atom_Double *)itr)->body);
				break;
			}
			case 't':
			{
				ptr = osc_set_timetag(ptr, end, ((const LV2_Atom_Long *)itr)->body);
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
				ptr = osc_set_char(ptr, end, ((const LV2_Atom_Int *)itr)->body);
				break;
			}
			case 'm':
			{
				const uint8_t *src = LV2_ATOM_BODY_CONST(itr);
				const uint8_t dst [4] = {
					0x00, // port byte
					itr->size >= 1 ? src[0] : 0x00,
					itr->size >= 2 ? src[1] : 0x00,
					itr->size >= 3 ? src[2] : 0x00
				};
				ptr = osc_set_midi(ptr, end, dst);
				break;
			}
		}
	}

	uint32_t size = ptr - handle->data.buf;

	if(size && osc_check_packet(handle->data.buf, size))
	{
		size = _osc_raw2sysex(handle->data.buf, handle->data.buf, size);

		if(size)
		{
			LV2_Atom_Forge *forge = &handle->forge;

			lv2_atom_forge_frame_time(forge, handle->data.frames);
			lv2_atom_forge_atom(forge, size, handle->uris.midi_MidiEvent);
			lv2_atom_forge_raw(forge, handle->data.buf, size);
			lv2_atom_forge_pad(forge, size);
		}
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = (plughandle_t *)instance;

	// prepare osc atom forge
	const uint32_t capacity = handle->midi_out->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->midi_out, capacity);
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_sequence_head(forge, &frame, 0);
	
	LV2_ATOM_SEQUENCE_FOREACH(handle->osc_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		handle->data.frames = ev->time.frames;

		osc_atom_event_unroll(&handle->oforge, obj, NULL, NULL, _cloak_message, handle);
	}

	lv2_atom_forge_pop(forge, &frame);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = (plughandle_t *)instance;

	free(handle);
}

const LV2_Descriptor eteroj_cloak = {
	.URI						= ETEROJ_CLOAK_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= NULL
};
