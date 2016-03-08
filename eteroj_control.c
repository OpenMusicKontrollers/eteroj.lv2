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

#define DEFAULT_CONTROL_PATH "/eteroj/control/1"
#define MAX_NPROPS 8

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	LV2_URID_Map *map;

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *notify;

	PROPS_T(props, MAX_NPROPS);
	LV2_Atom_Forge forge;
	osc_forge_t oforge;

	char control_path [MAX_NPROPS][512];
	float *control [MAX_NPROPS];

	int64_t frames;
};

static const props_def_t control_path_def [MAX_NPROPS] = {
	[0] = {
		.property = ETEROJ_URI"#control_path_1",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = 512 // strlen
	},
	[1] = {
		.property = ETEROJ_URI"#control_path_2",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = 512 // strlen
	},
	[2] = {
		.property = ETEROJ_URI"#control_path_3",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = 512 // strlen
	},
	[3] = {
		.property = ETEROJ_URI"#control_path_4",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = 512 // strlen
	},
	[4] = {
		.property = ETEROJ_URI"#control_path_5",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = 512 // strlen
	},
	[5] = {
		.property = ETEROJ_URI"#control_path_6",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = 512 // strlen
	},
	[6] = {
		.property = ETEROJ_URI"#control_path_7",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = 512 // strlen
	},
	[7] = {
		.property = ETEROJ_URI"#control_path_8",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = 512 // strlen
	},
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate, const char *bundle_path, const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

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

	lv2_atom_forge_init(&handle->forge, handle->map);
	osc_forge_init(&handle->oforge, handle->map);

	if(!props_init(&handle->props, MAX_NPROPS, descriptor->URI, handle->map, handle))
	{
		free(handle);
		return NULL;
	}

	LV2_URID urid = 1;
	for(unsigned i=0; i<MAX_NPROPS; i++)
		urid = urid && props_register(&handle->props, &control_path_def[i], PROP_EVENT_NONE, NULL, &handle->control_path[i]);

	if(urid)
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

	if(port == 0)
		handle->event_in = (const LV2_Atom_Sequence *)data;
	else if(port == 1)
		handle->notify = (LV2_Atom_Sequence *)data;
	else if(port-2 < MAX_NPROPS)
		handle->control[port-2] = (float *)data;
}

static void
_unpack_message(const char *path, const char *fmt,
	const LV2_Atom_Tuple *body, void *data)
{
	plughandle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge;

	for(unsigned i=0; i<MAX_NPROPS; i++)
	{
		if(strcmp(path, handle->control_path[i]))
			continue; // no matching path

		const LV2_Atom *itr = lv2_atom_tuple_begin(body);
		for(const char *type = fmt;
			*type && !lv2_atom_tuple_is_end(LV2_ATOM_BODY(body), body->atom.size, itr);
			type++, itr = lv2_atom_tuple_next(itr))
		{
			if( (*type == OSC_INT32) && (itr->type == forge->Int) )
			{
				*handle->control[i] = ((const LV2_Atom_Int *)itr)->body;
				break;
			}
			else if( (*type == OSC_INT64) && (itr->type == forge->Long) )
			{
				*handle->control[i] = ((const LV2_Atom_Long *)itr)->body;
				break;
			}
			else if( (*type == OSC_FLOAT) && (itr->type == forge->Float) )
			{
				*handle->control[i] = ((const LV2_Atom_Float *)itr)->body;
				break;
			}
			else if( (*type == OSC_DOUBLE) && (itr->type == forge->Double) )
			{
				*handle->control[i] = ((const LV2_Atom_Double *)itr)->body;
				break;
			}
		}
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = (plughandle_t *)instance;

	// prepare osc atom forge
	const uint32_t capacity = handle->notify->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->notify, capacity);
	LV2_Atom_Forge_Frame frame;
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_sequence_head(forge, &frame, 0);
	
	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		handle->frames = ev->time.frames;

		if(!props_advance(&handle->props, &handle->forge, ev->time.frames, obj, &ref))
		{
			osc_atom_event_unroll(&handle->oforge, obj, NULL, NULL, _unpack_message, handle);
		}
	}

	if(ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->notify);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = (plughandle_t *)instance;

	if(handle)
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

const LV2_Descriptor eteroj_control = {
	.URI						= ETEROJ_CONTROL_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
