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
#include <varchunk.h>
#include <osc.lv2/reader.h>
#include <osc.lv2/writer.h>
#include <osc.lv2/forge.h>
#include <osc.lv2/stream.h>
#include <props.h>
#include <tlsf.h>

#define POOL_SIZE 0x800000 // 8M
#define BUF_SIZE 0x100000 // 1M
#define MAX_NPROPS 3
#define STR_LEN 512

typedef enum _plugenum_t plugenum_t;
typedef struct _plugstate_t plugstate_t;
typedef struct _list_t list_t;
typedef struct _plughandle_t plughandle_t;

enum _plugenum_t {
	STATUS_IDLE					= 0,
	STATUS_READY				= 1,
	STATUS_TIMEDOUT			= 2,
	STATUS_ERRORED			= 3,
	STATUS_CONNECTED		= 4
};

struct _list_t {
	list_t *next;
	double frames;

	size_t size;
	uint8_t buf [0];
};

struct _plugstate_t {
	char osc_url [STR_LEN];
	char osc_status [STR_LEN];
	char osc_error [STR_LEN];
};

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_URID_Unmap *unmap;
	LV2_Worker_Schedule *sched;
	struct {
		LV2_URID state_default;
		LV2_URID subject;

		LV2_URID eteroj_stat;
		LV2_URID eteroj_err;
	} uris;

	PROPS_T(props, MAX_NPROPS);
	LV2_OSC_URID osc_urid;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	volatile bool status_updated;
	bool rolling;

	plugstate_t state;
	plugstate_t stash;

	LV2_Log_Log *log;
	LV2_Log_Logger logger;

	const LV2_Atom_Sequence *osc_in;
	LV2_Atom_Sequence *osc_out;

	LV2_OSC_Schedule *osc_sched;
	list_t *list;
	uint8_t mem [POOL_SIZE];
	tlsf_t tlsf;

	struct {
		LV2_OSC_Driver driver;
		LV2_OSC_Stream stream;
		varchunk_t *from_worker;
		varchunk_t *to_worker;
		varchunk_t *to_thread;
	} data;
};

static const char *status [] = {
	[STATUS_IDLE] = "idle",
	[STATUS_READY] = "ready",
	[STATUS_TIMEDOUT] = "timedout",
	[STATUS_ERRORED] = "errored",
	[STATUS_CONNECTED] = "connected"
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

	return props_save(&handle->props, store, state, flags, features);
}

static LV2_State_Status
_state_restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = (plughandle_t *)instance;

	return props_restore(&handle->props, retrieve, state, flags, features);
}

static const LV2_State_Interface state_iface = {
	.save = _state_save,
	.restore = _state_restore
};

// non-rt
static void *
_data_recv_req(void *data, size_t size, size_t *max)
{
	plughandle_t *handle = data;

	return varchunk_write_request_max(handle->data.from_worker, size, max);
}

// non-rt
static void
_data_recv_adv(void *data, size_t written)
{
	plughandle_t *handle = data;

	varchunk_write_advance(handle->data.from_worker, written);
}

// non-rt
static const void *
_data_send_req(void *data, size_t *len)
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

// rt
static void
_url_change(plughandle_t *handle, const char *url)
{
	LV2_OSC_Writer writer;
	uint8_t buf [STR_LEN];
	lv2_osc_writer_initialize(&writer, buf, STR_LEN);
	lv2_osc_writer_message_vararg(&writer, "/eteroj/url", "s", url);
	size_t size;
	lv2_osc_writer_finalize(&writer, &size);

	if(size)
	{
		uint8_t *dst;
		if((dst = varchunk_write_request(handle->data.to_thread, size))) //FIXME use request_max
		{
			memcpy(dst, buf, size);
			varchunk_write_advance(handle->data.to_thread, size);
		}
	}
}

static void
_intercept(void *data, int64_t frames, props_impl_t *impl)
{
	plughandle_t *handle = data;

	_url_change(handle, impl->value.body);
}

static const props_def_t defs [MAX_NPROPS] = {
	{
		.property = ETEROJ_URL_URI,
		.offset = offsetof(plugstate_t, osc_url),
		.type = LV2_ATOM__String,
		.max_size = STR_LEN,
		.event_cb = _intercept
	},
	{
		.property = ETEROJ_STAT_URI,
		.offset = offsetof(plugstate_t, osc_status),
		.access = LV2_PATCH__readable,
		.type = LV2_ATOM__String,
		.max_size = STR_LEN
	},
	{
		.property = ETEROJ_ERR_URI,
		.offset = offsetof(plugstate_t, osc_error),
		.access = LV2_PATCH__readable,
		.type = LV2_ATOM__String,
		.max_size = STR_LEN,
	}
};

