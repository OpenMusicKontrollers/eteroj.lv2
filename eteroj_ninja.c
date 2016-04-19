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
#include <varchunk.h>
#include <lv2_osc.h>

#include <sratom/sratom.h>

#define NS_RDF "http://www.w3.org/1999/02/22-rdf-syntax-ns#"
#define BUF_SIZE 8192

typedef struct _atom_ser_t atom_ser_t;
typedef struct _plughandle_t plughandle_t;

struct _atom_ser_t {
	uint32_t size;
	uint8_t *buf;
	uint32_t offset;
};

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_URID_Unmap *unmap;
	LV2_Worker_Schedule *sched;

	atom_ser_t ser;

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;
	LV2_Atom_Forge forge;
	osc_forge_t oforge;
	int64_t frames;

	Sratom *sratom;
	SerdNode subject;
	SerdNode predicate;

	varchunk_t *to_worker;
	varchunk_t *from_worker;
};
		
static const char *base_uri = "file:///tmp/base";

static inline LV2_Atom_Forge_Ref
_sink(LV2_Atom_Forge_Sink_Handle handle, const void *buf, uint32_t size)
{
	atom_ser_t *ser = handle;

	const LV2_Atom_Forge_Ref ref = ser->offset + 1;

	const uint32_t new_offset = ser->offset + size;
	if(new_offset > ser->size)
	{
		uint32_t new_size = ser->size << 1;
		while(new_offset > new_size)
			new_size <<= 1;

		if(!(ser->buf = realloc(ser->buf, new_size)))
			return 0; // realloc failed

		ser->size = new_size;
	}

	memcpy(ser->buf + ser->offset, buf, size);
	ser->offset = new_offset;

	return ref;
}

