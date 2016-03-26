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
#include <osc.h>
#include <lv2_osc.h>
#include <props.h>

#define DEFAULT_PACK_PATH "/midi"
#define MAX_NPROPS 1

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID midi_MidiEvent;
	} uris;

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;

	PROPS_T(props, MAX_NPROPS);
	LV2_Atom_Forge forge;
	osc_forge_t oforge;

	char pack_path [512];

	int64_t frames;
	LV2_Atom_Forge_Ref ref;
};

static const props_def_t pack_path_def = {
	.property = ETEROJ_PACK_PATH_URI,
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__String,
	.mode = PROP_MODE_STATIC,
	.maximum.s = 512 // strlen
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
	osc_forge_init(&handle->oforge, handle->map);
	
	strcpy(handle->pack_path, DEFAULT_PACK_PATH);

	if(!props_init(&handle->props, MAX_NPROPS, descriptor->URI, handle->map, handle))
	{
		free(handle);
		return NULL;
	}

	if(props_register(&handle->props, &pack_path_def, PROP_EVENT_NONE, NULL, &handle->pack_path))
	{
		props_sort(&handle->props);
	}
	else
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
_unpack_message(const char *path, const char *fmt,
	const LV2_Atom_Tuple *body, void *data)
{
	plughandle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Ref ref = handle->ref;

	const LV2_Atom *itr = lv2_atom_tuple_begin(body);
	for(const char *type = fmt;
		*type && !lv2_atom_tuple_is_end(LV2_ATOM_BODY(body), body->atom.size, itr);
		type++, itr = lv2_atom_tuple_next(itr))
	{
		if(  (*type == OSC_MIDI)
			&& (itr->type == handle->uris.midi_MidiEvent) )
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
			if(obj->atom.size <= 3) //TODO handle SysEx?
			{
				LV2_Atom_Forge_Frame frames [2];
				if(handle->ref)
					handle->ref = lv2_atom_forge_frame_time(forge, ev->time.frames);
				if(handle->ref)
					handle->ref = osc_forge_message_push(&handle->oforge, forge, frames, handle->pack_path, "m");
				if(handle->ref)
					handle->ref = osc_forge_midi(&handle->oforge, forge, obj->atom.size, LV2_ATOM_BODY_CONST(&obj->atom));
				if(handle->ref)
					osc_forge_message_pop(&handle->oforge, forge, frames);
			}
		}
		else if(!props_advance(&handle->props, &handle->forge, ev->time.frames, obj, &handle->ref))
		{
			// unpack MIDI from OSC
			osc_atom_event_unroll(&handle->oforge, obj, NULL, NULL, _unpack_message, handle);
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
