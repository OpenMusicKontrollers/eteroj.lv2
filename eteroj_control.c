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
#include <math.h>

#include <eteroj.h>
#include <osc.h>
#include <lv2_osc.h>
#include <props.h>

#define MAX_STRLEN 512
#define MAX_SLOTS 8
#define MAX_NPROPS (5 * MAX_SLOTS)
#define MIN_VAL 0.0
#define MAX_VAL 127.0

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	int64_t frames;
	osc_forge_t oforge;

	PROPS_T(props, MAX_NPROPS);

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;
	float *output [MAX_SLOTS];

	bool learning;

	int32_t learn [MAX_SLOTS];
	char path [MAX_SLOTS][MAX_STRLEN];
	double min [MAX_SLOTS];
	double max [MAX_SLOTS];
	double raw [MAX_SLOTS];

	LV2_URID learn_urid [MAX_SLOTS];
	LV2_URID path_urid[MAX_SLOTS];
	LV2_URID min_urid [MAX_SLOTS];
	LV2_URID max_urid [MAX_SLOTS];

	float value [MAX_SLOTS];
	double divider [MAX_SLOTS];
};

static const props_def_t stat_learn [MAX_SLOTS] = {
	[0] = {
		.property = ETEROJ_CONTROL_URI"_learn_1",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Bool,
		.mode = PROP_MODE_STATIC
	},
	[1] = {
		.property = ETEROJ_CONTROL_URI"_learn_2",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Bool,
		.mode = PROP_MODE_STATIC
	},
	[2] = {
		.property = ETEROJ_CONTROL_URI"_learn_3",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Bool,
		.mode = PROP_MODE_STATIC
	},
	[3] = {
		.property = ETEROJ_CONTROL_URI"_learn_4",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Bool,
		.mode = PROP_MODE_STATIC
	},
	[4] = {
		.property = ETEROJ_CONTROL_URI"_learn_5",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Bool,
		.mode = PROP_MODE_STATIC
	},
	[5] = {
		.property = ETEROJ_CONTROL_URI"_learn_6",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Bool,
		.mode = PROP_MODE_STATIC
	},
	[6] = {
		.property = ETEROJ_CONTROL_URI"_learn_7",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Bool,
		.mode = PROP_MODE_STATIC
	},
	[7] = {
		.property = ETEROJ_CONTROL_URI"_learn_8",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Bool,
		.mode = PROP_MODE_STATIC
	}
};

static const props_def_t stat_path [MAX_SLOTS] = {
	[0] = {
		.property = ETEROJ_CONTROL_URI"_path_1",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = MAX_STRLEN
	},
	[1] = {
		.property = ETEROJ_CONTROL_URI"_path_2",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = MAX_STRLEN
	},
	[2] = {
		.property = ETEROJ_CONTROL_URI"_path_3",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = MAX_STRLEN
	},
	[3] = {
		.property = ETEROJ_CONTROL_URI"_path_4",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = MAX_STRLEN
	},
	[4] = {
		.property = ETEROJ_CONTROL_URI"_path_5",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = MAX_STRLEN
	},
	[5] = {
		.property = ETEROJ_CONTROL_URI"_path_6",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = MAX_STRLEN
	},
	[6] = {
		.property = ETEROJ_CONTROL_URI"_path_7",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = MAX_STRLEN
	},
	[7] = {
		.property = ETEROJ_CONTROL_URI"_path_8",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__String,
		.mode = PROP_MODE_STATIC,
		.maximum.s = MAX_STRLEN
	}
};

static const props_def_t stat_min [MAX_SLOTS] = {
	[0] = {
		.property = ETEROJ_CONTROL_URI"_min_1",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	},
	[1] = {
		.property = ETEROJ_CONTROL_URI"_min_2",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	},
	[2] = {
		.property = ETEROJ_CONTROL_URI"_min_3",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	},
	[3] = {
		.property = ETEROJ_CONTROL_URI"_min_4",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	},
	[4] = {
		.property = ETEROJ_CONTROL_URI"_min_5",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	},
	[5] = {
		.property = ETEROJ_CONTROL_URI"_min_6",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	},
	[6] = {
		.property = ETEROJ_CONTROL_URI"_min_7",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	},
	[7] = {
		.property = ETEROJ_CONTROL_URI"_min_8",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	}
};