static inline LV2_Atom *
_deref(LV2_Atom_Forge_Sink_Handle handle, LV2_Atom_Forge_Ref ref)
{
	atom_ser_t *ser = handle;

	const uint32_t offset = ref - 1;

	return (LV2_Atom *)(ser->buf + offset);
}

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;
	mlock(handle, sizeof(plughandle_t));

	for(unsigned i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_URID__unmap))
			handle->unmap = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_WORKER__schedule))
			handle->sched= features[i]->data;
	}

	if(!handle->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	if(!handle->unmap)
	{
		fprintf(stderr, "%s: Host does not support urid:unmap\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	if(!handle->sched)
	{
		fprintf(stderr, "%s: Host does not support work:schedule\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	lv2_atom_forge_init(&handle->forge, handle->map);
	osc_forge_init(&handle->oforge, handle->map);

	handle->sratom = sratom_new(handle->map);
	if(!handle->sratom)
	{
		free(handle);
		return NULL;
	}

	handle->subject = serd_node_from_string(SERD_URI, (const uint8_t*)(""));
	handle->predicate = serd_node_from_string(SERD_URI, (const uint8_t*)(NS_RDF"value"));

	handle->to_worker = varchunk_new(BUF_SIZE, true);
	handle->from_worker = varchunk_new(BUF_SIZE, true);

	if(!handle->to_worker || !handle->from_worker)
	{
		free(handle);
		return NULL;
	}

	handle->ser.size = 2018;
	handle->ser.offset = 0;
	handle->ser.buf = malloc(handle->ser.size); //TODO check

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
_message(const char *path, const char *fmt,
	const LV2_Atom_Tuple *body, void *data)
{
	plughandle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge;

	if(strcmp(path, "/ttl") || strcmp(fmt, "s"))
		return;

	const LV2_Atom *itr = lv2_atom_tuple_begin(body);
	if(itr->type != forge->String)
		return;

	LV2_Atom *atom = sratom_from_turtle(handle->sratom, base_uri,
		&handle->subject, &handle->predicate, LV2_ATOM_BODY_CONST(itr));
	if(atom)
	{
		lv2_atom_forge_frame_time(forge, handle->frames);
		lv2_atom_forge_write(forge, atom, lv2_atom_total_size(atom));

		free(atom);
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = (plughandle_t *)instance;

	// prepare osc atom forge
	const uint32_t capacity = handle->event_out->atom.size;

	// move input events to worker thread
	if(handle->event_in->atom.size > sizeof(LV2_Atom_Sequence_Body))
	{
		const size_t size = lv2_atom_total_size(&handle->event_in->atom);
		LV2_Atom_Sequence *seq;
		if((seq = varchunk_write_request(handle->to_worker, size)))
		{
			memcpy(seq, handle->event_in, size);
			varchunk_write_advance(handle->to_worker, size);

			const int32_t dummy;
			handle->sched->schedule_work(handle->sched->handle, sizeof(int32_t), &dummy);
		}
	}

	// move output events from worker thread
	size_t size;
	const LV2_Atom_Sequence *seq;
	if((seq = varchunk_read_request(handle->from_worker, &size)))
	{
		if(size <= capacity)
			memcpy(handle->event_out, seq, size);
		varchunk_read_advance(handle->from_worker);
	}
	else
	{
		lv2_atom_sequence_clear(handle->event_out);
	}
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = (plughandle_t *)instance;

	if(handle->ser.buf)
		free(handle->ser.buf);
	if(handle->to_worker)
		varchunk_free(handle->to_worker);
	if(handle->from_worker)
		varchunk_free(handle->from_worker);
	sratom_free(handle->sratom);
	munlock(handle, sizeof(plughandle_t));
	free(handle);
}

// non-rt thread
static LV2_Worker_Status
_work(LV2_Handle instance,
	LV2_Worker_Respond_Function respond,
	LV2_Worker_Respond_Handle target,
	uint32_t size,
	const void *body)
{
	plughandle_t *handle = instance;
	LV2_Atom_Forge *forge = &handle->forge;
	osc_forge_t *oforge = &handle->oforge;

	(void)respond;
	(void)target;
	(void)size;
	(void)body;

	size_t _size;
	const LV2_Atom_Sequence *seq;
	while((seq = varchunk_read_request(handle->to_worker, &_size)))
	{
		handle->ser.offset = 0;
		lv2_atom_forge_set_sink(forge, _sink, _deref, &handle->ser);
		LV2_Atom_Forge_Frame frame;
		lv2_atom_forge_sequence_head(forge, &frame, 0);

		LV2_ATOM_SEQUENCE_FOREACH(seq, ev)
		{
			const LV2_Atom *atom = &ev->body;
			const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;

			if(osc_atom_is_bundle(oforge, obj) || osc_atom_is_message(oforge, obj))
			{
				handle->frames = ev->time.frames;
				osc_atom_event_unroll(oforge, obj, NULL, NULL, _message, handle);
			}
			else
			{
				char *ttl = sratom_to_turtle(handle->sratom, handle->unmap, base_uri,
					&handle->subject, &handle->predicate,
					atom->type, atom->size, LV2_ATOM_BODY_CONST(atom));

				if(ttl)
				{
					lv2_atom_forge_frame_time(forge, ev->time.frames);
					osc_forge_message_vararg(&handle->oforge, forge, "/ttl", "s", ttl);

					free(ttl);
				}
			}
		}

		lv2_atom_forge_pop(forge, &frame);

		seq = (const LV2_Atom_Sequence *)handle->ser.buf;
		if(seq->atom.size > sizeof(LV2_Atom_Sequence_Body))
		{
			const size_t len = lv2_atom_total_size(&seq->atom);
			LV2_Atom_Sequence *dst;
			if((dst = varchunk_write_request(handle->from_worker, len)))
			{
				memcpy(dst, seq, len);
				varchunk_write_advance(handle->from_worker, len);
			}
		}

		varchunk_read_advance(handle->to_worker);
	}

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

	return NULL;
}

const LV2_Descriptor eteroj_ninja = {
	.URI						= ETEROJ_NINJA_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data 
};
