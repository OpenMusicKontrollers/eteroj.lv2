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
#include <unistd.h>
#include <math.h>

#include <eteroj.h>
#include <timely.h>
#include <varchunk.h>
#include <lv2_osc.h>
#include <props.h>
#include <osc.h>

#define BUF_SIZE 0x10000
#define DEFAULT_FILE_NAME "seq.osc"
#define MAX_NPROPS 2

typedef enum _jobtype_t jobtype_t;
typedef struct _job_t job_t;
typedef struct _plughandle_t plughandle_t;

enum _jobtype_t {
	JOB_PLAY				= 0,
	JOB_RECORD			= 1,
	JOB_SEEK				= 2,
	JOB_OPEN_PLAY		= 3,
	JOB_OPEN_RECORD	= 4,
	JOB_DRAIN				= 5
};

struct _job_t {
	jobtype_t type;
	union {
		double beats;
	};
};

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_Atom_Forge forge;
	osc_forge_t oforge;
	struct {
		LV2_URID eteroj_drain;
	} uris;

	timely_t timely;
	LV2_Worker_Schedule *sched;

	PROPS_T(props, MAX_NPROPS);

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;
	int32_t record;

	LV2_Log_Log *log;
	LV2_Log_Logger logger;

	double beats_period;
	double beats_upper;

	bool rolling;
	int draining;

	char path [1024];
	FILE *io;

	double seek_beats;

	varchunk_t *to_disk;
	varchunk_t *from_disk;

	int frame_cnt;
	LV2_Atom_Forge_Frame frame [32][2]; // 32 nested bundles should be enough
	int64_t offset;

	osc_data_t *ptr;
	osc_data_t *end;

	int bndl_cnt;
	osc_data_t *bndl [32]; // 32 nested bundles should be enough
};

static const props_def_t record_def = {
	.property = ETEROJ_DISK_RECORD_URI,
	.access = LV2_PATCH__writable,
	.type = LV2_ATOM__Bool,
	.mode = PROP_MODE_STATIC
};

static const props_def_t path_def = {
	.property = ETEROJ_DISK_PATH_URI,
	.access = LV2_PATCH__readable,
	.type = LV2_ATOM__Path,
	.mode = PROP_MODE_STATIC,
	.maximum.i = 1024
};

static inline void
_event_ntoh(LV2_Atom_Event *ev)
{
	ev->time.frames = be64toh(ev->time.frames);
	ev->body.type = be32toh(ev->body.type);
	ev->body.size = be32toh(ev->body.size);
}
#define _event_hton _event_ntoh

// rt
static void
_bundle_in(osc_time_t timestamp, void *data)
{
	plughandle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge;

	if(!handle->frame_cnt) // no nested bundle
		lv2_atom_forge_frame_time(forge, handle->offset);

	osc_forge_bundle_push(&handle->oforge, forge,
		handle->frame[handle->frame_cnt++], timestamp);
}

