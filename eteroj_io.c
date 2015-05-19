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

#include <uv.h>

#include <osc.h>
#include <osc_stream.h>

#include <eteroj.h>
#include <varchunk.h>
#include <lv2_osc.h>

#if defined(_WIN32)
#	include <avrt.h>
#endif

#define BUF_SIZE 0x10000

typedef enum _plugstate_t plugstate_t;
typedef struct _plughandle_t plughandle_t;

enum _plugstate_t {
	STATE_NONE					= 0,
	STATE_TIMEDOUT			= 1,
	STATE_RESOLVED			= 2,
	STATE_CONNECTED			= 3,
	STATE_DISCONNECTED	= 4
};

struct _plughandle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID osc_event;
		LV2_URID state_default;
		LV2_URID eteroj_url;
		LV2_URID eteroj_trig;
		LV2_URID eteroj_dirty;
		
		LV2_URID log_note;
		LV2_URID log_error;
		LV2_URID log_trace;
	} uris;

	osc_forge_t oforge;
	LV2_Atom_Forge forge;

	char *osc_url;
	volatile int dirty;

	LV2_Log_Log *log;

	const LV2_Atom_Sequence *osc_sink;
	LV2_Atom_Sequence *osc_source;
	float *state;

	uv_thread_t thread;
	uv_loop_t loop;
	uv_async_t quit;
	uv_async_t flush;

	struct {
		osc_stream_driver_t driver;
		osc_stream_t *stream;
		varchunk_t *from_worker;
		varchunk_t *to_worker;
		LV2_Atom_Forge_Frame obj_frame;
		LV2_Atom_Forge_Frame tup_frame;
	} data;
};

static void
lprintf(plughandle_t *handle, LV2_URID type, const char *fmt, ...)
{
	if(handle->log)
	{
		va_list args;
		va_start(args, fmt);
		handle->log->vprintf(handle->log->handle, type, fmt, args);
		va_end(args);
	}
	else if(type != handle->uris.log_trace)
	{
		const char *type_str = NULL;
		if(type == handle->uris.log_note)
			type_str = "Note";
		else if(type == handle->uris.log_error)
			type_str = "Error";

		fprintf(stderr, "[%s]", type_str);
		va_list args;
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);
		fputc('\n', stderr);
	}
}

static LV2_State_Status
_state_save(LV2_Handle instance, LV2_State_Store_Function store,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = (plughandle_t *)instance;

	return store(
		state,
		handle->uris.eteroj_url,
		handle->osc_url,
		strlen(handle->osc_url) + 1,
		handle->forge.String,
		LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
}

static LV2_State_Status
_state_restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = (plughandle_t *)instance;

	size_t size;
	uint32_t type;
	uint32_t flags2;
	const char *osc_url = retrieve(
		state,
		handle->uris.eteroj_url,
		&size,
		&type,
		&flags2
	);

	// check type
	if(type != handle->forge.String)
		return LV2_STATE_ERR_BAD_TYPE;

	// check flags
	/* no need to check, as string IS POD and IS PORTABLE everywhere
	if(  !(flags2 & LV2_STATE_IS_POD)
		|| !(flags2 & LV2_STATE_IS_PORTABLE) )
		return LV2_STATE_ERR_BAD_FLAGS;
	*/

	if(osc_url && size)
	{
		if(handle->osc_url)
			free(handle->osc_url);
		handle->osc_url = strdup(osc_url);
		//handle->dirty = 1; // atomic instruction //FIXME
	}

	return LV2_STATE_SUCCESS;
}
	
static const LV2_State_Interface state_iface = {
	.save = _state_save,
	.restore = _state_restore
};

// non-rt
static void *
_data_recv_req(size_t size, void *data)
{
	plughandle_t *handle = data;

	void *ptr;
	do ptr = varchunk_write_request(handle->data.from_worker, size);
	while(!ptr);

	return ptr;
}

// non-rt
static void
_data_recv_adv(size_t written, void *data)
{
	plughandle_t *handle = data;

	varchunk_write_advance(handle->data.from_worker, written);
}

