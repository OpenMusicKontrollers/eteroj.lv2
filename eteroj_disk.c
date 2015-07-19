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

#include <osc.h>

#include <eteroj.h>
#include <varchunk.h>
#include <lv2_osc.h>

#define BUF_SIZE 0x10000

static const char magic [4] = {'O', 'S', 'C', 0x0};

typedef enum _plugstate_t plugstate_t;
typedef struct _header_t header_t;
typedef struct _event_t event_t;
typedef struct _plughandle_t plughandle_t;

enum _plugstate_t {
	STATE_PLAY					= 0,
	STATE_PAUSE					= 1,
	STATE_RECORD				= 2
};

struct _header_t {
	char magic [4];
	uint32_t sample_rate;
} __attribute__((packed));

struct _event_t {
	uint32_t delta;

	uint32_t size;
	osc_data_t buf [0];
} __attribute__((packed));

struct _plughandle_t {
	LV2_URID_Map *map;
	struct {
		LV2_URID eteroj_path;
	} uris;

	struct {
		int64_t play;
		int64_t record;
	} offset;

	uint32_t sample_rate;
	FILE *osc_file;
	size_t write_ptr;
	size_t read_ptr;
	char osc_path [512];
	volatile int restored;

	osc_forge_t oforge;
	LV2_Atom_Forge forge;

	const LV2_Atom_Sequence *osc_in;
	LV2_Atom_Sequence *osc_out;
	const float *state;
	plugstate_t state_i; 
	
	LV2_Worker_Schedule *sched;
	LV2_Worker_Respond_Function respond;
	LV2_Worker_Respond_Handle target;

	struct {
		varchunk_t *from_worker;
		varchunk_t *to_worker;
		
		int frame_cnt;
		LV2_Atom_Forge_Frame frame [32][2]; // 32 nested bundles should be enough
		
		osc_data_t *buf;
		osc_data_t *ptr;
		osc_data_t *end;

		int bndl_cnt;
		osc_data_t *bndl [32]; // 32 nested bundles should be enough
	} data;
};

static inline void
_header_ntoh(header_t *header)
{
	header->sample_rate = be32toh(header->sample_rate);
}
#define _header_hton _header_ntoh

static inline void
_event_ntoh(event_t *event)
{
	event->delta = be32toh(event->delta);
	event->size = be32toh(event->size);
}
#define _event_hton _event_ntoh

static LV2_State_Status
_state_save(LV2_Handle instance, LV2_State_Store_Function store,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = (plughandle_t *)instance;

	const LV2_State_Map_Path *map_path = NULL;

	for(int i=0; features[i]; i++)
		if(!strcmp(features[i]->URI, LV2_STATE__mapPath))
			map_path = features[i]->data;

	if(!map_path)
	{
		fprintf(stderr, "_state_save: LV2_STATE__mapPath not supported.");
		return LV2_STATE_ERR_UNKNOWN;
	}

	const char *abstract = map_path->abstract_path(map_path->handle, handle->osc_path);

	return store(
		state,
		handle->uris.eteroj_path,
		abstract,
		strlen(abstract) + 1,
		handle->forge.Path,
		LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
}

static LV2_State_Status
_state_restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = (plughandle_t *)instance;

	const LV2_State_Map_Path *map_path = NULL;

	for(int i=0; features[i]; i++)
		if(!strcmp(features[i]->URI, LV2_STATE__mapPath))
			map_path = features[i]->data;

	if(!map_path)
	{
		fprintf(stderr, "_state_restore: LV2_STATE__mapPath not supported.");
		return LV2_STATE_ERR_UNKNOWN;
	}

	size_t size;
	uint32_t type;
	uint32_t flags2;
	const char *osc_path = retrieve(
		state,
		handle->uris.eteroj_path,
		&size,
		&type,
		&flags2
	);

	// check type
	if(osc_path && (type != handle->forge.Path) )
		return LV2_STATE_ERR_BAD_TYPE;

	if(!osc_path)
		osc_path = "dump.osc";
		
	const char *absolute = map_path->absolute_path(map_path->handle, osc_path);

	strcpy(handle->osc_path, absolute);
	handle->restored = 1;

	return LV2_STATE_SUCCESS;
}

