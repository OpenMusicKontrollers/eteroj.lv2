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

#include <props.h>

#include <lv2/lv2plug.in/ns/ext/log/log.h>

#define PROPS_PREFIX		"http://open-music-kontrollers.ch/lv2/props#"
#define PROPS_TEST_URI	PROPS_PREFIX"test"

#define MAX_NPROPS 32

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_Log_Log *log;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	LV2_URID log_trace;

	PROPS_T(props, MAX_NPROPS);

	struct {
		int32_t val1;
		int64_t val2;
		float val3;
		double val4;
		int32_t val5;
		int32_t val6;
		char val7 [256];
	} dyn;

	struct {
		int32_t val1;
		int64_t val2;
		float val3;
		double val4;
		char val5 [256];
		char val6 [256];
	} stat;

	struct {
		LV2_URID stat2;
		LV2_URID stat4;
		LV2_URID dyn2;
		LV2_URID dyn4;
	} urid;

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;
};

static const props_def_t dyn1 = {
	.label = "Int",
	.property = PROPS_PREFIX"Int",
	.access = LV2_PATCH__writable,
	.unit = LV2_UNITS__hz,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_DYNAMIC,
	.minimum.i = 0,
	.maximum.i = 10
};

static const props_def_t dyn2 = {
	.label = "Long",
	.property = PROPS_PREFIX"Long",
	.access = LV2_PATCH__readable,
	.unit = LV2_UNITS__khz,
	.type = LV2_ATOM__Long,
	.mode = PROP_MODE_DYNAMIC,
	.minimum.h = 0,
	.maximum.h = 20
};

static const props_def_t dyn3 = {
	.label = "Float",
	.property = PROPS_PREFIX"Float",
	.access = LV2_PATCH__writable,
	.unit = LV2_UNITS__mhz,
	.type = LV2_ATOM__Float,
	.mode = PROP_MODE_DYNAMIC,
	.minimum.f = -0.5f,
	.maximum.f = 0.5f
};

static const props_def_t dyn4 = {
	.label = "Double",
	.property = PROPS_PREFIX"Double",
	.access = LV2_PATCH__readable,
	.unit = LV2_UNITS__db,
	.type = LV2_ATOM__Double,
	.mode = PROP_MODE_DYNAMIC,
	.minimum.d = -1.0,
	.maximum.d = 1.0
};

static const props_scale_point_t scale_points5 [] = {
	{.label = "One",		.value.i = 0},
	{.label = "Two",		.value.i = 1},
	{.label = "Three",	.value.i = 2},
	{.label = "Four",		.value.i = 3},
	{.label = NULL } // sentinel
};

static const props_def_t dyn5 = {
	.label = "scaleInt",
	.property = PROPS_PREFIX"scaleInt",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_DYNAMIC,
	.minimum.i = 0,
	.maximum.i = 3,
	.scale_points = scale_points5
};

static const props_def_t dyn6 = {
	.label = "Bool",
	.property = PROPS_PREFIX"Bool",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Bool,
	.mode = PROP_MODE_DYNAMIC,
	.minimum.d = 0,
	.maximum.d = 1
};

static const props_def_t dyn7 = {
	.label = "String",
	.property = PROPS_PREFIX"String",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__String,
	.mode = PROP_MODE_DYNAMIC,
	.maximum.s = 256, // strlen
};

static const props_def_t stat1 = {
	.label = "statInt",
	.property = PROPS_PREFIX"statInt",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_STATIC
};

static const props_def_t stat2 = {
	.label = "statLong",
	.property = PROPS_PREFIX"statLong",
	.access = LV2_PATCH__readable,
	.type = LV2_ATOM__Long,
	.mode = PROP_MODE_STATIC
};

static const props_def_t stat3 = {
	.label = "statFloat",
	.property = PROPS_PREFIX"statFloat",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Float,
	.mode = PROP_MODE_STATIC
};

static const props_def_t stat4 = {
	.label = "statDouble",
	.property = PROPS_PREFIX"statDouble",
	.access = LV2_PATCH__readable,
	.type = LV2_ATOM__Double,
	.mode = PROP_MODE_STATIC
};

static const props_def_t stat5 = {
	.label = "statString",
	.property = PROPS_PREFIX"statString",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__String,
	.mode = PROP_MODE_STATIC,
	.maximum.s = 256 // strlen
};

static const props_def_t stat6 = {
	.label = "statPath",
	.property = PROPS_PREFIX"statPath",
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Path,
	.mode = PROP_MODE_STATIC,
	.maximum.s = 256 // strlen
};

static int
_log_vprintf(plughandle_t *handle, LV2_URID type, const char *fmt, va_list args)
{
	return handle->log->vprintf(handle->log->handle, type, fmt, args);
}

// non-rt || rt with LV2_LOG__Trace
static int
_log_printf(plughandle_t *handle, LV2_URID type, const char *fmt, ...)
{
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = _log_vprintf(handle, type, fmt, args);
  va_end(args);

	return ret;
}

static void
_intercept(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	plughandle_t *handle = data;

	switch(event)
	{
		case PROP_EVENT_GET:
		{
			_log_printf(handle, handle->log_trace, "GET     : %s", impl->def->label);
			break;
		}
		case PROP_EVENT_SET:
		{
			_log_printf(handle, handle->log_trace, "SET     : %s", impl->def->label);
			break;
		}
		case PROP_EVENT_REGISTER:
		{
			_log_printf(handle, handle->log_trace, "REGISTER: %s", impl->def->label);
			break;
		}
		case PROP_EVENT_SAVE:
		{
			_log_printf(handle, handle->log_trace, "SAVE    : %s", impl->def->label);
			break;
		}
		case PROP_EVENT_RESTORE:
		{
			_log_printf(handle, handle->log_trace, "RESTORE : %s", impl->def->label);
			break;
		}
	}
}

