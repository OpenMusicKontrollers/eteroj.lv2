/*
 * Copyright (c) 2016 Hanspeter Portner (dev@open-music-kontrollers.ch)
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
#include <osc.lv2/forge.h>
#include <props.h>

#define MAX_NPROPS 2
#define MAX_STRLEN 512

typedef enum _pack_format_t pack_format_t;
typedef struct _plugstate_t plugstate_t;
typedef struct _plughandle_t plughandle_t;

enum _pack_format_t {
	PACK_FORMAT_MIDI = 0,
	PACK_FORMAT_BLOB = 1
};

struct _plugstate_t {
	char pack_path [MAX_STRLEN];
	int32_t pack_format;
};

struct _plughandle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID midi_MidiEvent;
	} uris;

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;

	PROPS_T(props, MAX_NPROPS);
	LV2_Atom_Forge forge;
	LV2_OSC_URID osc_urid;

	plugstate_t state;
	plugstate_t stash;

	int64_t frames;
	LV2_Atom_Forge_Ref ref;
};

static const props_def_t pack_path_def = {
	.property = ETEROJ_PACK_PATH_URI,
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__String,
	.mode = PROP_MODE_STATIC,
	.max_size = MAX_STRLEN
};

static const props_def_t pack_format_def = {
	.property = ETEROJ_PACK_FORMAT_URI,
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_STATIC
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate, const char *bundle_path, const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;
	mlock(handle, sizeof(plughandle_t));

	for(unsigned i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = (LV2_URID_Map *)features[i]->data;
	}

	if(!handle->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	handle->uris.midi_MidiEvent = handle->map->map(handle->map->handle, LV2_MIDI__MidiEvent);
	lv2_atom_forge_init(&handle->forge, handle->map);
	lv2_osc_urid_init(&handle->osc_urid, handle->map);
	
	if(!props_init(&handle->props, MAX_NPROPS, descriptor->URI, handle->map, handle))
	{
		free(handle);
		return NULL;
	}

	if(  !props_register(&handle->props, &pack_path_def, handle->state.pack_path, handle->stash.pack_path)
		|| !props_register(&handle->props, &pack_format_def, &handle->state.pack_format, &handle->stash.pack_format) )
	{
		free(handle);
		return NULL;
	}

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

static void
_unroll(const char *path, const LV2_Atom_Tuple *arguments, void *data)
{
	plughandle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Ref ref = handle->ref;

	LV2_ATOM_TUPLE_FOREACH(arguments, itr)	
	{
		bool is_midi = false;

		switch((pack_format_t)handle->state.pack_format)
		{
			case PACK_FORMAT_MIDI:
				if(lv2_osc_argument_type(&handle->osc_urid, itr) == LV2_OSC_MIDI)
					is_midi = true;
				break;
			case PACK_FORMAT_BLOB:
				if(lv2_osc_argument_type(&handle->osc_urid, itr) == LV2_OSC_BLOB)
					is_midi = true;
				break;
		}

		if(is_midi)
		{
			if(ref)
				ref = lv2_atom_forge_frame_time(forge, handle->frames);
			if(ref)
				ref = lv2_atom_forge_atom(forge, itr->size, handle->uris.midi_MidiEvent);
			if(ref)
				ref = lv2_atom_forge_raw(forge, LV2_ATOM_BODY_CONST(itr), itr->size);
			if(ref)
				lv2_atom_forge_pad(forge, itr->size);
		}
	}

	handle->ref = ref;
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
	handle->ref = lv2_atom_forge_sequence_head(forge, &frame, 0);
	
	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		handle->frames = ev->time.frames;

		if(obj->atom.type == handle->uris.midi_MidiEvent)
		{
			// pack MIDI into OSC
			switch((pack_format_t)handle->state.pack_format)
			{
				case PACK_FORMAT_MIDI:
				{
					if(obj->atom.size <= 3) // OSC 'm' type does not support more :(
					{
						LV2_Atom_Forge_Frame frames [2];
						if(handle->ref)
							handle->ref = lv2_atom_forge_frame_time(forge, ev->time.frames);
						if(handle->ref)
							handle->ref = lv2_osc_forge_message_head(forge, &handle->osc_urid, frames, handle->state.pack_path);
						if(handle->ref)
							handle->ref = lv2_osc_forge_midi(forge, &handle->osc_urid, LV2_ATOM_BODY_CONST(&obj->atom), obj->atom.size);
						if(handle->ref)
							lv2_osc_forge_pop(forge, frames);
					}

					break;
				}
				case PACK_FORMAT_BLOB:
				{
					LV2_Atom_Forge_Frame frames [2];
					if(handle->ref)
						handle->ref = lv2_atom_forge_frame_time(forge, ev->time.frames);
					if(handle->ref)
						handle->ref = lv2_osc_forge_message_head(forge, &handle->osc_urid, frames, handle->state.pack_path);
					if(handle->ref)
						handle->ref = lv2_osc_forge_blob(forge, &handle->osc_urid, LV2_ATOM_BODY_CONST(&obj->atom), obj->atom.size);
					if(handle->ref)
						lv2_osc_forge_pop(forge, frames);

					break;
				}
			}
		}
		else if(!props_advance(&handle->props, &handle->forge, ev->time.frames, obj, &handle->ref))
		{
			// unpack MIDI from OSC
			lv2_osc_unroll(&handle->osc_urid, obj, _unroll, handle);
		}
	}

	if(handle->ref)
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

static LV2_State_Status
_state_save(LV2_Handle instance, LV2_State_Store_Function store,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = (plughandle_t *)instance;

	return props_save(&handle->props, &handle->forge, store, state, flags, features);
}

static LV2_State_Status
_state_restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = (plughandle_t *)instance;

	return props_restore(&handle->props, &handle->forge, retrieve, state, flags, features);
}

static const LV2_State_Interface state_iface = {
	.save = _state_save,
	.restore = _state_restore
};

static const void*
extension_data(const char* uri)
{
	if(!strcmp(uri, LV2_STATE__interface))
		return &state_iface;

	return NULL;
}

const LV2_Descriptor eteroj_pack = {
	.URI						= ETEROJ_PACK_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
