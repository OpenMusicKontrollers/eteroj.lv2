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
#include <tlsf.h>

#define POOL_SIZE 0x20000 // 128KB
#define BUF_SIZE 0x10000

typedef enum _plugstate_t plugstate_t;
typedef struct _list_t list_t;
typedef struct _plughandle_t plughandle_t;

enum _plugstate_t {
	STATE_IDLE					= 0,
	STATE_READY					= 1,
	STATE_TIMEDOUT			= 2,
	STATE_ERRORED				= 3,
	STATE_CONNECTED			= 4
};

struct _list_t {
	list_t *next;
	int64_t frames;

	size_t size;
	osc_data_t buf [0];
};

struct _plughandle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID state_default;
		LV2_URID subject;

		LV2_URID eteroj_event;
		LV2_URID eteroj_url;
		LV2_URID eteroj_stat;

		LV2_URID log_note;
		LV2_URID log_error;
		LV2_URID log_trace;

		LV2_URID patch_message;
		LV2_URID patch_set;
		LV2_URID patch_get;
		LV2_URID patch_subject;
		LV2_URID patch_property;
		LV2_URID patch_value;
	} uris;

	osc_forge_t oforge;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	volatile int needs_flushing;
	volatile int restored;
	volatile int url_updated;
	volatile int status_updated;
	volatile int reconnection_requested;
	char osc_url [512];
	char osc_status [512];

	LV2_Log_Log *log;

	const LV2_Atom_Sequence *osc_in;
	LV2_Atom_Sequence *osc_out;
	float *state;

	uv_loop_t loop;

	LV2_Worker_Schedule *sched;
	LV2_Worker_Respond_Function respond;
	LV2_Worker_Respond_Handle target;

	osc_schedule_t *osc_sched;
	list_t *list;
	uint8_t mem [POOL_SIZE];
	tlsf_t tlsf;

	struct {
		osc_stream_driver_t driver;
		osc_stream_t *stream;
		varchunk_t *from_worker;
		varchunk_t *to_worker;
		int frame_cnt;
		LV2_Atom_Forge_Frame frame [32][2]; // 32 nested bundles should be enough
		
		osc_data_t *buf;
		osc_data_t *ptr;
		osc_data_t *end;
		osc_data_t *bndl [32]; // 32 nested bundles should be enough
		int bndl_cnt;
	} data;
};

static inline list_t *
_list_insert(list_t *root, list_t *item)
{
	if(!root || (item->frames < root->frames) ) // prepend
	{
		item->next = root;
		return item;
	}

	list_t *l0;
	for(l0 = root; l0->next != NULL; l0 = l0->next)
	{
		if(item->frames < l0->next->frames)
			break; // found insertion point
	}

	item->next = l0->next; // is NULL at end of list
	l0->next = item;
	return root;
}

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

	strcpy(handle->osc_url, osc_url);
	handle->restored = 1;

	return LV2_STATE_SUCCESS;
}

static const LV2_State_Interface state_iface = {
	.save = _state_save,
	.restore = _state_restore
};

// non-rt
static int
_worker_api_flush(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	osc_stream_flush(handle->data.stream);

	return 1;
}

// non-rt
static int
_worker_api_recv(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	// do nothing

	return 1;
}

// non-rt
static int
_worker_api_url(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;
	const osc_data_t *ptr = buf;

	const char *osc_url;
	ptr = osc_get_string(ptr, &osc_url);

	strcpy(handle->osc_url, osc_url);

	if(handle->data.stream)
		osc_stream_free(handle->data.stream);
	else
		handle->reconnection_requested = 1;

	return 1;
}

static const osc_method_t worker_api [] = {
	{"/eteroj/flush", NULL, _worker_api_flush},
	{"/eteroj/recv", NULL, _worker_api_recv},
	{"/eteroj/url", "s", _worker_api_url},

	{NULL, NULL, NULL}
};