static const LV2_State_Interface state_iface = {
	.save = _state_save,
	.restore = _state_restore
};

// non-rt
static int
_worker_api_path(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;
	const osc_data_t *ptr = buf;

	printf("_worker_api_path\n");

	const char *osc_path;
	ptr = osc_get_string(ptr, &osc_path);

	strcpy(handle->osc_path, osc_path);
	handle->read_ptr = sizeof(header_t);
	handle->write_ptr = sizeof(header_t);

	if(handle->osc_file)
	{
		fflush(handle->osc_file);
		fclose(handle->osc_file);
	}
	handle->osc_file = fopen(handle->osc_path, "a+b");
	if(!handle->osc_file)
		return 1;

	fseek(handle->osc_file, 0, SEEK_SET);
		
	header_t header;
	if(fread(&header, sizeof(header_t), 1, handle->osc_file) == 1)
	{
		printf("reading header\n;");
		_header_ntoh(&header);
		assert(!strcmp(header.magic, magic));
		assert(header.sample_rate == handle->sample_rate);
	}
	else
	{
		printf("writing header\n;");
		strcpy(header.magic, magic);
		header.sample_rate = handle->sample_rate;
		_header_hton(&header);
		assert(fwrite(&header, sizeof(header_t), 1, handle->osc_file) == 1);
		fflush(handle->osc_file);
	}

	return 1;
}

// non-rt
static int
_worker_api_record(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	if(!handle->osc_file)
		return 1;

	//printf("_worker_api_record\n");
		
	fseek(handle->osc_file, handle->write_ptr, SEEK_SET);

	event_t *ev;
	size_t len;
	while((ev = (event_t *)varchunk_read_request(handle->data.to_worker, &len)))
	{
		_event_hton(ev);

		if(handle->write_ptr == sizeof(header_t)) // first event to write
			ev->delta = 0;

		if(fwrite(ev, len, 1, handle->osc_file) != 1)
		{
			_event_ntoh(ev); // revert byteswap, event was not written to disk
			break;
		}

		varchunk_read_advance(handle->data.to_worker);
	}

	fflush(handle->osc_file);
	handle->write_ptr = ftell(handle->osc_file);

	return 1;
}

// non-rt
static int
_worker_api_play(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	if(!handle->osc_file)
		return 1;

	//printf("_worker_api_play\n");
		
	fseek(handle->osc_file, handle->read_ptr, SEEK_SET);

	// read as many events as possible from disk and add to ringbuffer
	while(!feof(handle->osc_file))
	{
		event_t ev;
		if(fread(&ev, sizeof(event_t), 1, handle->osc_file) != 1)
			break;
		_event_ntoh(&ev);

		void *ptr;
		const size_t len = sizeof(event_t) + ev.size;
		if((ptr = varchunk_write_request(handle->data.from_worker, len)))
		{
			memcpy(ptr, &ev, sizeof(event_t));
			if(fread(ptr + sizeof(event_t), ev.size, 1, handle->osc_file) != 1)
				break;

			varchunk_write_advance(handle->data.from_worker, len);
		}
		else
			break;
	}

	handle->read_ptr = ftell(handle->osc_file);

	return 1;
}

// non-rt
static int
_worker_api_pause(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	if(!handle->osc_file)
		return 1;

	//printf("_worker_api_pause\n");
	
	handle->read_ptr = sizeof(header_t);
	handle->write_ptr = sizeof(header_t);

	return 1;
}

