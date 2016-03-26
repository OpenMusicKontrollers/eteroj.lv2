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

#include <uv.h>

#include <osc.h>
#include <osc_stream.h>

#include <eteroj.h>
#include <varchunk.h>
#include <lv2_osc.h>
#include <props.h>
#include <tlsf.h>

#define POOL_SIZE 0x20000 // 128KB
#define BUF_SIZE 0x10000
#define MAX_NPROPS 3

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
	double frames;

	size_t size;
	osc_data_t buf [0];
};

struct _plughandle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID state_default;
		LV2_URID subject;

		LV2_URID eteroj_stat;
		LV2_URID eteroj_err;
	} uris;

	PROPS_T(props, MAX_NPROPS);
	osc_forge_t oforge;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	volatile bool needs_flushing;
	volatile bool status_updated;
	volatile bool reconnection_requested;
	char worker_url [512];
	char osc_url [512];
	int32_t osc_status;
	char osc_error [512];

	LV2_Log_Log *log;
	LV2_Log_Logger logger;

	const LV2_Atom_Sequence *osc_in;
	LV2_Atom_Sequence *osc_out;

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

	strcpy(handle->worker_url, osc_url);

	if(handle->data.stream)
		osc_stream_free(handle->data.stream);
	else
		handle->reconnection_requested = true;

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
		handle->data.stream = osc_stream_new(&handle->loop, handle->worker_url,
			&handle->data.driver, handle);

		handle->reconnection_requested = false;
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
	handle->reconnection_requested = true;
}

// rt
static int
_resolve(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	handle->osc_status = STATE_READY;
	sprintf(handle->osc_error, "");

	handle->needs_flushing = true;
	handle->status_updated = true;

	return 1;
}

// rt
static int
_timeout(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	handle->osc_status = STATE_TIMEDOUT;
	sprintf(handle->osc_error, "");

	handle->status_updated = true;

	return 1;
}

// rt
static int
_error(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	const char *where;
	const char *what;

	const osc_data_t *ptr = buf;
	ptr = osc_get_string(ptr, &where);
	ptr = osc_get_string(ptr, &what);

	handle->osc_status = STATE_ERRORED;
	sprintf(handle->osc_error, "%s (%s)\n", where, what);

	handle->status_updated = true;

	return 1;
}

// rt
static int
_connect(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	handle->osc_status = STATE_CONNECTED;
	sprintf(handle->osc_error, "");

	handle->needs_flushing = true;
	handle->status_updated = true;

	return 1;
}

// rt
static int
_disconnect(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	handle->osc_status = STATE_READY;
	sprintf(handle->osc_error, "");

	handle->status_updated = true;

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

	double frames;
	if(handle->osc_sched)
		frames = handle->osc_sched->osc2frames(handle->osc_sched->handle, time);
	else
		frames = 0.0;

	// add event to list
	list_t *l = tlsf_malloc(handle->tlsf, sizeof(list_t) + size);
	if(l)
	{
		l->frames = frames;
		l->size = size;
		memcpy(l->buf, buf, size);

		handle->list = _list_insert(handle->list, l);
	}
	else if(handle->log)
		lv2_log_trace(&handle->logger, "message pool overflow");
}

static const osc_unroll_inject_t inject = {
	.stamp = _unroll_stamp,
	.message = _unroll_message,
	.bundle = _unroll_bundle
};

static const props_def_t url_def = {
	.property = ETEROJ_URL_URI,
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__String,
	.mode = PROP_MODE_STATIC,
	.maximum.s = 512 // strlen
};

static const props_def_t status_def = {
	.property = ETEROJ_STAT_URI,
	.access = LV2_PATCH__readable,
	.type = LV2_ATOM__Int,
	.mode = PROP_MODE_STATIC
};

static const props_def_t error_def = {
	.property = ETEROJ_ERR_URI,
	.access = LV2_PATCH__readable,
	.type = LV2_ATOM__String,
	.mode = PROP_MODE_STATIC,
	.maximum.s = 512 // strlen
};

static void
_url_change(plughandle_t *handle, const char *url);

static void
_intercept(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	plughandle_t *handle = data;

	if(impl->def == &url_def)
	{
		_url_change(handle, impl->value);
	}
}

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	int i;
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;
	mlock(handle, sizeof(plughandle_t));

	for(i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_WORKER__schedule))
			handle->sched = features[i]->data;
		else if(!strcmp(features[i]->URI, OSC__schedule))
			handle->osc_sched = features[i]->data;
	}

	if(!handle->map)
	{
		fprintf(stderr,
			"%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	if(!handle->sched)
	{
		fprintf(stderr,	
			"%s: Host does not support worker:sched\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	handle->tlsf = tlsf_create_with_pool(handle->mem, POOL_SIZE);

	if(handle->log)
		lv2_log_logger_init(&handle->logger, handle->map, handle->log);
	osc_forge_init(&handle->oforge, handle->map);
	lv2_atom_forge_init(&handle->forge, handle->map);

	// init data
	handle->data.from_worker = varchunk_new(BUF_SIZE, true);
	handle->data.to_worker = varchunk_new(BUF_SIZE, true);
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
	handle->osc_status = STATE_IDLE;
	strcpy(handle->osc_error, "");

	if(!props_init(&handle->props, MAX_NPROPS, descriptor->URI, handle->map, handle))
	{
		free(handle);
		return NULL;
	}

	if(  props_register(&handle->props, &url_def, PROP_EVENT_WRITE, _intercept, &handle->osc_url)
		&& (handle->uris.eteroj_stat = props_register(&handle->props, &status_def, PROP_EVENT_NONE, NULL, &handle->osc_status))
		&& (handle->uris.eteroj_err = props_register(&handle->props, &error_def, PROP_EVENT_NONE, NULL, &handle->osc_error)))
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
			handle->osc_in = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->osc_out = (LV2_Atom_Sequence *)data;
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
	handle->needs_flushing = false;

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
			if(!props_advance(&handle->props, &handle->forge, ev->time.frames, obj, &handle->ref))
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
				else if(handle->log)
					lv2_log_trace(&handle->logger, "output ringbuffer overflow");
			}
		}
	}
	if(handle->osc_in->atom.size > sizeof(LV2_Atom_Sequence_Body))
		handle->needs_flushing = true;

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

	// reschedule scheduled bundles
	if(handle->osc_sched)
	{
		for(list_t *l = handle->list; l; l = l->next)
		{
			uint64_t time = be64toh(*(uint64_t *)(l->buf + 8));

			double frames = handle->osc_sched->osc2frames(handle->osc_sched->handle, time);
			if(frames < 0.0) // we may occasionally get -1 frames events when rescheduling
				l->frames = 0.0;
			else
				l->frames = frames;
		}
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
	for(list_t *l = handle->list; l; )
	{
		if(l->frames < 0.0) // late event
		{
			if(handle->log)
				lv2_log_trace(&handle->logger, "late event: %lf samples", l->frames);
			l->frames = 0.0; // dispatch as early as possible
		}
		else if(l->frames >= nsamples) // not scheduled for this period
		{
			break;
		}

		uint64_t time = be64toh(*(uint64_t *)(l->buf + 8));

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

	if(handle->status_updated)
	{
		props_set(&handle->props, forge, nsamples-1, handle->uris.eteroj_stat);
		props_set(&handle->props, forge, nsamples-1, handle->uris.eteroj_err);

		handle->status_updated = false;
	}

	if(handle->ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->osc_out);
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

	munlock(handle, sizeof(plughandle_t));
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