// non-rt thread
static LV2_Worker_Status
_work(LV2_Handle instance,
	LV2_Worker_Respond_Function respond,
	LV2_Worker_Respond_Handle target,
	uint32_t size,
	const void *body)
{
	plughandle_t *handle = instance;

	if(handle->reconnection_requested)
	{
		handle->data.stream = osc_stream_new(&handle->loop, handle->osc_url,
			&handle->data.driver, handle);

		handle->reconnection_requested = 0;
	}

	handle->respond = respond;
	handle->target = target;

	osc_dispatch_method(body, size, worker_api, NULL, NULL, handle);
	uv_run(&handle->loop, UV_RUN_NOWAIT);

	handle->respond = NULL;
	handle->target = NULL;

	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_work_response(LV2_Handle instance, uint32_t size, const void *body)
{
	plughandle_t *handle = instance;

	// do nothing

	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_end_run(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	// do nothing

	return LV2_WORKER_SUCCESS;
}

static const LV2_Worker_Interface work_iface = {
	.work = _work,
	.work_response = _work_response,
	.end_run = _end_run
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

// non-rt
static void
_data_free(void *data)
{
	plughandle_t *handle = data;

	handle->data.stream = NULL;
	handle->reconnection_requested = 1;
}

// rt
static int
_resolve(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	*handle->state = STATE_READY;

	sprintf(handle->osc_status, "resolved");

	handle->needs_flushing = 1;
	handle->url_updated = 1;
	handle->status_updated = 1;

	return 1;
}

// rt
static int
_timeout(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	*handle->state = STATE_TIMEDOUT;

	sprintf(handle->osc_status, "timed out");

	handle->status_updated = 1;

	return 1;
}

// rt
static int
_error(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	*handle->state = STATE_ERRORED;

	const char *where;
	const char *what;

	const osc_data_t *ptr = buf;
	ptr = osc_get_string(ptr, &where);
	ptr = osc_get_string(ptr, &what);

	sprintf(handle->osc_status, "%s (%s)\n", where, what);

	handle->status_updated = 1;

	return 1;
}

// rt
static int
_connect(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	*handle->state = STATE_CONNECTED;

	sprintf(handle->osc_status, "connected");

	handle->needs_flushing = 1;
	handle->status_updated = 1;

	return 1;
}

// rt
static int
_disconnect(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	*handle->state = STATE_READY;

	sprintf(handle->osc_status, "disconnected");

	handle->status_updated = 1;

	return 1;
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

	if(!handle->data.frame_cnt) // message not in a bundle
		lv2_atom_forge_frame_time(forge, 0);

	LV2_Atom_Forge_Ref ref = handle->ref;

	if(ref)
		ref = osc_forge_message_push(&handle->oforge, forge, frame, path, fmt);

	for(const char *type = fmt; *type; type++)
		switch(*type)
		{
			case 'i':
			{
				int32_t i;
				ptr = osc_get_int32(ptr, &i);
				if(ref)
					ref = osc_forge_int32(&handle->oforge, forge, i);
				break;
			}
			case 'f':
			{
				float f;
				ptr = osc_get_float(ptr, &f);
				if(ref)
					ref = osc_forge_float(&handle->oforge, forge, f);
				break;
			}
			case 's':
			case 'S':
			{
				const char *s;
				ptr = osc_get_string(ptr, &s);
				if(ref)
					ref = osc_forge_string(&handle->oforge, forge, s);
				break;
			}
			case 'b':
			{
				osc_blob_t b;
				ptr = osc_get_blob(ptr, &b);
				if(ref)
					ref = osc_forge_blob(&handle->oforge, forge, b.size, b.payload);
				break;
			}

			case 'h':
			{
				int64_t h;
				ptr = osc_get_int64(ptr, &h);
				if(ref)
					ref = osc_forge_int64(&handle->oforge, forge, h);
				break;
			}
			case 'd':
			{
				double d;
				ptr = osc_get_double(ptr, &d);
				if(ref)
					ref = osc_forge_double(&handle->oforge, forge, d);
				break;
			}
			case 't':
			{
				uint64_t t;
				ptr = osc_get_timetag(ptr, &t);
				if(ref)
					ref = osc_forge_timestamp(&handle->oforge, forge, t);
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
				if(ref)
					ref = osc_forge_char(&handle->oforge, forge, c);
				break;
			}
			case 'm':
			{
				const uint8_t *m;
				ptr = osc_get_midi(ptr, &m);
				if(ref)
					ref = osc_forge_midi(&handle->oforge, forge, 3, m + 1); // skip port byte
				break;
			}
		}

	if(ref)
		osc_forge_message_pop(&handle->oforge, forge, frame);

	handle->ref = ref;

	return 1;
}

static const osc_method_t methods [] = {
	{"/stream/resolve", "", _resolve},
	{"/stream/timeout", "", _timeout},
	{"/stream/error", "ss", _error},
	{"/stream/connect", "", _connect},
	{"/stream/disconnect", "", _disconnect},
	{NULL, NULL, _message},

	{NULL, NULL, NULL}
};

// rt
static void
_unroll_stamp(osc_time_t stamp, void *data)
{
	plughandle_t *handle = data;

	//FIXME
}

// rt
static void
_unroll_message(const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	osc_dispatch_method(buf, size, methods, NULL, NULL, handle);
}

// rt
static void
_unroll_bundle(const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	uint64_t time = be64toh(*(uint64_t *)(buf + 8));

	int64_t frames;
	if(handle->osc_sched)
		frames = handle->osc_sched->osc2frames(handle->osc_sched->handle, time);
	else
		frames = 0;

	// add event to list
	list_t *l = tlsf_malloc(handle->tlsf, sizeof(list_t) + size);
	if(l)
	{
		l->frames = frames;
		l->size = size;
		memcpy(l->buf, buf, size);

		handle->list = _list_insert(handle->list, l);
	}
	else
		lprintf(handle, handle->uris.log_trace, "message pool overflow");
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
			handle->map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_WORKER__schedule))
			handle->sched = features[i]->data;
		else if(!strcmp(features[i]->URI, OSC__schedule))
			handle->osc_sched = features[i]->data;

	if(!handle->map)
	{
		lprintf(handle, handle->uris.log_error,
			"%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	if(!handle->sched)
	{
		lprintf(handle, handle->uris.log_error,
			"%s: Host does not support worker:sched\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	handle->tlsf = tlsf_create_with_pool(handle->mem, POOL_SIZE);

	handle->uris.state_default = handle->map->map(handle->map->handle,
		LV2_STATE__loadDefaultState);
	handle->uris.subject = handle->map->map(handle->map->handle,
		descriptor->URI);

	handle->uris.eteroj_event = handle->map->map(handle->map->handle,
		ETEROJ_EVENT_URI);
	handle->uris.eteroj_url = handle->map->map(handle->map->handle,
		ETEROJ_URL_URI);
	handle->uris.eteroj_stat = handle->map->map(handle->map->handle,
		ETEROJ_STAT_URI);

	handle->uris.log_note = handle->map->map(handle->map->handle,
		LV2_LOG__Note);
	handle->uris.log_error = handle->map->map(handle->map->handle,
		LV2_LOG__Error);
	handle->uris.log_trace = handle->map->map(handle->map->handle,
		LV2_LOG__Trace);

	handle->uris.patch_message = handle->map->map(handle->map->handle,
		LV2_PATCH__Message);
	handle->uris.patch_set = handle->map->map(handle->map->handle,
		LV2_PATCH__Set);
	handle->uris.patch_get = handle->map->map(handle->map->handle,
		LV2_PATCH__Get);
	handle->uris.patch_subject = handle->map->map(handle->map->handle,
		LV2_PATCH__subject);
	handle->uris.patch_property = handle->map->map(handle->map->handle,
		LV2_PATCH__property);
	handle->uris.patch_value = handle->map->map(handle->map->handle,
		LV2_PATCH__value);

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
	handle->data.driver.free = _data_free;

	strcpy(handle->osc_url, "osc.udp4://localhost:9090");
	strcpy(handle->osc_status, "");

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
			handle->osc_out = (LV2_Atom_Sequence *)data;
			break;
		case 2:
			handle->state = (float *)data;
			break;
		default:
			break;
	}
}

static void
activate(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	uv_loop_init(&handle->loop);

	handle->restored = 1;
}

// rt
static void
_bundle_push_cb(uint64_t timestamp, void *data)
{
	plughandle_t *handle = data;

	handle->data.ptr = osc_start_bundle(handle->data.ptr, handle->data.end,
		timestamp, &handle->data.bndl[handle->data.bndl_cnt++]);
}

// rt
static void
_bundle_pop_cb(void *data)
{
	plughandle_t *handle = data;

	handle->data.ptr = osc_end_bundle(handle->data.ptr, handle->data.end,
		handle->data.bndl[--handle->data.bndl_cnt]);
}

// rt
static void
_message_cb(const char *path, const char *fmt,
	const LV2_Atom_Tuple *body, void *data)
{
	plughandle_t *handle = data;

	osc_data_t *ptr = handle->data.ptr;
	const osc_data_t *end = handle->data.end;

	osc_data_t *itm;
	if(handle->data.bndl_cnt)
		ptr = osc_start_bundle_item(ptr, end, &itm);

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
				ptr = osc_set_blob(ptr, end, itr->size, LV2_ATOM_BODY(itr));
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

	if(handle->data.bndl_cnt)
		ptr = osc_end_bundle_item(ptr, end, itm);

	handle->data.ptr = ptr;
}

static const char flush_msg [] = "/eteroj/flush\0\0\0,\0\0\0";
static const char recv_msg [] = "/eteroj/recv\0\0\0\0,\0\0\0";

// rt
static void
_url_change(plughandle_t *handle, const char *url)
{
	osc_data_t buf [512];
	osc_data_t *ptr = buf;
	osc_data_t *end = buf + 512;

	ptr = osc_set_path(ptr, end, "/eteroj/url");
	ptr = osc_set_fmt(ptr, end, "s");
	ptr = osc_set_string(ptr, end, url);

	if(ptr)
	{
		LV2_Worker_Status status = handle->sched->schedule_work(
			handle->sched->handle, ptr - buf, buf);
		//TODO check status
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Frame frame;
	uint32_t capacity;
	handle->needs_flushing = 0;

	// prepare sequence forges
	capacity = handle->osc_out->atom.size;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->osc_out, capacity);
	handle->ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

	// write outgoing data
	LV2_ATOM_SEQUENCE_FOREACH(handle->osc_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;

		if(obj->atom.type == forge->Object)
		{
			if(obj->body.otype == handle->uris.patch_set)
			{
				const LV2_Atom_URID *subject = NULL;
				const LV2_Atom_URID *property = NULL;
				const LV2_Atom_String *value = NULL;

				LV2_Atom_Object_Query q[] = {
					{ handle->uris.patch_subject, (const LV2_Atom **)&subject },
					{ handle->uris.patch_property, (const LV2_Atom **)&property },
					{ handle->uris.patch_value, (const LV2_Atom **)&value },
					LV2_ATOM_OBJECT_QUERY_END
				};
				lv2_atom_object_query(obj, q);

				if(subject && (subject->body != handle->uris.subject))
					continue; // subject not matching

				if(property && (property->body == handle->uris.eteroj_url)
					&& value && value->atom.size)
				{
					const char *new_url = LV2_ATOM_BODY_CONST(value);

					_url_change(handle, new_url);
				}
			}
			else if(obj->body.otype == handle->uris.patch_get)
			{
				const LV2_Atom_URID *subject = NULL;
				const LV2_Atom_URID *property = NULL;

				LV2_Atom_Object_Query q[] = {
					{ handle->uris.patch_subject, (const LV2_Atom **)&subject },
					{ handle->uris.patch_property, (const LV2_Atom **)&property },
					LV2_ATOM_OBJECT_QUERY_END
				};
				lv2_atom_object_query(obj, q);

				if(subject && (subject->body != handle->uris.subject))
					continue; // subject not matching

				if(property && (property->body == handle->uris.eteroj_url))
					handle->url_updated = 1;
				else if(property && (property->body == handle->uris.eteroj_stat))
					handle->status_updated = 1;
			}
			else
			{
				size_t reserve = obj->atom.size;
				if((handle->data.buf = varchunk_write_request(handle->data.to_worker, reserve)))
				{
					handle->data.ptr = handle->data.buf;
					handle->data.end = handle->data.buf + reserve;

					osc_atom_event_unroll(&handle->oforge, obj, _bundle_push_cb,
						_bundle_pop_cb, _message_cb, handle);

					size_t size = handle->data.ptr
						? handle->data.ptr - handle->data.buf
						: 0;

					if(size)
						varchunk_write_advance(handle->data.to_worker, size);
				}
			}
		}
	}
	if(handle->osc_in->atom.size > sizeof(LV2_Atom_Sequence_Body))
		handle->needs_flushing = 1;

	// reschedule scheduled bundles
	list_t *l;
	for(l = handle->list; l; l = l->next)
	{
		uint64_t time = be64toh(*(uint64_t *)(l->buf + 8));

		int64_t frames = handle->osc_sched->osc2frames(handle->osc_sched->handle, time);
		l->frames = frames;
	}

	// read incoming data
	const osc_data_t *ptr;
	size_t size;
	while((ptr = varchunk_read_request(handle->data.from_worker, &size)))
	{
		if(osc_check_packet(ptr, size))
			osc_unroll_packet((osc_data_t *)ptr, size, OSC_UNROLL_MODE_PARTIAL, &inject, handle);

		varchunk_read_advance(handle->data.from_worker);
	}

	// handle scheduled bundles
	for(l = handle->list; l; )
	{
		uint64_t time = be64toh(*(uint64_t *)(l->buf + 8));
		//lprintf(handle, handle->uris.log_trace, "frames: %lu, %lu", time, l->frames);

		if(l->frames < 0) // late event
		{
			lprintf(handle, handle->uris.log_trace, "late event: %li samples", l->frames);
			l->frames = 0; // dispatch as early as possible
		}
		else if(l->frames >= nsamples) // not scheduled for this period
		{
			l = l->next;
			continue;
		}

		if(handle->ref)
			handle->ref = lv2_atom_forge_frame_time(forge, l->frames);
		if(handle->ref)
			handle->ref = osc_forge_bundle_push(&handle->oforge, forge,
				handle->data.frame[handle->data.frame_cnt++], time);
		if(handle->ref)
			osc_dispatch_method(l->buf, l->size, methods, NULL, NULL, handle);
		if(handle->ref)
			osc_forge_bundle_pop(&handle->oforge, forge,
				handle->data.frame[--handle->data.frame_cnt]);

		list_t *l0 = l;
		l = l->next;
		handle->list = l;
		tlsf_free(handle->tlsf, l0);
	}

	if(handle->needs_flushing)
	{
		LV2_Worker_Status status = handle->sched->schedule_work(
			handle->sched->handle, sizeof(flush_msg), flush_msg);
		//TODO check status
	}
	else
	{
		LV2_Worker_Status status = handle->sched->schedule_work(
			handle->sched->handle, sizeof(recv_msg), recv_msg);
		//TODO check status
	}

	if(handle->url_updated)
	{
		// create response
		lv2_atom_forge_frame_time(forge, nsamples-1);
		LV2_Atom_Forge_Frame obj_frame;
		lv2_atom_forge_object(forge, &obj_frame, 0, handle->uris.patch_set);
		lv2_atom_forge_key(forge, handle->uris.patch_subject);
			lv2_atom_forge_urid(forge, handle->uris.subject);
		lv2_atom_forge_key(forge, handle->uris.patch_property);
			lv2_atom_forge_urid(forge, handle->uris.eteroj_url);
		lv2_atom_forge_key(forge, handle->uris.patch_value);
			lv2_atom_forge_string(forge, handle->osc_url, strlen(handle->osc_url));
		lv2_atom_forge_pop(forge, &obj_frame);

		handle->url_updated = 0;
	}

	if(handle->status_updated)
	{
		// create response
		lv2_atom_forge_frame_time(forge, nsamples-1);
		LV2_Atom_Forge_Frame obj_frame;
		lv2_atom_forge_object(forge, &obj_frame, 0, handle->uris.patch_set);
		lv2_atom_forge_key(forge, handle->uris.patch_subject);
			lv2_atom_forge_urid(forge, handle->uris.subject);
		lv2_atom_forge_key(forge, handle->uris.patch_property);
			lv2_atom_forge_urid(forge, handle->uris.eteroj_stat);
		lv2_atom_forge_key(forge, handle->uris.patch_value);
			lv2_atom_forge_string(forge, handle->osc_status, strlen(handle->osc_status));
		lv2_atom_forge_pop(forge, &obj_frame);

		handle->status_updated = 0;
	}

	if(handle->ref)
		lv2_atom_forge_pop(forge, &frame);

	if(handle->restored)
	{
		_url_change(handle, handle->osc_url);

		handle->restored = 0;
	}
}

static void
deactivate(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	if(handle->data.stream)
		osc_stream_free(handle->data.stream);

	uv_run(&handle->loop, UV_RUN_NOWAIT);

	uv_loop_close(&handle->loop);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	tlsf_destroy(handle->tlsf);
	varchunk_free(handle->data.from_worker);
	varchunk_free(handle->data.to_worker);
	free(handle);
}

static const void*
extension_data(const char* uri)
{
	if(!strcmp(uri, LV2_STATE__interface))
		return &state_iface;
	else if(!strcmp(uri, LV2_WORKER__interface))
		return &work_iface;

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