// non-rt
static const void *
_data_send_req(size_t *len, void *data)
{
	plughandle_t *handle = data;

	return varchunk_read_request(handle->data.to_worker, len);
}

// non-rt
static void
_data_send_adv(void *data)
{
	plughandle_t *handle = data;

	varchunk_read_advance(handle->data.to_worker);
}

static void
_bundle_in(osc_time_t timestamp, void *data)
{
	plughandle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Frame *obj_frame = &handle->data.obj_frame;
	LV2_Atom_Forge_Frame *tup_frame = &handle->data.tup_frame;

	osc_forge_bundle_push(&handle->oforge, forge, obj_frame, tup_frame, timestamp);
}

static void
_bundle_out(osc_time_t timestamp, void *data)
{
	plughandle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Frame *obj_frame = &handle->data.obj_frame;
	LV2_Atom_Forge_Frame *tup_frame = &handle->data.tup_frame;
	
	osc_forge_bundle_pop(&handle->oforge, forge, obj_frame, tup_frame);
}

static int
_resolve(osc_time_t timestamp, const char *path, const char *fmt,
	osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	//TODO
	*handle->state = STATE_RESOLVED;

	return 1;
}

static int
_timeout(osc_time_t timestamp, const char *path, const char *fmt,
	osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	//TODO
	*handle->state = STATE_TIMEDOUT;

	return 1;
}

static int
_connect(osc_time_t timestamp, const char *path, const char *fmt,
	osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	//TODO
	*handle->state = STATE_CONNECTED;

	return 1;
}

static int
_disconnect(osc_time_t timestamp, const char *path, const char *fmt,
	osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	//TODO
	*handle->state = STATE_DISCONNECTED;

	return 1;
}

static int
_message(osc_time_t timestamp, const char *path, const char *fmt,
	osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Frame obj_frame;
	LV2_Atom_Forge_Frame tup_frame;

	osc_data_t *ptr = buf;

	osc_forge_message_push(&handle->oforge, forge, &obj_frame, &tup_frame,
		path, fmt);

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
			{
				osc_forge_true(&handle->oforge, forge);
				break;
			}
			case 'F':
			{
				osc_forge_false(&handle->oforge, forge);
				break;
			}
			case 'N':
			{
				osc_forge_nil(&handle->oforge, forge);
				break;
			}
			case 'I':
			{
				osc_forge_bang(&handle->oforge, forge);
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
				uint8_t *m;
				ptr = osc_get_midi(ptr, &m);
				osc_forge_midi(&handle->oforge, forge, m);
				break;
			}
		}

	osc_forge_message_pop(&handle->oforge, forge, &obj_frame, &tup_frame);

	return 1;
}

static const osc_method_t methods [] = {
	{"/stream/resolve", "", _resolve},
	{"/stream/timeout", "", _timeout},
	{"/stream/connect", "", _connect},
	{"/stream/disconnect", "", _disconnect},
	{NULL, NULL, _message},

	{NULL, NULL, NULL}
};

static void
_unroll_stamp(osc_time_t tstmp, void *data)
{
	plughandle_t *handle = data;

	//TODO
}

static void
_unroll_message(osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge;

	lv2_atom_forge_frame_time(forge, 0); //TODO
	osc_dispatch_method(OSC_IMMEDIATE, buf, size, (osc_method_t *)methods,
		NULL, NULL, handle);
}

static void
_unroll_bundle(osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge;

	lv2_atom_forge_frame_time(forge, 0); //TODO
	osc_dispatch_method(OSC_IMMEDIATE, buf, size, (osc_method_t *)methods,
		_bundle_in, _bundle_out, handle);
}

