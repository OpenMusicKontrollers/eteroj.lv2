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
#include <osc.lv2/writer.h>
#include <osc.lv2/forge.h>
#include <props.h>

#define MAX_STRLEN 1024
#define BUF_SIZE 0x10000
#define DEFAULT_FILE_NAME "seq.osc"
#define MAX_NPROPS 2

typedef enum _jobtype_t jobtype_t;
typedef struct _job_t job_t;
typedef struct _plugstate_t plugstate_t;
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

struct _plugstate_t {
	int32_t record;
	char path [MAX_STRLEN];
};

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_URID_Unmap *unmap;
	LV2_Atom_Forge forge;
	LV2_OSC_URID osc_urid;
	struct {
		LV2_URID eteroj_drain;
	} uris;

	timely_t timely;
	LV2_Worker_Schedule *sched;

	PROPS_T(props, MAX_NPROPS);

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;

	LV2_Log_Log *log;
	LV2_Log_Logger logger;

	double beats_period;
	double beats_upper;

	bool rolling;
	int draining;

	plugstate_t state;
	plugstate_t stash;

	FILE *io;

	double seek_beats;

	varchunk_t *to_disk;
	varchunk_t *from_disk;

	int frame_cnt;
	LV2_Atom_Forge_Frame frame [32][2]; // 32 nested bundles should be enough
	LV2_Atom_Forge_Ref ref;
	int64_t offset;

	uint8_t *ptr;
	uint8_t *end;

	int bndl_cnt;
	uint8_t *bndl [32]; // 32 nested bundles should be enough
};

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

static void
_intercept(void *data, int64_t frames, props_impl_t *impl)
{
	plughandle_t *handle = data;

	if(handle->state.record)
		_trigger_open_record(handle);
	else
		_trigger_open_play(handle);
}

static const props_def_t defs [MAX_NPROPS] = {
	{
		.property = ETEROJ_DISK_RECORD_URI,
		.offset = offsetof(plugstate_t, record),
		.type = LV2_ATOM__Bool,
		.event_cb = _intercept
	},
	{
		.property = ETEROJ_DISK_PATH_URI,
		.offset = offsetof(plugstate_t, path),
		.access = LV2_PATCH__readable,
		.type = LV2_ATOM__Path,
		.max_size = MAX_STRLEN 
	}
};

static inline void
_event_ntoh(LV2_Atom_Event *ev)
{
	ev->time.frames = be64toh(ev->time.frames);
	ev->body.type = be32toh(ev->body.type);
	ev->body.size = be32toh(ev->body.size);
}
#define _event_hton _event_ntoh

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
				lv2_log_trace(&handle->logger, "_play: event late %"PRIi64, handle->offset);

			handle->offset = 0;
		}

		if(handle->ref)
			handle->ref = lv2_atom_forge_frame_time(&handle->forge, handle->offset);
		if(handle->ref)
			handle->ref = lv2_osc_forge_packet(&handle->forge, &handle->osc_urid, handle->map,
				LV2_ATOM_BODY_CONST(&src->body), src->body.size);

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
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&src->body;
		uint8_t *base = LV2_ATOM_BODY(&dst->body);

		LV2_OSC_Writer writer;
		lv2_osc_writer_initialize(&writer, base, src->body.size);
		lv2_osc_writer_packet(&writer, &handle->osc_urid, handle->unmap, obj->atom.size, &obj->body);
		size_t size;
		lv2_osc_writer_finalize(&writer, &size);

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

		if(handle->state.record)
			_reposition_rec(handle, beats);
		else
			_reposition_play(handle, beats);
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
		else if(!strcmp(features[i]->URI, LV2_URID__unmap))
			handle->unmap = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_WORKER__schedule))
			handle->sched = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_STATE__makePath))
			make_path = features[i]->data;
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
	lv2_osc_urid_init(&handle->osc_urid, handle->map);

	char *tmp = make_path->path(make_path->handle, DEFAULT_FILE_NAME);
	if(tmp)
	{
		strncpy(handle->state.path, tmp, MAX_STRLEN-1);
		free(tmp);
	
		if(access(handle->state.path, F_OK)) // does not yet exist
		{
			fprintf(stderr, "touching file\n");
			handle->io = fopen(handle->state.path, "w");
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

	if(!props_init(&handle->props, descriptor->URI,
		defs, MAX_NPROPS, &handle->state, &handle->stash,
		handle->map, handle))
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
	handle->ref = lv2_atom_forge_sequence_head(&handle->forge, &frame, 0);

	props_idle(&handle->props, &handle->forge, 0, &handle->ref);

	handle->beats_period = handle->beats_upper;

	int64_t last_t = 0;
	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		const int time_event = timely_advance(&handle->timely, obj, last_t, ev->time.frames);
		if(!time_event)
			props_advance(&handle->props, &handle->forge, ev->time.frames, obj, &handle->ref);
		handle->beats_upper = _beats(&handle->timely);

		if(handle->state.record && !time_event && handle->rolling)
			_rec(handle, ev); // dont' record time position signals
	
		if(!handle->state.record && handle->rolling)
			_play(handle, ev->time.frames, capacity);

		last_t = ev->time.frames;
	}

	timely_advance(&handle->timely, NULL, last_t, nsamples);
	handle->beats_upper = _beats(&handle->timely);

	if(!handle->state.record && handle->rolling)
		_play(handle, nsamples, capacity);

	// trigger post triggers
	if(handle->rolling)
	{
		if(handle->state.record)
			_trigger_record(handle);
		else
			_trigger_play(handle);
	}

	if(handle->ref)
		lv2_atom_forge_pop(&handle->forge, &frame);
	else
		lv2_atom_sequence_clear(handle->event_out);
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

// non-rt thread
static LV2_Worker_Status
_work(LV2_Handle instance,
	LV2_Worker_Respond_Function respond,
	LV2_Worker_Respond_Handle worker,
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
				lv2_log_note(&handle->logger, "open for playback: %s", handle->state.path);
			if(handle->io)
				fclose(handle->io);
			if(!(handle->io = fopen(handle->state.path, "r+")) && handle->log)
				lv2_log_error(&handle->logger, "failed to open file.");

			break;
		}
		case JOB_OPEN_RECORD:
		{
			if(handle->log)
				lv2_log_note(&handle->logger, "open for recording: %s", handle->state.path);
			if(handle->io)
				fclose(handle->io);
			if(!(handle->io = fopen(handle->state.path, "w+")) && handle->log)
				lv2_log_error(&handle->logger, "failed to open file.");

			break;
		}

		default:
		{
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

	return LV2_WORKER_SUCCESS;
}

static const LV2_Worker_Interface work_iface = {
	.work = _work,
	.work_response = _work_response,
	.end_run = NULL
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