static inline void
_set_status(plughandle_t *handle, int stat)
{
	props_impl_t *impl = _props_bsearch(&handle->props, handle->uris.eteroj_stat);
	if(impl)
	{
		const char *str = status[stat];
		_props_impl_set(&handle->props, impl, handle->forge.String, strlen(str) + 1, str);
	}
}

static inline void
_set_error(plughandle_t *handle, const char *err)
{
	props_impl_t *impl = _props_bsearch(&handle->props, handle->uris.eteroj_err);
	if(impl)
	{
		_props_impl_set(&handle->props, impl, handle->forge.String, strlen(err) + 1, err);
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
		else if(!strcmp(features[i]->URI, LV2_URID__unmap))
			handle->unmap = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_OSC__schedule))
			handle->osc_sched = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_WORKER__schedule))
			handle->sched= features[i]->data;
	}

	if(!handle->map || !handle->unmap)
	{
		fprintf(stderr,
			"%s: Host does not support urid:(un)map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	if(!handle->sched)
	{
		fprintf(stderr, "%s: Host does not support work:schedule\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	handle->tlsf = tlsf_create_with_pool(handle->mem, POOL_SIZE);

	if(handle->log)
		lv2_log_logger_init(&handle->logger, handle->map, handle->log);
	lv2_osc_urid_init(&handle->osc_urid, handle->map);
	lv2_atom_forge_init(&handle->forge, handle->map);

	// init data
	handle->data.from_worker = varchunk_new(BUF_SIZE, true);
	handle->data.to_worker = varchunk_new(BUF_SIZE, true);
	handle->data.to_thread = varchunk_new(BUF_SIZE, true);
	if(!handle->data.from_worker || !handle->data.to_worker || !handle->data.to_thread)
	{
		free(handle);
		return NULL;
	}

	handle->data.driver.write_req = _data_recv_req;
	handle->data.driver.write_adv = _data_recv_adv;
	handle->data.driver.read_req = _data_send_req;
	handle->data.driver.read_adv = _data_send_adv;

	if(!props_init(&handle->props, descriptor->URI,
		defs, MAX_NPROPS, &handle->state, &handle->stash,
		handle->map, handle))
	{
		free(handle);
		return NULL;
	}

	handle->uris.eteroj_stat = props_map(&handle->props, ETEROJ_STAT_URI);
	handle->uris.eteroj_err = props_map(&handle->props, ETEROJ_ERR_URI);

	_set_status(handle, STATUS_IDLE);
	_set_error(handle, "");

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

static inline void
_parse(plughandle_t *handle, double frames, const uint8_t *buf, size_t size)
{
	LV2_OSC_Reader reader;
	LV2_OSC_Arg arg;
	lv2_osc_reader_initialize(&reader, buf, size);
	lv2_osc_reader_arg_begin(&reader, &arg, size);

#if 0
	if(!strcmp(arg.path, "/stream/resolve"))
	{
		_set_status(handle, STATUS_READY);
		_set_error(handle, "");

		handle->status_updated = true;
	}
	else if(!strcmp(arg.path, "/stream/timeout"))
	{
		_set_status(handle, STATUS_TIMEDOUT);
		_set_error(handle, "");

		handle->status_updated = true;
	}
	else if(!strcmp(arg.path, "/stream/error"))
	{
		const char *where = arg.s;
		const char *what = lv2_osc_reader_arg_next(&reader, &arg)->s;

		char err [STR_LEN];
		snprintf(err, STR_LEN, "%s (%s)\n", where, what);
		_set_status(handle, STATUS_ERRORED);
		_set_error(handle, err);

		handle->status_updated = true;
	}
	else if(!strcmp(arg.path, "/stream/connect"))
	{
		_set_status(handle, STATUS_CONNECTED);
		_set_error(handle, "");

		handle->status_updated = true;
	}
	else if(!strcmp(arg.path, "/stream/disconnect"))
	{
		_set_status(handle, STATUS_READY);
		_set_error(handle, "");

		handle->status_updated = true;
	}
	else // general message
#endif
	{
		if(handle->ref)
			handle->ref = lv2_atom_forge_frame_time(&handle->forge, frames);
		if(handle->ref)
			handle->ref = lv2_osc_forge_packet(&handle->forge, &handle->osc_urid, handle->map,
				buf, size);
	}
}

static inline void 
_unroll(plughandle_t *handle, const uint8_t *buf, size_t size)
{
	LV2_OSC_Reader reader;
	lv2_osc_reader_initialize(&reader, buf, size);

	if(lv2_osc_reader_is_bundle(&reader))
	{
		LV2_OSC_Item *itm = OSC_READER_BUNDLE_BEGIN(&reader, size);

		if(itm->timetag == LV2_OSC_IMMEDIATE) // immediate dispatch
		{
			_parse(handle, 0.0, buf, size);
		}
		else // schedule dispatch
		{
			double frames;
			if(handle->osc_sched)
				frames = handle->osc_sched->osc2frames(handle->osc_sched->handle, itm->timetag);
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
	}
	else if(lv2_osc_reader_is_message(&reader)) // immediate dispatch
	{
		_parse(handle, 0.0, buf, size);
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Frame frame;
	uint32_t capacity;

	// prepare sequence forges
	capacity = handle->osc_out->atom.size;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->osc_out, capacity);
	handle->ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

	props_idle(&handle->props, &handle->forge, 0, &handle->ref);

	// write outgoing data
	LV2_ATOM_SEQUENCE_FOREACH(handle->osc_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;

		if(obj->atom.type == forge->Object)
		{
			if(  !props_advance(&handle->props, &handle->forge, ev->time.frames, obj, &handle->ref)
				&& lv2_osc_is_message_or_bundle_type(&handle->osc_urid, obj->body.otype) )
			{
				uint8_t *dst;
				size_t reserve = obj->atom.size;
				if((dst = varchunk_write_request(handle->data.to_worker, reserve)))
				{
					LV2_OSC_Writer writer;
					lv2_osc_writer_initialize(&writer, dst, reserve);
					lv2_osc_writer_packet(&writer, &handle->osc_urid, handle->unmap, obj->atom.size, &obj->body);
					size_t written;
					lv2_osc_writer_finalize(&writer, &written);

					if(written)
					{
						varchunk_write_advance(handle->data.to_worker, written);
					}
				}
				else if(handle->log)
					lv2_log_trace(&handle->logger, "output ringbuffer overflow");
			}
		}
	}

	// wake worker once per period
	const uint32_t dummy = 0;
	handle->sched->schedule_work(handle->sched->handle, sizeof(dummy), &dummy);

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
	const uint8_t *ptr;
	size_t size;
	while((ptr = varchunk_read_request(handle->data.from_worker, &size)))
	{
		_unroll(handle, ptr, size);

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

		_parse(handle, l->frames, l->buf, l->size);

		list_t *l0 = l;
		l = l->next;
		handle->list = l;
		tlsf_free(handle->tlsf, l0);
	}

	if(handle->status_updated)
	{
		props_set(&handle->props, forge, nsamples-1, handle->uris.eteroj_stat, &handle->ref);
		props_set(&handle->props, forge, nsamples-1, handle->uris.eteroj_err, &handle->ref);

		handle->status_updated = false;
	}

	if(handle->ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->osc_out);
}

static inline void
_activate(plughandle_t *handle, const char *osc_url)
{
	if(!handle->rolling && osc_url)
	{
		handle->rolling = lv2_osc_stream_init(&handle->data.stream, osc_url,
			&handle->data.driver, handle) == 0;
		//printf("url: %s %i\n", osc_url, handle->rolling);
	}
}

static void
activate(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	_activate(handle, NULL);
}

static inline void
_deactivate(plughandle_t *handle)
{
	if(handle->rolling)
	{
		lv2_osc_stream_deinit(&handle->data.stream);
		handle->rolling = false;
	}
}

static void
deactivate(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	_deactivate(handle);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	tlsf_destroy(handle->tlsf);
	varchunk_free(handle->data.from_worker);
	varchunk_free(handle->data.to_worker);
	varchunk_free(handle->data.to_thread);

	munlock(handle, sizeof(plughandle_t));
	free(handle);
}

// non-rt thread
static LV2_Worker_Status
_work(LV2_Handle instance,
	LV2_Worker_Respond_Function respond,
	LV2_Worker_Respond_Handle target,
	uint32_t _size,
	const void *_body)
{
	plughandle_t *handle = instance;

	const char *osc_url = NULL;

	size_t size;
	const uint8_t *body;
	while((body = varchunk_read_request(handle->data.to_thread, &size)))
	{
		LV2_OSC_Reader reader;
		LV2_OSC_Arg arg;
		lv2_osc_reader_initialize(&reader, body, size);
		lv2_osc_reader_arg_begin(&reader, &arg, size);

		if(!strcmp(arg.path, "/eteroj/url"))
		{
			osc_url = arg.s;

			_deactivate(handle);
		}

		varchunk_read_advance(handle->data.to_thread);
	}

	_activate(handle, osc_url);

	if(handle->rolling)
		lv2_osc_stream_run(&handle->data.stream);

	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_work_response(LV2_Handle instance, uint32_t size, const void *body)
{
	// do nothing

	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_end_run(LV2_Handle instance)
{
	// do nothing

	return LV2_WORKER_SUCCESS;
}

static const LV2_Worker_Interface work_iface = {
	.work = _work,
	.work_response = _work_response,
	.end_run = _end_run
};

static const void*
extension_data(const char* uri)
{
	if(!strcmp(uri, LV2_WORKER__interface))
		return &work_iface;
	else if(!strcmp(uri, LV2_STATE__interface))
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