static void
_bundle_out(osc_time_t timestamp, void *data)
{
	plughandle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge;

	osc_forge_bundle_pop(&handle->oforge, forge,
		handle->frame[--handle->frame_cnt]);
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

	if(!handle->frame_cnt) // message not in a bundle
		lv2_atom_forge_frame_time(forge, handle->offset);

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

// rt
static void
_bundle_push_cb(uint64_t timestamp, void *data)
{
	plughandle_t *handle = data;

	handle->ptr = osc_start_bundle(handle->ptr, handle->end,
		timestamp, &handle->bndl[handle->bndl_cnt++]);
}

// rt
static void
_bundle_pop_cb(void *data)
{
	plughandle_t *handle = data;

	handle->ptr = osc_end_bundle(handle->ptr, handle->end,
		handle->bndl[--handle->bndl_cnt]);
}

// rt
static void
_message_cb(const char *path, const char *fmt,
	const LV2_Atom_Tuple *body, void *data)
{
	plughandle_t *handle = data;

	osc_data_t *ptr = handle->ptr;
	const osc_data_t *end = handle->end;

	osc_data_t *itm;
	if(handle->bndl_cnt)
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

	if(handle->bndl_cnt)
		ptr = osc_end_bundle_item(ptr, end, itm);

	handle->ptr = ptr;
}

static inline LV2_Worker_Status
_trigger_job(plughandle_t *handle, const job_t *job)
{
	return handle->sched->schedule_work(handle->sched->handle, sizeof(job_t), job);
}

static inline LV2_Worker_Status
_trigger_record(plughandle_t *handle)
{
	const job_t job = {
		.type = JOB_RECORD
	};

	return _trigger_job(handle, &job);
}

static inline LV2_Worker_Status
_trigger_play(plughandle_t *handle)
{
	const job_t job = {
		.type = JOB_PLAY
	};

	return _trigger_job(handle, &job);
}

static inline LV2_Worker_Status
_trigger_seek(plughandle_t *handle, double beats)
{
	const job_t job = {
		.type = JOB_SEEK,
		.beats = beats
	};

	return _trigger_job(handle, &job);
}

static inline LV2_Worker_Status
_trigger_open_play(plughandle_t *handle)
{
	const job_t job = {
		.type = JOB_OPEN_PLAY
	};

	return _trigger_job(handle, &job);
}

static inline LV2_Worker_Status
_trigger_open_record(plughandle_t *handle)
{
	const job_t job = {
		.type = JOB_OPEN_RECORD
	};

	return _trigger_job(handle, &job);
}

static inline LV2_Worker_Status
_trigger_drain(plughandle_t *handle)
{
	const job_t job = {
		.type = JOB_DRAIN
	};

	return _trigger_job(handle, &job);
}

static inline void
_play(plughandle_t *handle, int64_t to, uint32_t capacity)
{
	const LV2_Atom_Event *src;
	size_t len;
	while((src = varchunk_read_request(handle->from_disk, &len)))
	{
		if(handle->draining > 0)
		{
			if(src->body.type == handle->uris.eteroj_drain)
			{
				//fprintf(stderr, "_play: draining %i\n", handle->draining);
				handle->draining -= 1;
			}

			varchunk_read_advance(handle->from_disk);
			continue;
		}

		//fprintf(stderr, "_play: %lf %lf\n", src->time.beats, handle->beats_upper);

		if(src->time.beats >= handle->beats_upper)
			break; // event not part of this region

		handle->offset = round((src->time.beats - handle->beats_period) * TIMELY_FRAMES_PER_BEAT(&handle->timely));
		if(handle->offset < 0)
		{
			if(handle->log)
				lv2_log_trace(&handle->logger, "_play: event late %li", handle->offset);

			handle->offset = 0;
		}

		osc_dispatch_method(LV2_ATOM_BODY_CONST(&src->body), src->body.size,
			methods, _bundle_in, _bundle_out, handle);

		varchunk_read_advance(handle->from_disk);
	}
}

static inline void
_rec(plughandle_t *handle, const LV2_Atom_Event *src)
{
	// add event to ring buffer
	LV2_Atom_Event *dst;
	if((dst = varchunk_write_request(handle->to_disk, sizeof(LV2_Atom_Event) + src->body.size)))
	{
		osc_data_t *base = LV2_ATOM_BODY(&dst->body);
		handle->ptr = base;
		handle->end = base + src->body.size;

		osc_atom_event_unroll(&handle->oforge, (const LV2_Atom_Object *)&src->body,
			_bundle_push_cb, _bundle_pop_cb, _message_cb, handle);
	
		uint32_t size = handle->ptr
			? handle->ptr - base
			: 0;

		if(size)
		{
			dst->time.beats = handle->beats_upper;
			dst->body.type = 0; //XXX
			dst->body.size = size;

			varchunk_write_advance(handle->to_disk, sizeof(LV2_Atom_Event) + size);
		}
	} 
	else if(handle->log)
		lv2_log_trace(&handle->logger, "_rec: ringbuffer overflow");
}

static inline void
_reposition_play(plughandle_t *handle, double beats)
{
	handle->draining += 1;
	_trigger_drain(handle);
	_trigger_seek(handle, beats);
}

static inline void
_reposition_rec(plughandle_t *handle, double beats)
{
	_trigger_seek(handle, beats);
}

static inline double
_beats(timely_t *timely)
{
	double beats = TIMELY_BAR(timely)
		* TIMELY_BEATS_PER_BAR(timely)
		+ TIMELY_BAR_BEAT(timely);

	return beats;
}

static void
_cb(timely_t *timely, int64_t frames, LV2_URID type, void *data)
{
	plughandle_t *handle = data;

	if(type == TIMELY_URI_SPEED(timely))
	{
		handle->rolling = TIMELY_SPEED(timely) > 0.f ? true : false;
	}
	else if(type == TIMELY_URI_BAR_BEAT(timely))
	{
		double beats = _beats(&handle->timely);

		if(handle->record)
			_reposition_rec(handle, beats);
		else
			_reposition_play(handle, beats);
	}
}

static void
_intercept(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	plughandle_t *handle = data;

	if(impl->def == &record_def)
	{
		if(handle->record)
			_trigger_open_record(handle);
		else
			_trigger_open_play(handle);
	}
}

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;
	mlock(handle, sizeof(plughandle_t));

	const LV2_State_Make_Path *make_path = NULL;

	for(unsigned i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_WORKER__schedule))
			handle->sched = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_STATE__makePath))
			make_path = features[i]->data;
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
	if(!make_path)
	{
		fprintf(stderr,
			"%s: Host does not support state:makePath\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	timely_mask_t mask = TIMELY_MASK_BAR_BEAT
		//| TIMELY_MASK_BAR
		| TIMELY_MASK_BEAT_UNIT
		| TIMELY_MASK_BEATS_PER_BAR
		| TIMELY_MASK_BEATS_PER_MINUTE
		| TIMELY_MASK_FRAMES_PER_SECOND
		| TIMELY_MASK_SPEED;
	timely_init(&handle->timely, handle->map, rate, mask, _cb, handle);
	if(handle->log)
		lv2_log_logger_init(&handle->logger, handle->map, handle->log);
	lv2_atom_forge_init(&handle->forge, handle->map);
	osc_forge_init(&handle->oforge, handle->map);

	char *tmp = make_path->path(make_path->handle, DEFAULT_FILE_NAME);
	if(tmp)
	{
		strncpy(handle->path, tmp, 1024);
		free(tmp);
	
		if(access(handle->path, F_OK)) // does not yet exist
		{
			fprintf(stderr, "touching file\n");
			handle->io = fopen(handle->path, "w");
			if(handle->io)
			{
				fclose(handle->io);
				handle->io = NULL;
			}
			else
				fprintf(stderr, "failed to touch file\n");
		}
	}
	
	handle->uris.eteroj_drain = handle->map->map(handle->map->handle, ETEROJ_DRAIN_URI);

	handle->to_disk = varchunk_new(BUF_SIZE, true);
	handle->from_disk = varchunk_new(BUF_SIZE, true);

	if(!props_init(&handle->props, MAX_NPROPS, descriptor->URI, handle->map, handle))
	{
		free(handle);
		return NULL;
	}

	if(  props_register(&handle->props, &record_def, PROP_EVENT_WRITE, _intercept, &handle->record)
		&& props_register(&handle->props, &path_def, PROP_EVENT_NONE, NULL, &handle->path)) //FIXME
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
activate(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	handle->rolling = false;
	handle->seek_beats = 0.0;

	handle->beats_period = 0.0;
	handle->beats_upper = 0.0;
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;
	uint32_t capacity = handle->event_out->atom.size;

	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_set_buffer(&handle->forge, (uint8_t *)handle->event_out, capacity);
	LV2_Atom_Forge_Ref ref = lv2_atom_forge_sequence_head(&handle->forge, &frame, 0);

	handle->beats_period = handle->beats_upper;

	int64_t last_t = 0;
	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		const int time_event = timely_advance(&handle->timely, obj, last_t, ev->time.frames);
		if(!time_event)
			props_advance(&handle->props, &handle->forge, ev->time.frames, obj, &ref);
		handle->beats_upper = _beats(&handle->timely);

		if(handle->record && !time_event && handle->rolling)
			_rec(handle, ev); // dont' record time position signals
	
		if(!handle->record && handle->rolling)
			_play(handle, ev->time.frames, capacity);

		last_t = ev->time.frames;
	}

	timely_advance(&handle->timely, NULL, last_t, nsamples);
	handle->beats_upper = _beats(&handle->timely);

	if(!handle->record && handle->rolling)
		_play(handle, nsamples, capacity);

	// trigger post triggers
	if(handle->rolling)
	{
		if(handle->record)
			_trigger_record(handle);
		else
			_trigger_play(handle);
	}

	lv2_atom_forge_pop(&handle->forge, &frame);
}

static void
deactivate(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	if(handle->io)
	{
		fclose(handle->io);
		handle->io = NULL;
	}
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	if(handle->to_disk)
		varchunk_free(handle->to_disk);
	if(handle->from_disk)
		varchunk_free(handle->from_disk);

	if(handle->io)
		fclose(handle->io);

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

// non-rt thread
static LV2_Worker_Status
_work(LV2_Handle instance,
	LV2_Worker_Respond_Function respond,
	LV2_Worker_Respond_Handle target,
	uint32_t size,
	const void *body)
{
	plughandle_t *handle = instance;

	const job_t *job = body;
	switch(job->type)
	{
		case JOB_PLAY:
		{
			while(handle->io && !feof(handle->io))
			{
				// read event header from disk
				LV2_Atom_Event ev1;
				if(fread(&ev1, sizeof(LV2_Atom_Event), 1, handle->io) == 1)
				{
					_event_ntoh(&ev1); // swap bytes

					size_t len = sizeof(LV2_Atom_Event) + ev1.body.size;
					LV2_Atom_Event *ev2;
					if((ev2 = varchunk_write_request(handle->from_disk, len)))
					{
						// clone event data
						ev2->time.beats = ev1.time.beats;
						ev2->body.type = ev1.body.type;
						ev2->body.size = ev1.body.size;

						// read event body from disk
						if(ev1.body.size == 0)
						{
							//fprintf(stderr, "play: beats %lf %u\n", ev1.time.beats, ev1.body.size);
							handle->seek_beats = ev1.time.beats;
							varchunk_write_advance(handle->from_disk, len);
						}
						else if(fread(LV2_ATOM_BODY(&ev2->body), ev1.body.size, 1, handle->io) == 1)
						{
							//fprintf(stderr, "play: beats %lf %u\n", ev1.time.beats, ev1.body.size);
							handle->seek_beats = ev1.time.beats;
							varchunk_write_advance(handle->from_disk, len);
						}
						else
						{
							if(!feof(handle->io) && handle->log)
								lv2_log_warning(&handle->logger, "play: reading event body failed.");
						}
					}
					else
					{
						fseek(handle->io, -sizeof(LV2_Atom_Event), SEEK_CUR); // rewind

						break; // ringbuffer full
					}
				}
				else
				{
					if(!feof(handle->io) && handle->log)
						lv2_log_warning(&handle->logger, "play: reading event header failed.");
				}
			}

			break;
		}
		case JOB_RECORD:
		{
			const LV2_Atom_Event *ev;
			size_t len;
			while(handle->io && (ev = varchunk_read_request(handle->to_disk, &len)))
			{
				double beats = ev->time.beats;

				_event_hton((LV2_Atom_Event *)ev); // swap bytes
				if(fwrite(ev, len, 1, handle->io) == 1)
				{
					//fprintf(stderr, "record: beats %lf %u\n", ev->time.beats, ev->body.size);
					handle->seek_beats = beats;
					varchunk_read_advance(handle->to_disk);
				}
				else if(handle->log)
				{
					lv2_log_warning(&handle->logger, "record: writing event failed.");
					break;
				}
			}

			break;
		}
		case JOB_SEEK:
		{
			if(handle->log)
				lv2_log_note(&handle->logger, "seek: request to %lf", job->beats);

			// rewind to start of file
			if(job->beats < handle->seek_beats)
			{
				if(handle->log)
					lv2_log_note(&handle->logger, "seek rewind");
				fseek(handle->io, 0, SEEK_SET);
				handle->seek_beats = 0.0;
			}

			while(handle->io && !feof(handle->io))
			{
				// read event header from disk
				LV2_Atom_Event ev1;
				if(fread(&ev1, sizeof(LV2_Atom_Event), 1, handle->io) == 1)
				{
					_event_hton(&ev1); // swap bytes

					// check whether event precedes current seek
					if(ev1.time.beats < job->beats)
					{
						handle->seek_beats = ev1.time.beats;

						// skip event
						if(ev1.body.size > 0)
							fseek(handle->io, ev1.body.size, SEEK_CUR); //FIXME check return

						continue;
					}

					// unread event header
					fseek(handle->io, -sizeof(LV2_Atom_Event), SEEK_CUR); //FIXME check return
					if(handle->log)
						lv2_log_note(&handle->logger, "seek: seeked to %lf", handle->seek_beats);

					break;
				}
				else
				{
					if(!feof(handle->io) && handle->log)
						lv2_log_warning(&handle->logger, "seek: reading event header failed.");
				}
			}

			break;
		}
		case JOB_DRAIN:
		{
			// inject drain marker event
			LV2_Atom_Event *ev;
			const size_t len = sizeof(LV2_Atom_Event);
			if((ev = varchunk_write_request(handle->from_disk, len))) //FIXME try until success
			{
				ev->time.beats = 0.0; //XXX
				ev->body.type = handle->uris.eteroj_drain;
				ev->body.size = 0;

				varchunk_write_advance(handle->from_disk, len);
			}
			else if(handle->log)
				lv2_log_warning(&handle->logger, "drain: ringbuffer overflow");

			break;
		}
		case JOB_OPEN_PLAY:
		{
			if(handle->log)
				lv2_log_note(&handle->logger, "open for playback: %s", handle->path);
			if(handle->io)
				fclose(handle->io);
			if(!(handle->io = fopen(handle->path, "r+")) && handle->log)
				lv2_log_error(&handle->logger, "failed to open file.");

			break;
		}
		case JOB_OPEN_RECORD:
		{
			if(handle->log)
				lv2_log_note(&handle->logger, "open for recording: %s", handle->path);
			if(handle->io)
				fclose(handle->io);
			if(!(handle->io = fopen(handle->path, "w+")) && handle->log)
				lv2_log_error(&handle->logger, "failed to open file.");

			break;
		}
	}

	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_work_response(LV2_Handle instance, uint32_t size, const void *body)
{
	plughandle_t *handle = instance;

	//TODO

	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_end_run(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	//TODO

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