// non-rt
static int
_worker_api_clear(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	if(!handle->osc_file)
		return 1;

	printf("_worker_api_clear\n");

	handle->osc_file = freopen(NULL, "w+b", handle->osc_file);
	if(handle->osc_file)
	{
		header_t header;
		printf("writing header\n;");
		strcpy(header.magic, magic);
		header.sample_rate = handle->sample_rate;
		_header_hton(&header);
		assert(fwrite(&header, sizeof(header_t), 1, handle->osc_file) == 1);
		fflush(handle->osc_file);
	}

	handle->read_ptr = sizeof(header_t);
	handle->write_ptr = sizeof(header_t);

	return 1;
}

// non-rt
static int
_worker_api_rewind(osc_time_t timestamp, const char *path, const char *fmt,
	const osc_data_t *buf, size_t size, void *data)
{
	plughandle_t *handle = data;

	if(!handle->osc_file)
		return 1;

	printf("_worker_api_rewind\n");

	handle->read_ptr = sizeof(header_t);

	return 1;
}

static const char play_msg [] = "/eteroj/play\0\0\0\0,\0\0\0";
static const char pause_msg [] = "/eteroj/pause\0\0\0,\0\0\0";
static const char record_msg [] = "/eteroj/record\0\0,\0\0\0";
static const char clear_msg [] = "/eteroj/clear\0\0\0,\0\0\0";
static const char rewind_msg [] = "/eteroj/rewind\0\0,\0\0\0";

static const osc_method_t worker_api [] = {
	{"/eteroj/play", "", _worker_api_play},
	{"/eteroj/pause", "", _worker_api_pause},
	{"/eteroj/record", "", _worker_api_record},
	{"/eteroj/clear", "", _worker_api_clear},
	{"/eteroj/rewind", "", _worker_api_rewind},
	{"/eteroj/path", "s", _worker_api_path},

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

	handle->respond = respond;
	handle->target = target;

	osc_dispatch_method(body, size, worker_api, NULL, NULL, handle);

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

// rt
static void
_bundle_in(osc_time_t timestamp, void *data)
{
	plughandle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge;

	if(!handle->data.frame_cnt) // no nested bundle
		lv2_atom_forge_frame_time(forge, handle->offset.play);

	osc_forge_bundle_push(&handle->oforge, forge,
		handle->data.frame[handle->data.frame_cnt++], timestamp);
}

static void
_bundle_out(osc_time_t timestamp, void *data)
{
	plughandle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge;

	osc_forge_bundle_pop(&handle->oforge, forge,
		handle->data.frame[--handle->data.frame_cnt]);
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
		lv2_atom_forge_frame_time(forge, handle->offset.play);

	osc_forge_message_push(&handle->oforge, forge, frame, path, fmt);

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
				osc_forge_char(&handle->oforge, forge, c);
				break;
			}
			case 'm':
			{
				const uint8_t *m;
				ptr = osc_get_midi(ptr, &m);
				osc_forge_midi(&handle->oforge, forge, 3, m + 1); // skip port byte
				break;
			}
		}

	osc_forge_message_pop(&handle->oforge, forge, frame);

	return 1;
}

static const osc_method_t methods [] = {
	{NULL, NULL, _message},

	{NULL, NULL, NULL}
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	int i;
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	handle->sample_rate = rate;
	handle->state_i = STATE_PAUSE;

	for(i=0; features[i]; i++)
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_WORKER__schedule))
			handle->sched = features[i]->data;

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

	handle->uris.eteroj_path = handle->map->map(handle->map->handle,
		ETEROJ_PATH_URI);

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

	handle->osc_path[0] = '\0';

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
			handle->state = (const float *)data;
			break;
		default:
			break;
	}
}