static void
_intercept_dyn1(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	plughandle_t *handle = data;

	_intercept(data, forge, frames, event, impl);

	if(event & PROP_EVENT_WRITE)
	{
		handle->dyn.val2 = handle->dyn.val1 * 2;

		if(handle->ref)
			handle->ref = props_set(&handle->props, forge, frames, handle->urid.dyn2);
	}
}

static void
_intercept_dyn3(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	plughandle_t *handle = data;

	_intercept(data, forge, frames, event, impl);

	if(event & PROP_EVENT_WRITE)
	{
		handle->dyn.val4 = handle->dyn.val3 * 2;

		if(handle->ref)
			handle->ref = props_set(&handle->props, forge, frames, handle->urid.dyn4);
	}
}

static void
_intercept_stat1(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	plughandle_t *handle = data;

	_intercept(data, forge, frames, event, impl);

	if(event & PROP_EVENT_WRITE)
	{
		handle->stat.val2 = handle->stat.val1 * 2;

		if(handle->ref)
			handle->ref = props_set(&handle->props, forge, frames, handle->urid.stat2);
	}
}

static void
_intercept_stat3(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	plughandle_t *handle = data;

	_intercept(data, forge, frames, event, impl);

	if(event & PROP_EVENT_WRITE)
	{
		handle->stat.val4 = handle->stat.val3 * 2;

		if(handle->ref)
			handle->ref = props_set(&handle->props, forge, frames, handle->urid.stat4);
	}
}

static void
_intercept_stat6(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	plughandle_t *handle = data;

	_intercept(data, forge, frames, event, impl);

	if(event & PROP_EVENT_WRITE)
	{
		const char *path = strstr(handle->stat.val6, "file://")
			? handle->stat.val6 + 7 // skip "file://"
			: handle->stat.val6;
		FILE *f = fopen(path, "wb"); // create empty file
		if(f)
			fclose(f);
	}
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
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = features[i]->data;
	}

	if(!handle->map)
	{
		fprintf(stderr,
			"%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	if(!handle->log)
	{
		fprintf(stderr,
			"%s: Host does not support log:log\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	handle->log_trace = handle->map->map(handle->map->handle, LV2_LOG__Trace);

	lv2_atom_forge_init(&handle->forge, handle->map);
	if(!props_init(&handle->props, MAX_NPROPS, descriptor->URI, handle->map, handle))
	{
		fprintf(stderr, "failed to initialize property structure\n");
		free(handle);
		return NULL;
	}

	handle->dyn.val1 = 2;
	handle->dyn.val2 = handle->dyn.val1 * 2;
	handle->dyn.val3 = 0.2f;
	handle->dyn.val4 = handle->dyn.val3 * 2;
	handle->dyn.val5 = 0;
	handle->dyn.val6 = true;
	handle->dyn.val7[0] = '\0';

	if(  props_register(&handle->props, &dyn1, PROP_EVENT_ALL, _intercept_dyn1, &handle->dyn.val1)
		&& (handle->urid.dyn2 =
				props_register(&handle->props, &dyn2, PROP_EVENT_ALL, _intercept, &handle->dyn.val2))
		&& props_register(&handle->props, &dyn3, PROP_EVENT_ALL, _intercept_dyn3, &handle->dyn.val3)
		&& (handle->urid.dyn4 =
				props_register(&handle->props, &dyn4, PROP_EVENT_ALL, _intercept, &handle->dyn.val4))
		&& props_register(&handle->props, &dyn5, PROP_EVENT_ALL, _intercept, &handle->dyn.val5)
		&& props_register(&handle->props, &dyn6, PROP_EVENT_ALL, _intercept, &handle->dyn.val6)
		&& props_register(&handle->props, &dyn7, PROP_EVENT_ALL, _intercept, &handle->dyn.val7)

		&& props_register(&handle->props, &stat1, PROP_EVENT_ALL, _intercept_stat1, &handle->stat.val1)
		&& (handle->urid.stat2 =
				props_register(&handle->props, &stat2, PROP_EVENT_ALL, _intercept, &handle->stat.val2))
		&& props_register(&handle->props, &stat3, PROP_EVENT_ALL, _intercept_stat3, &handle->stat.val3)
		&& (handle->urid.stat4 =
				props_register(&handle->props, &stat4, PROP_EVENT_ALL, _intercept, &handle->stat.val4))
		&& props_register(&handle->props, &stat5, PROP_EVENT_ALL, _intercept, &handle->stat.val5)
		&& props_register(&handle->props, &stat6, PROP_EVENT_ALL, _intercept_stat6, &handle->stat.val6) )
	{
		props_sort(&handle->props);
	}
	else
	{
		_log_printf(handle, handle->log_trace, "ERR     : registering");
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
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	uint32_t capacity = handle->event_out->atom.size;
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_set_buffer(&handle->forge, (uint8_t *)handle->event_out, capacity);
	handle->ref = lv2_atom_forge_sequence_head(&handle->forge, &frame, 0);

	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;

		if(handle->ref)
			props_advance(&handle->props, &handle->forge, ev->time.frames, obj, &handle->ref); //TODO handle return
	}
	if(handle->ref)
		lv2_atom_forge_pop(&handle->forge, &frame);
	else
		lv2_atom_sequence_clear(handle->event_out);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

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

LV2_State_Interface state_iface = {
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

const LV2_Descriptor props_test = {
	.URI						= PROPS_TEST_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};

#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
	switch(index)
	{
		case 0:
			return &props_test;
		default:
			return NULL;
	}
}
