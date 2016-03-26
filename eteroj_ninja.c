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
#include <lv2_osc.h>

#include <sratom/sratom.h>

#define BUF_SIZE 2048

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_URID_Unmap *unmap;

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;
	LV2_Atom_Forge forge;
	osc_forge_t oforge;
	LV2_Atom_Forge_Ref ref;
	int64_t frames;

	Sratom *sratom;
	SerdNode subject;
	SerdNode predicate;
};
		
static const char *base_uri = "file:///tmp/base";

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

	lv2_atom_forge_init(&handle->forge, handle->map);
	osc_forge_init(&handle->oforge, handle->map);

	handle->sratom = sratom_new(handle->map);
	if(!handle->sratom)
	{
		free(handle);
		return NULL;
	}

	handle->subject = serd_node_from_string(SERD_URI, (const uint8_t*)(""));
	handle->predicate = serd_node_from_string(SERD_URI, (const uint8_t*)(LV2_ATOM__Event));

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
	LV2_Atom_Forge_Ref ref = handle->ref;

	if(strcmp(path, "/ttl") || strcmp(fmt, "s"))
		return;

	const LV2_Atom *itr = lv2_atom_tuple_begin(body);
	if(itr->type != forge->String)
		return;

	LV2_Atom *atom = sratom_from_turtle(handle->sratom, base_uri,
		&handle->subject, &handle->predicate, LV2_ATOM_BODY_CONST(itr));
	if(atom)
	{
		if(ref)
			ref = lv2_atom_forge_frame_time(forge, handle->frames);
		if(ref)
			ref = lv2_atom_forge_write(forge, atom, lv2_atom_total_size(atom));

		free(atom); //FIXME not rt-safe
	}

	handle->ref = ref;
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = (plughandle_t *)instance;

	// prepare osc atom forge
	const uint32_t capacity = handle->event_out->atom.size;
	LV2_Atom_Forge *forge = &handle->forge;
	osc_forge_t *oforge = &handle->oforge;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->event_out, capacity);
	LV2_Atom_Forge_Frame frame;
	handle->ref = lv2_atom_forge_sequence_head(forge, &frame, 0);
	
	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
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
				if(handle->ref)
					handle->ref = lv2_atom_forge_frame_time(forge, ev->time.frames);
				if(handle->ref)
					handle->ref = osc_forge_message_vararg(&handle->oforge, forge, "/ttl", "s", ttl);

				free(ttl); //FIXME not rt-safe
			}
		}
	}

	if(handle->ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->event_out);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = (plughandle_t *)instance;

	sratom_free(handle->sratom);
	munlock(handle, sizeof(plughandle_t));
	free(handle);
}

const LV2_Descriptor eteroj_ninja = {
	.URI						= ETEROJ_NINJA_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= NULL
};