static void
activate(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	handle->offset.play = 0;
	handle->offset.record = 0;
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

// rt
static void
_path_change(plughandle_t *handle, const char *path)
{
	osc_data_t buf [512];
	osc_data_t *ptr = buf;
	osc_data_t *end = buf + 512;

	ptr = osc_set_path(ptr, end, "/eteroj/path");
	ptr = osc_set_fmt(ptr, end, "s");
	ptr = osc_set_string(ptr, end, path);

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

	plugstate_t state = floor(*handle->state); 

	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Frame frame;
	uint32_t capacity;

	// prepare sequence forges
	capacity = handle->osc_out->atom.size;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->osc_out, capacity);
	lv2_atom_forge_sequence_head(forge, &frame, 0);

	// write outgoing data
	if(state == STATE_RECORD)
	{
		LV2_ATOM_SEQUENCE_FOREACH(handle->osc_in, ev)
		{
			const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;

			if(obj->atom.type == forge->Object)
			{
				event_t *oev;
				size_t reserve = sizeof(event_t) + obj->atom.size;
				if((oev= varchunk_write_request(handle->data.to_worker, reserve)))
				{
					handle->data.buf = oev->buf;
					handle->data.ptr = handle->data.buf;
					handle->data.end = handle->data.buf + reserve - sizeof(event_t);

					osc_atom_event_unroll(&handle->oforge, obj, _bundle_push_cb,
						_bundle_pop_cb, _message_cb, handle);

					size_t size = handle->data.ptr
						? handle->data.ptr - handle->data.buf
						: 0;

					if(size)
					{
						handle->offset.record += ev->time.frames;
						if(handle->offset.record < 0)
							handle->offset.record = 0;

						oev->delta = handle->offset.record;
						oev->size = size;

						// offset is always relative to sample #0 of last events period 
						handle->offset.record = -ev->time.frames;

						varchunk_write_advance(handle->data.to_worker, sizeof(event_t) + size);
					}
				}
			}
		}

		if(state != handle->state_i)
		{
			LV2_Worker_Status status = handle->sched->schedule_work(
				handle->sched->handle, sizeof(clear_msg), clear_msg);
			//TODO check status
		}

		if(handle->osc_in->atom.size > sizeof(LV2_Atom_Sequence_Body))
		{

			LV2_Worker_Status status = handle->sched->schedule_work(
				handle->sched->handle, sizeof(record_msg), record_msg);
			//TODO check status
		}
	}
	else if(state == STATE_PLAY)
	{
		// read incoming data
		const event_t *ev;
		size_t size;
		while((ev = varchunk_read_request(handle->data.from_worker, &size)))
		{
			if(handle->offset.play + ev->delta >= nsamples)
				break; // event not part of this period

			handle->offset.play += ev->delta;
			if(handle->offset.play < 0)
				handle->offset.play = 0;

			osc_dispatch_method(ev->buf, ev->size, methods, _bundle_in, _bundle_out, handle);

			varchunk_read_advance(handle->data.from_worker);
		}

		if(state != handle->state_i)
		{
			LV2_Worker_Status status = handle->sched->schedule_work(
				handle->sched->handle, sizeof(rewind_msg), rewind_msg);
			//TODO check status
		}

		LV2_Worker_Status status = handle->sched->schedule_work(
			handle->sched->handle, sizeof(play_msg), play_msg);
		//TODO check status
	}
	else if(state == STATE_PAUSE)
	{
		LV2_Worker_Status status = handle->sched->schedule_work(
			handle->sched->handle, sizeof(pause_msg), pause_msg);
		//TODO check status
	}
	handle->state_i = state;

	lv2_atom_forge_pop(forge, &frame);

	handle->offset.play -= nsamples;
	handle->offset.record += nsamples;
	
	if(handle->restored && handle->osc_path[0])
	{
		_path_change(handle, handle->osc_path);

		handle->restored = 0;
	}
}

static void
deactivate(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	if(handle->osc_file)
	{
		fflush(handle->osc_file);
		fclose(handle->osc_file);
	}
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

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

const LV2_Descriptor eteroj_disk = {
	.URI						= ETEROJ_DISK_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