static const props_def_t stat_max [MAX_SLOTS] = {
	[0] = {
		.property = ETEROJ_CONTROL_URI"_max_1",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	},
	[1] = {
		.property = ETEROJ_CONTROL_URI"_max_2",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	},
	[2] = {
		.property = ETEROJ_CONTROL_URI"_max_3",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	},
	[3] = {
		.property = ETEROJ_CONTROL_URI"_max_4",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	},
	[4] = {
		.property = ETEROJ_CONTROL_URI"_max_5",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	},
	[5] = {
		.property = ETEROJ_CONTROL_URI"_max_6",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	},
	[6] = {
		.property = ETEROJ_CONTROL_URI"_max_7",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	},
	[7] = {
		.property = ETEROJ_CONTROL_URI"_max_8",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	}
};

static const props_def_t stat_raw [MAX_SLOTS] = {
	[0] = {
		.property = ETEROJ_CONTROL_URI"_raw_1",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	},
	[1] = {
		.property = ETEROJ_CONTROL_URI"_raw_2",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	},
	[2] = {
		.property = ETEROJ_CONTROL_URI"_raw_3",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	},
	[3] = {
		.property = ETEROJ_CONTROL_URI"_raw_4",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	},
	[4] = {
		.property = ETEROJ_CONTROL_URI"_raw_5",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	},
	[5] = {
		.property = ETEROJ_CONTROL_URI"_raw_6",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	},
	[6] = {
		.property = ETEROJ_CONTROL_URI"_raw_7",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	},
	[7] = {
		.property = ETEROJ_CONTROL_URI"_raw_8",
		.access = LV2_PATCH__writable,
		.type = LV2_ATOM__Double,
		.mode = PROP_MODE_STATIC
	}
};

static void
_intercept_learn(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	plughandle_t *handle = data;

	const int i = (int32_t *)impl->value  - handle->learn;

	if(handle->learn[i]) // set to learn
	{
		handle->learning = true;

		handle->min[i] = MAX_VAL;
		if(handle->ref)
			handle->ref = props_set(&handle->props, forge, frames, handle->min_urid[i]);

		handle->max[i] = MIN_VAL;
		if(handle->ref)
			handle->ref = props_set(&handle->props, forge, frames, handle->max_urid[i]);
	}
}

static inline void
_update_divider(plughandle_t *handle, int i)
{
	if(handle->max[i] != handle->min[i])
		handle->divider[i] = 1.0 / (handle->max[i] - handle->min[i]);
	else
		handle->divider[i] = 1.0;
}

static inline void
_update_value(plughandle_t *handle, int i)
{
	handle->value[i] = (handle->raw[i] - handle->min[i]) * handle->divider[i];
}

static void
_intercept_min(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	plughandle_t *handle = data;

	const int i = (double *)impl->value  - handle->min;
	_update_divider(handle, i);
	_update_value(handle, i);
}

static void
_intercept_max(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	plughandle_t *handle = data;

	const int i = (double *)impl->value  - handle->max;
	_update_divider(handle, i);
	_update_value(handle, i);
}

static void
_intercept_raw(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	plughandle_t *handle = data;

	const int i = (double *)impl->value  - handle->raw;
	_update_value(handle, i);
}

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	for(unsigned i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = features[i]->data;
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
		fprintf(stderr, "failed to allocate property structure\n");
		free(handle);
		return NULL;
	}

	bool success = true;
	for(unsigned i=0; i<MAX_SLOTS; i++)
	{
		handle->learn_urid[i] = props_register(&handle->props, &stat_learn[i],
			PROP_EVENT_WRITE, _intercept_learn, &handle->learn[i]);
		handle->path_urid[i] = props_register(&handle->props, &stat_path[i],
			PROP_EVENT_NONE, NULL, handle->path[i]);
		handle->min_urid[i] = props_register(&handle->props, &stat_min[i],
			PROP_EVENT_WRITE, _intercept_min, &handle->min[i]);
		handle->max_urid[i] = props_register(&handle->props, &stat_max[i],
			PROP_EVENT_WRITE, _intercept_max, &handle->max[i]);
		LV2_URID raw_urid = props_register(&handle->props, &stat_raw[i],
			PROP_EVENT_RESTORE, _intercept_raw, &handle->raw[i]);

		if(  !handle->learn_urid[i] || !handle->path_urid[i]
			|| !handle->min_urid[i] || !handle->max_urid[i] || !raw_urid)
			success = false;
	}

	if(success)
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
	plughandle_t *handle = instance;

	if(port == 0)
		handle->event_in = (const LV2_Atom_Sequence *)data;
	else if(port == 1)
		handle->event_out = (LV2_Atom_Sequence *)data;
	else if(port < 2 + MAX_SLOTS)
		handle->output[port - 2] = (float *)data;
}

