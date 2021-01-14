/*
 * Copyright (c) 2016-2021 Hanspeter Portner (dev@open-music-kontrollers.ch)
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
#include <osc.lv2/util.h>
#include <osc.lv2/writer.h>
#include <osc.lv2/forge.h>

#define BUF_SIZE 2048

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_URID_Unmap *unmap;
	struct {
		LV2_URID midi_MidiEvent;
	} uris;

	uint8_t buf [BUF_SIZE];

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;
	LV2_Atom_Forge forge;
	LV2_OSC_URID osc_urid;
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;
	mlock(handle, sizeof(plughandle_t));

	for(unsigned i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_URID__unmap))
			handle->unmap = features[i]->data;
	}

	if(!handle->map || !handle->unmap)
	{
		fprintf(stderr, "%s: Host does not support urid:(un)map\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	handle->uris.midi_MidiEvent = handle->map->map(handle->map->handle, LV2_MIDI__MidiEvent);
	lv2_atom_forge_init(&handle->forge, handle->map);
	lv2_osc_urid_init(&handle->osc_urid, handle->map);

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	plughandle_t *handle = (plughandle_t *)instance;

	switch(port)
	{
		case 0:
			handle->event_in = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->event_out = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

#define SYSEX_START 0xf0
#define SYSEX_END 0xf7

// 11111111 22222222 33333333 44444444 55555555 66666666 77777777
// 0xf0 07654321 01111111 02222222 03333333 04444444 05555555 06666666 07777777 0xf7

static uint32_t
_7bit_decode(uint8_t *dst, const uint8_t *src, uint32_t size)
{
	volatile uint8_t *to = dst;
	
	if( (size < 2) || (src[0] != SYSEX_START) || (src[size - 1] != SYSEX_END) )
		return 0;

	uint8_t acc = 0x0;
	for(unsigned i = 0; i < size - 2; i++)
	{
		const unsigned shift = i % 8;
		const uint8_t byt = src[i + 1];

		if(shift == 0)
		{
			acc = byt;
		}
		else
		{
			const uint8_t upp = (acc << (8 - shift)) & 0x80;
			*to++ = byt | upp;
		}
	}

	return to - dst;
}

static uint32_t
_7bit_encode(uint8_t *dst, const uint8_t *src, uint32_t size)
{
	const unsigned rem = size % 7;
	uint32_t written = size / 7;
	written *= 8;
	if(rem != 0)
		written += 1 + rem;
	written += 2; // SYSEX_START + SYSEX_END

	volatile uint8_t *to = dst + written;
	to--; // got to last valid position
	*to-- = SYSEX_END;

	uint8_t acc = 0x0;
	for(int i = size - 1; i >= 0; i--)
	{
		const unsigned shift = i % 7;
		const uint8_t byt = src[i];
		const uint8_t upp = (byt & 0x80) >> (7 - shift);
		const uint8_t low = byt & 0x7f;

		acc |= upp;
		*to-- = low;

		if(shift == 0)
		{
			*to-- = acc;
			acc = 0x0;
		}
	}

	*to = SYSEX_START;

	return written;
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = (plughandle_t *)instance;

	// prepare osc atom forge
	const uint32_t capacity = handle->event_out->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->event_out, capacity);
	LV2_Atom_Forge_Frame frame;
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_sequence_head(forge, &frame, 0);
	
	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		const int64_t frames = ev->time.frames;

		if(obj->atom.type == handle->uris.midi_MidiEvent)
		{
			size_t size = obj->atom.size;
			const uint8_t *buf = LV2_ATOM_BODY_CONST(&obj->atom);

			memcpy(handle->buf, buf, size);
			size = _7bit_decode(handle->buf, handle->buf, size);
			if(size)
			{
				if(ref)
					ref = lv2_atom_forge_frame_time(forge, frames);
				if(ref)
					ref = lv2_osc_forge_packet(forge, &handle->osc_urid, handle->map, handle->buf, size);
			}
		}
		else
		{
			LV2_OSC_Writer writer;
			lv2_osc_writer_initialize(&writer, handle->buf, BUF_SIZE);
			lv2_osc_writer_packet(&writer, &handle->osc_urid, handle->unmap, obj->atom.size, &obj->body);
			size_t size;
			uint8_t *ptr = lv2_osc_writer_finalize(&writer, &size);

			if(size)
			{
				size = _7bit_encode(handle->buf, handle->buf, size);

				if(size)
				{
					if(ref)
						ref = lv2_atom_forge_frame_time(forge, frames);
					if(ref)
						ref = lv2_atom_forge_atom(forge, size, handle->uris.midi_MidiEvent);
					if(ref)
						ref = lv2_atom_forge_raw(forge, handle->buf, size);
					if(ref)
						lv2_atom_forge_pad(forge, size);
				}
			}
		}
	}

	if(ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->event_out);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = (plughandle_t *)instance;

	munlock(handle, sizeof(plughandle_t));
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
