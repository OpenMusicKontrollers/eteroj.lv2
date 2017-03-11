/*
 * Copyright (c) 2017 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

#include <time.h>
#include <netatom.lv2/netatom.h>
#include <sratom/sratom.h>

#define MAX_URIDS 2048

typedef struct _urid_t urid_t;
typedef struct _handle_t handle_t;

struct _urid_t {
	LV2_URID urid;
	char *uri;
};

struct _handle_t {
	urid_t urids [MAX_URIDS];
	LV2_URID urid;
};

static LV2_URID
_map(LV2_URID_Map_Handle instance, const char *uri)
{
	handle_t *handle = instance;

	urid_t *itm;
	for(itm=handle->urids; itm->urid; itm++)
	{
		if(!strcmp(itm->uri, uri))
			return itm->urid;
	}

	assert(handle->urid + 1 < MAX_URIDS);

	// create new
	itm->urid = ++handle->urid;
	itm->uri = strdup(uri);

	return itm->urid;
}

static const char *
_unmap(LV2_URID_Unmap_Handle instance, LV2_URID urid)
{
	handle_t *handle = instance;

	for(urid_t *itm=handle->urids; itm->urid; itm++)
	{
		if(itm->urid == urid)
			return itm->uri;
	}

	// not found
	return NULL;
}

static void
_freemap(handle_t *handle)
{
	for(urid_t *itm = handle->urids; itm->urid; itm++)
		free(itm->uri);
}

static void
_netatom_test(LV2_URID_Map *map, LV2_URID_Unmap *unmap, bool swap,
	const LV2_Atom *atom, unsigned iterations)
{
	netatom_t *netatom_tx = netatom_new(map, unmap, swap);
	netatom_t *netatom_rx = netatom_new(map, unmap, swap);

	for(unsigned i = 0; i < iterations; i++)
	{
		uint32_t size_tx;
		const uint8_t *buf_tx = netatom_serialize(netatom_tx, atom, &size_tx);

		if(iterations == 1)
		{
			fwrite(buf_tx, size_tx, 1, stdout);

			fprintf(stderr, "%u, %u, %f\n", lv2_atom_total_size(atom), size_tx,
				(float)size_tx / lv2_atom_total_size(atom));
		}

		const LV2_Atom *atom_rx = netatom_deserialize(netatom_rx, buf_tx, size_tx);
		const uint32_t size_rx = lv2_atom_total_size(atom_rx);

		assert(size_rx == lv2_atom_pad_size(lv2_atom_total_size(atom)));
		assert(memcmp(atom, atom_rx, size_rx) == 0);
	}

	netatom_free(netatom_tx);
	netatom_free(netatom_rx);
}

static void
_sratom_test(LV2_URID_Map *map, LV2_URID_Unmap *unmap, bool pretty,
	const LV2_Atom *atom, unsigned iterations)
{
	Sratom *sratom = sratom_new(map);
	assert(sratom);
	sratom_set_pretty_numbers(sratom, pretty);
	const char *base_uri = "file:///tmp/base";
	const SerdNode subject = serd_node_from_string(SERD_URI, (const uint8_t *)(""));
	const SerdNode predicate = serd_node_from_string(SERD_URI, (const uint8_t *)(LV2_ATOM__atomTransfer));

	for(unsigned i = 0; i < iterations; i++)
	{
		char *ttl = sratom_to_turtle(sratom, unmap, base_uri, &subject, &predicate,
			atom->type, atom->size, LV2_ATOM_BODY_CONST(atom));
		assert(ttl);

		LV2_Atom *clone = sratom_from_turtle(sratom, base_uri, &subject, &predicate, ttl);
		assert(clone);

		assert(atom->size == clone->size);
		assert(atom->type == clone->type);
		//assert(memcmp(LV2_ATOM_BODY_CONST(atom), LV2_ATOM_BODY_CONST(clone), atom->size) == 0);

		free(clone);
		free(ttl);
	}

	sratom_free(sratom);
}

#define MAP(O) map.map(map.handle, "urn:netatom:test#"O)

int
main(int argc, char **argv)
{
	static handle_t handle;

	if(argc < 2)
		return -1;
	const unsigned iterations = atoi(argv[1]);

	LV2_URID_Map map = {
		.handle = &handle,
		.map = _map
	};

	LV2_URID_Unmap unmap = {
		.handle = &handle,
		.unmap = _unmap
	};

	LV2_Atom_Forge forge;
	lv2_atom_forge_init(&forge, &map);

	union {
		LV2_Atom atom;
		uint8_t buf [2048];
	} un;

	lv2_atom_forge_set_buffer(&forge, un.buf, 2048);

	LV2_Atom_Forge_Frame obj_frame;
	lv2_atom_forge_object(&forge, &obj_frame, 0, MAP("otype"));
	{
		lv2_atom_forge_key(&forge, forge.Int);
		lv2_atom_forge_int(&forge, 12);

		lv2_atom_forge_key(&forge, forge.Bool);
		lv2_atom_forge_bool(&forge, 1);

		lv2_atom_forge_key(&forge, forge.Long);
		lv2_atom_forge_long(&forge, 14);

		lv2_atom_forge_key(&forge, forge.Float);
		lv2_atom_forge_float(&forge, 1.5);

		lv2_atom_forge_key(&forge, forge.Double);
		lv2_atom_forge_double(&forge, 4.5);

		lv2_atom_forge_key(&forge, forge.String);
		lv2_atom_forge_string(&forge, "hello", 5);

		lv2_atom_forge_key(&forge, forge.Path);
		lv2_atom_forge_path(&forge, "/tmp", 4);

		lv2_atom_forge_key(&forge, forge.Literal);
		lv2_atom_forge_literal(&forge, "hello", 5, MAP("dtype"), MAP("lang"));

		/*
		lv2_atom_forge_key(&forge, forge.URI);
		lv2_atom_forge_uri(&forge, LV2_URID__map, strlen(LV2_URID__map));
		*/

		lv2_atom_forge_key(&forge, forge.URID);
		lv2_atom_forge_urid(&forge, MAP("key"));

		const uint8_t m [3] = {0x90, 0x2f, 0x7f};
		lv2_atom_forge_key(&forge, map.map(map.handle, LV2_MIDI__MidiEvent));
		lv2_atom_forge_atom(&forge, 3, map.map(map.handle, LV2_MIDI__MidiEvent));
		lv2_atom_forge_write(&forge, m, 3);

		const uint8_t b [8] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7};
		lv2_atom_forge_key(&forge, forge.Chunk);
		lv2_atom_forge_atom(&forge, 8, forge.Chunk);
		lv2_atom_forge_write(&forge, b, 8);

		LV2_Atom_Forge_Frame tup_frame;
		lv2_atom_forge_key(&forge, forge.Tuple);
		lv2_atom_forge_tuple(&forge, &tup_frame);
		{
			for(unsigned i = 0; i < 16; i++)
				lv2_atom_forge_int(&forge, i);
		}
		lv2_atom_forge_pop(&forge, &tup_frame);

		LV2_Atom_Forge_Frame vec_frame;
		lv2_atom_forge_key(&forge, forge.Vector);
		lv2_atom_forge_vector_head(&forge, &vec_frame, sizeof(int32_t), forge.Int);
		{
			for(unsigned i = 0; i < 16; i++)
				lv2_atom_forge_int(&forge, i);
		}
		lv2_atom_forge_pop(&forge, &vec_frame);

		LV2_Atom_Forge_Frame seq_frame;
		lv2_atom_forge_key(&forge, forge.Sequence);
		lv2_atom_forge_sequence_head(&forge, &seq_frame, MAP(LV2_ATOM__frameTime));
		{
			for(unsigned i = 0; i < 16; i++)
			{
				lv2_atom_forge_frame_time(&forge, i);
				lv2_atom_forge_int(&forge, i);
			}
		}
		lv2_atom_forge_pop(&forge, &seq_frame);
	}
	lv2_atom_forge_pop(&forge, &obj_frame);

	// add some dummy URI to hash map
	char tmp [32];
	for(int i=0; i<1024; i++)
	{
		snprintf(tmp, 32, "urn:dummy:%i", i);
		map.map(map.handle, tmp);
	}

	struct timespec t0, t1, t2;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	_netatom_test(&map, &unmap, true, &un.atom, iterations);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	_sratom_test(&map, &unmap, false, &un.atom, iterations);
	clock_gettime(CLOCK_MONOTONIC, &t2);

	const double d1 = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
	const double d2 = (t2.tv_sec - t1.tv_sec) + (t2.tv_nsec - t1.tv_nsec) * 1e-9;
	fprintf(stderr, "%lf s, %lf s, x %lf\n", d1, d2, d2/d1);

	_freemap(&handle);

	return 0;
}