static void
activate(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	handle->learning = false;
}

static inline double
_value(const char *fmt, const LV2_Atom_Tuple *body, LV2_Atom_Forge *forge)
{
	const LV2_Atom *itr = lv2_atom_tuple_begin(body);
	for(const char *type = fmt;
		*type && !lv2_atom_tuple_is_end(LV2_ATOM_BODY(body), body->atom.size, itr);
		type++, itr = lv2_atom_tuple_next(itr))
	{
		if( (*type == OSC_INT32) && (itr->type == forge->Int) )
			return ((const LV2_Atom_Int *)itr)->body;
		else if( (*type == OSC_INT64) && (itr->type == forge->Long) )
			return ((const LV2_Atom_Long *)itr)->body;
		else if( (*type == OSC_FLOAT) && (itr->type == forge->Float) )
			return ((const LV2_Atom_Float *)itr)->body;
		else if( (*type == OSC_DOUBLE) && (itr->type == forge->Double) )
			return ((const LV2_Atom_Double *)itr)->body;
	}

	return MIN_VAL;
}

static void
_message(const char *path, const char *fmt, const LV2_Atom_Tuple *body, void *data)
{
	plughandle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge;
	const int64_t frames = handle->frames;

	if(handle->learning)
	{
		for(unsigned i=0; i<MAX_SLOTS; i++)
		{
			if(handle->learn[i])
			{
				strncpy(handle->path[i], path, MAX_STRLEN);
				handle->learn[i] = false;

				if(handle->ref)
					handle->ref = props_set(&handle->props, forge, frames, handle->learn_urid[i]);
				if(handle->ref)
					handle->ref = props_set(&handle->props, forge, frames, handle->path_urid[i]);
			}
		}

		handle->learning = false;
	}

	for(unsigned i=0; i<MAX_SLOTS; i++)
	{
		if(!strncmp(path, handle->path[i], MAX_STRLEN))
		{
			const double value = _value(fmt, body, &handle->forge);

			if(value < handle->min[i])
			{
				handle->min[i] = value;
				_update_divider(handle, i);

				if(handle->ref)
					handle->ref = props_set(&handle->props, forge, frames, handle->min_urid[i]);
			}

			if(value > handle->max[i])
			{
				handle->max[i] = value;
				_update_divider(handle, i);

				if(handle->ref)
					handle->ref = props_set(&handle->props, forge, frames, handle->max_urid[i]);
			}

			handle->raw[i] = value;
			_update_value(handle, i);
		}
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	const uint32_t capacity = handle->event_out->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->event_out, capacity);
	LV2_Atom_Forge_Frame frame;
	handle->ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		const LV2_Atom *atom = (const LV2_Atom *)&ev->body;
		handle->frames = ev->time.frames;

		if(!props_advance(&handle->props, forge, handle->frames, obj, &handle->ref))
		{
			osc_atom_event_unroll(&handle->oforge, obj, NULL, NULL, _message, handle);
		}
	}

	for(unsigned i=0; i<MAX_SLOTS; i++)
		*handle->output[i] = handle->value[i];

	if(handle->ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->event_out);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	if(handle)
		free(handle);
}

static LV2_State_Status
_state_save(LV2_Handle instance, LV2_State_Store_Function store,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = instance;

	return props_save(&handle->props, &handle->forge, store, state, flags, features);
}

static LV2_State_Status
_state_restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = instance;

	return props_restore(&handle->props, &handle->forge, retrieve, state, flags, features);
}

static const LV2_State_Interface state_iface = {
	.save = _state_save,
	.restore = _state_restore
};

static const void *
extension_data(const char *uri)
{
	if(!strcmp(uri, LV2_STATE__interface))
		return &state_iface;
	return NULL;
}

const LV2_Descriptor eteroj_control = {
	.URI						= ETEROJ_CONTROL_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