static const osc_unroll_inject_t inject = {
	.stamp = _unroll_stamp,
	.message = _unroll_message,
	.bundle = _unroll_bundle
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	int i;
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	for(i=0; features[i]; i++)
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = (LV2_URID_Map *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = (LV2_Log_Log *)features[i]->data;

	if(!handle->map)
	{
		lprintf(handle, handle->uris.log_error,
			"%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	
	handle->uris.osc_event = handle->map->map(handle->map->handle,
		LV2_OSC__OscEvent);
	handle->uris.state_default = handle->map->map(handle->map->handle,
		LV2_STATE__loadDefaultState);
	handle->uris.eteroj_url = handle->map->map(handle->map->handle,
		ETEROJ_URL_URI);
	handle->uris.eteroj_trig = handle->map->map(handle->map->handle,
		ETEROJ_TRIG_URI);
	handle->uris.eteroj_dirty = handle->map->map(handle->map->handle,
		ETEROJ_DIRTY_URI);
	handle->uris.log_note = handle->map->map(handle->map->handle,
		LV2_LOG__Note);
	handle->uris.log_error = handle->map->map(handle->map->handle,
		LV2_LOG__Error);
	handle->uris.log_trace = handle->map->map(handle->map->handle,
		LV2_LOG__Trace);

	osc_forge_init(&handle->oforge, handle->map);
	lv2_atom_forge_init(&handle->forge, handle->map);

	// init data
	handle->data.from_worker = varchunk_new(BUF_SIZE);
	handle->data.to_worker = varchunk_new(BUF_SIZE);
	if(!handle->data.from_worker || !handle->data.to_worker)
	{
		free(handle);
		return NULL;
	}

	handle->data.driver.recv_req = _data_recv_req;
	handle->data.driver.recv_adv = _data_recv_adv;
	handle->data.driver.send_req = _data_send_req;
	handle->data.driver.send_adv = _data_send_adv;

	//handle->osc_url = strdup("osc.udp4://:9999");
	//handle->osc_url = strdup("osc.udp6://:9999");
	//handle->osc_url = strdup("osc.tcp4://:9999");
	handle->osc_url = strdup("osc.tcp6://:9999");
	//handle->osc_url = strdup("osc.slip.tcp4://:9999");
	//handle->osc_url = strdup("osc.slip.tcp6://:9999");

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	plughandle_t *handle = (plughandle_t *)instance;

	switch(port)
	{
		case 0:
			handle->osc_sink = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->osc_source = (LV2_Atom_Sequence *)data;
			break;
		case 2:
			handle->state = (float *)data;
			break;
		default:
			break;
	}
}

// non-rt
static void
_quit(uv_async_t *quit)
{
	plughandle_t *handle = quit->data;

	uv_close((uv_handle_t *)&handle->quit, NULL);
	uv_close((uv_handle_t *)&handle->flush, NULL);
	osc_stream_free(handle->data.stream);
}

// non-rt
static void
_flush(uv_async_t *quit)
{
	plughandle_t *handle = quit->data;

	// flush sending queue
	osc_stream_flush(handle->data.stream);
}

// non-rt
static void
_thread(void *data)
{
	plughandle_t *handle = data;

	const int priority = 50;

#if defined(_WIN32)
	int mcss_sched_priority;
	mcss_sched_priority = priority > 50 // TODO when to use CRITICAL?
		? AVRT_PRIORITY_CRITICAL
		: (priority > 0
			? AVRT_PRIORITY_HIGH
			: AVRT_PRIORITY_NORMAL);

	// Multimedia Class Scheduler Service
	DWORD dummy = 0;
	HANDLE task = AvSetMmThreadCharacteristics("Pro Audio", &dummy);
	if(!task)
		fprintf(stderr, "AvSetMmThreadCharacteristics error: %d\n", GetLastError());
	else if(!AvSetMmThreadPriority(task, mcss_sched_priority))
		fprintf(stderr, "AvSetMmThreadPriority error: %d\n", GetLastError());

#else
	struct sched_param schedp;
	memset(&schedp, 0, sizeof(struct sched_param));
	schedp.sched_priority = priority;
	
	if(pthread_setschedparam(pthread_self(), SCHED_RR, &schedp))
		fprintf(stderr, "pthread_setschedparam error\n");
#endif

	// main loop
	uv_run(&handle->loop, UV_RUN_DEFAULT);
}

static void
activate(LV2_Handle instance)
{
	plughandle_t *handle = (plughandle_t *)instance;

	uv_loop_init(&handle->loop);

	handle->quit.data = handle;
	uv_async_init(&handle->loop, &handle->quit, _quit);
	
	handle->flush.data = handle;
	uv_async_init(&handle->loop, &handle->flush, _flush);

	handle->data.stream = osc_stream_new(&handle->loop, handle->osc_url,
		&handle->data.driver, handle);

	uv_thread_create(&handle->thread, _thread, handle);
}

// rt-thread
static void
_recv(uint64_t timestamp, const char *path, const char *fmt,
	const LV2_Atom_Tuple *body, void *data)
{
	plughandle_t *handle = data;

	size_t reserve = osc_strlen(path) + osc_strlen(fmt) + body->atom.size;

	osc_data_t *buf;
	if((buf = varchunk_write_request(handle->data.to_worker, reserve)))
	{
		osc_data_t *ptr = buf;
		osc_data_t *end = buf + reserve;

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
					ptr = osc_set_int32(ptr, end, ((const LV2_Atom_Int *)itr)->body);
					break;
				case 'f':
					ptr = osc_set_float(ptr, end, ((const LV2_Atom_Float *)itr)->body);
					break;
				case 's':
				case 'S':
					ptr = osc_set_string(ptr, end, LV2_ATOM_BODY_CONST(itr));
					break;
				case 'b':
					ptr = osc_set_blob(ptr, end, itr->size, LV2_ATOM_BODY(itr));
					break;
				
				case 'h':
					ptr = osc_set_int64(ptr, end, ((const LV2_Atom_Long *)itr)->body);
					break;
				case 'd':
					ptr = osc_set_double(ptr, end, ((const LV2_Atom_Double *)itr)->body);
					break;
				case 't':
					ptr = osc_set_timetag(ptr, end, ((const LV2_Atom_Long *)itr)->body);
					break;
				
				case 'T':
				case 'F':
				case 'N':
				case 'I':
					break;
				
				case 'c':
					ptr = osc_set_char(ptr, end, ((const LV2_Atom_Int *)itr)->body);
					break;
				case 'm':
					ptr = osc_set_midi(ptr, end, LV2_ATOM_BODY(itr));
					break;
			}
		}

		size_t size = ptr ? ptr - buf : 0;

		if(size)
			varchunk_write_advance(handle->data.to_worker, size);
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = (plughandle_t *)instance;
	
	// write outgoing data
	LV2_ATOM_SEQUENCE_FOREACH(handle->osc_sink, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		osc_atom_unpack(&handle->oforge, obj, _recv, handle);
	}
	if(handle->osc_sink->atom.size > sizeof(LV2_Atom_Sequence_Body))
		uv_async_send(&handle->flush);

	// read incoming data
	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Frame frame;
	uint32_t capacity = handle->osc_source->atom.size;

	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->osc_source, capacity);
	lv2_atom_forge_sequence_head(forge, &frame, 0);

	const void *ptr;
	size_t size;
	while((ptr = varchunk_read_request(handle->data.from_worker, &size)))
	{
		osc_unroll_packet((osc_data_t *)ptr, size, OSC_UNROLL_MODE_PARTIAL,
			(osc_unroll_inject_t *)&inject, handle);

		varchunk_read_advance(handle->data.from_worker);
	}
	lv2_atom_forge_pop(forge, &frame);
}

static void
deactivate(LV2_Handle instance)
{
	plughandle_t *handle = (plughandle_t *)instance;

	uv_async_send(&handle->quit);
	uv_thread_join(&handle->thread);
	uv_loop_close(&handle->loop);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = (plughandle_t *)instance;

	varchunk_free(handle->data.from_worker);
	varchunk_free(handle->data.to_worker);
	free(handle);
}

static const void*
extension_data(const char* uri)
{
	if(!strcmp(uri, LV2_STATE__interface))
		return &state_iface;
		
	return NULL;
}

const LV2_Descriptor eteroj_io = {
	.URI						= ETEROJ_IO_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
