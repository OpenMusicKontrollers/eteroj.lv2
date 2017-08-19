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

#ifndef _NETATOM_H
#define _NETATOM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>

#include <netatom.lv2/endian.h>

#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>

#ifndef NETATOM_API
#	define NETATOM_API static
#endif

typedef struct _netatom_t netatom_t;

NETATOM_API uint8_t *
netatom_serialize(netatom_t *netatom, LV2_Atom *atom, size_t size_rx,
	size_t *size_tx);

NETATOM_API const LV2_Atom *
netatom_deserialize(netatom_t *netatom, uint8_t *buf_tx, size_t size_tx);

NETATOM_API netatom_t *
netatom_new(LV2_URID_Map *map, LV2_URID_Unmap *unmap, bool swap);

NETATOM_API void
netatom_free(netatom_t *netatom);

#ifdef NETATOM_IMPLEMENTATION

typedef union _netatom_union_t netatom_union_t;

union _netatom_union_t {
	LV2_Atom *atom;
	uint8_t *buf;
};

struct _netatom_t {
	bool swap;
	LV2_URID_Unmap *unmap;
	LV2_URID_Map *map;
	LV2_Atom_Forge forge;
	struct {
		uint8_t *buf;
		const uint8_t *cur;
		const uint8_t *end;
	} dict;
	uint32_t MIDI_MidiEvent;
	bool overflow;
};

static inline void
_netatom_ser_uri(netatom_t *netatom, uint32_t *urid, const char *uri)
{
	if(*urid == 0)
		return; // ignore untyped atoms

	// look for matching URID in dictionary
	uint32_t match = 0;

	for(netatom_union_t ptr = { .buf = netatom->dict.buf };
		ptr.buf < netatom->dict.cur;
		ptr.buf += lv2_atom_pad_size(lv2_atom_total_size(ptr.atom)))
	{
		if(ptr.atom->type == *urid)
		{
			match = ptr.buf - netatom->dict.buf + 1;
			break;
		}
	}

	if(match) // use already matched URI in dictionary
	{
		*urid = match;
	}
	else // add new URI to dictionary
	{
		if(!uri)
			uri = netatom->unmap->unmap(netatom->unmap->handle, *urid);

		if(!uri) // invalid urid
		{
			*urid = 0;
		}
		else
		{
			const uint32_t size = strlen(uri) + 1;
			const uint32_t tot_size = sizeof(LV2_Atom) + lv2_atom_pad_size(size);
			const uint32_t ref = netatom->dict.cur - netatom->dict.buf + 1;

			if(netatom->dict.cur + tot_size <= netatom->dict.end)
			{
				LV2_Atom *atom = (LV2_Atom *)netatom->dict.cur;
				atom->size = size;
				atom->type = *urid;
				strncpy(LV2_ATOM_BODY(atom), uri, tot_size); // automatic padding

				*urid = ref;
				netatom->dict.cur += tot_size;
			}
			else // dict buffer overflow
			{
				*urid = 0;
				netatom->overflow = true;
			}
		}
	}

	if(netatom->swap)
		*urid = htobe32(*urid);
}

static inline void
_netatom_ser_dict(netatom_t *netatom)
{
	LV2_Atom *body = NULL;
	for(netatom_union_t ptr = { .buf = netatom->dict.buf };
		ptr.buf < netatom->dict.cur;
		ptr.buf += lv2_atom_pad_size(lv2_atom_total_size(ptr.atom)))
	{
		if( netatom->swap && body)
			body->size = htobe32(body->size);
		body = ptr.atom;
		ptr.atom->type = 0; // clear key
	}
	if(netatom->swap && body)
		body->size = htobe32(body->size);
}

static inline void
_netatom_deser_uri(netatom_t *netatom, uint32_t *urid)
{
	if(*urid == 0)
		return; // ignore untyped atoms

	const uint32_t ref = netatom->swap
		? be32toh(*urid)
		: *urid;

	const LV2_Atom *atom = (const LV2_Atom *)&netatom->dict.buf[ref - 1];
	*urid = atom->type;
}

static inline void
_netatom_deser_dict(netatom_t *netatom)
{
	for(netatom_union_t ptr = { .buf = netatom->dict.buf};
		ptr.buf < netatom->dict.cur;
		ptr.buf += lv2_atom_pad_size(lv2_atom_total_size(ptr.atom)))
	{
		if(netatom->swap)
			ptr.atom->size = be32toh(ptr.atom->size);
		const char *uri = LV2_ATOM_BODY_CONST(ptr.atom);
		ptr.atom->type = netatom->map->map(netatom->map->handle, uri);
	}
}

static void
_netatom_ser_atom(netatom_t *netatom, LV2_Atom *atom)
{
	LV2_Atom_Forge *forge = &netatom->forge;
	const char *uri = NULL;

	if(atom->type == forge->Bool)
	{
		if(netatom->swap)
		{
			uint32_t *u = LV2_ATOM_BODY(atom);
			*u = htobe32(*u);
		}
		uri = LV2_ATOM__Bool;
	}
	else if(atom->type == forge->Int)
	{
		if(netatom->swap)
		{
			uint32_t *u = LV2_ATOM_BODY(atom);
			*u = htobe32(*u);
		}
		uri = LV2_ATOM__Int;
	}
	else if(atom->type == forge->Float)
	{
		if(netatom->swap)
		{
			uint32_t *u = LV2_ATOM_BODY(atom);
			*u = htobe32(*u);
		}
		uri = LV2_ATOM__Float;
	}
	else if(atom->type == forge->Long)
	{
		if(netatom->swap)
		{
			uint64_t *u = LV2_ATOM_BODY(atom);
			*u = htobe64(*u);
		}
		uri = LV2_ATOM__Long;
	}
	else if(atom->type == forge->Double)
	{
		if(netatom->swap)
		{
			uint64_t *u = LV2_ATOM_BODY(atom);
			*u = htobe64(*u);
		}
		uri = LV2_ATOM__Double;
	}
	else if(atom->type == forge->URID)
	{
		uint32_t *u = LV2_ATOM_BODY(atom);
		_netatom_ser_uri(netatom, u, NULL);
		uri = LV2_ATOM__URID;
	}
	else if(atom->type == forge->String)
	{
		uri = LV2_ATOM__String;
	}
	else if(atom->type == forge->Chunk)
	{
		uri = LV2_ATOM__Chunk;
	}
	else if(atom->type == netatom->MIDI_MidiEvent)
	{
		uri = LV2_MIDI__MidiEvent;
	}
	else if(atom->type == forge->Literal)
	{
		LV2_Atom_Literal *lit = (LV2_Atom_Literal *)atom;
		_netatom_ser_uri(netatom, &lit->body.datatype, NULL);
		_netatom_ser_uri(netatom, &lit->body.lang, NULL);
		uri = LV2_ATOM__Literal;
	}
	else if(atom->type == forge->Object)
	{
		LV2_Atom_Object *obj = (LV2_Atom_Object *)atom;
		LV2_Atom *body = NULL;
		LV2_ATOM_OBJECT_FOREACH(obj, prop)
		{
			if(body)
				_netatom_ser_atom(netatom, body);
			body = &prop->value;
			_netatom_ser_uri(netatom, &prop->key, NULL);
			_netatom_ser_uri(netatom, &prop->context, NULL);
		}
		if(body)
			_netatom_ser_atom(netatom, body);
		_netatom_ser_uri(netatom, &obj->body.id, NULL);
		_netatom_ser_uri(netatom, &obj->body.otype, NULL);
		uri = LV2_ATOM__Object;
	}
	else if(atom->type == forge->Tuple)
	{
		LV2_Atom_Tuple *tup = (LV2_Atom_Tuple *)atom;
		LV2_Atom *body = NULL;
		LV2_ATOM_TUPLE_FOREACH(tup, item)
		{
			if(body)
				_netatom_ser_atom(netatom, body);
			body = item;
		}
		if(body)
			_netatom_ser_atom(netatom, body);
		uri = LV2_ATOM__Tuple;
	}
	else if(atom->type == forge->Sequence)
	{
		LV2_Atom_Sequence *seq = (LV2_Atom_Sequence *)atom;
		LV2_Atom *body = NULL;
		LV2_ATOM_SEQUENCE_FOREACH(seq, ev)
		{
			if(body)
				_netatom_ser_atom(netatom, body);
			body = &ev->body;
			if(netatom->swap)
				ev->time.frames = htobe64(ev->time.frames);
		}
		if(body)
			_netatom_ser_atom(netatom, body);
		_netatom_ser_uri(netatom, &seq->body.unit, NULL);
		if(netatom->swap)
			seq->body.pad = htobe32(seq->body.pad);
		uri = LV2_ATOM__Sequence;
	}
	else if(atom->type == forge->Vector)
	{
		LV2_Atom_Vector *vec = (LV2_Atom_Vector *)atom;
		if(netatom->swap)
		{
			if(vec->body.child_size == 4)
			{
				const unsigned n = (vec->atom.size - sizeof(LV2_Atom_Vector_Body)) / 4;
				uint32_t *u = LV2_ATOM_CONTENTS(LV2_Atom_Vector, atom);
				for(unsigned i = 0; i < n; i++)
					u[i] = htobe32(u[i]);
			}
			else if(vec->body.child_size == 8)
			{
				const unsigned n = (vec->atom.size - sizeof(LV2_Atom_Vector_Body)) / 8;
				uint64_t *u = LV2_ATOM_CONTENTS(LV2_Atom_Vector, atom);
				for(unsigned i = 0; i < n; i++)
					u[i] = htobe64(u[i]);
			}
			vec->body.child_size = htobe32(vec->body.child_size);
		}
		_netatom_ser_uri(netatom, &vec->body.child_type, NULL); //TODO set uri
		uri = LV2_ATOM__Vector;
	}
	else if(atom->type == forge->Path)
	{
		uri = LV2_ATOM__Path;
	}
	else if(atom->type == forge->URI)
	{
		uri = LV2_ATOM__URI;
	}

	if(netatom->swap)
		atom->size = htobe32(atom->size);
	_netatom_ser_uri(netatom, &atom->type, uri);
}

static void
_netatom_deser_atom(netatom_t *netatom, LV2_Atom *atom)
{
	LV2_Atom_Forge *forge = &netatom->forge;

	if(netatom->swap)
		atom->size = be32toh(atom->size);
	_netatom_deser_uri(netatom, &atom->type);

	if(  (atom->type == forge->Bool)
		|| (atom->type == forge->Int)
		|| (atom->type == forge->Float) )
	{
		if(netatom->swap)
		{
			uint32_t *u = LV2_ATOM_BODY(atom);
			*u = be32toh(*u);
		}
	}
	else if( (atom->type == forge->Long)
		|| (atom->type == forge->Double) )
	{
		if(netatom->swap)
		{
			uint64_t *u = LV2_ATOM_BODY(atom);
			*u = be64toh(*u);
		}
	}
	else if(atom->type == forge->URID)
	{
		uint32_t *u = LV2_ATOM_BODY(atom);
		_netatom_deser_uri(netatom, u);
	}
	else if(atom->type == forge->Literal)
	{
		LV2_Atom_Literal *lit = (LV2_Atom_Literal *)atom;
		_netatom_deser_uri(netatom, &lit->body.datatype);
		_netatom_deser_uri(netatom, &lit->body.lang);
	}
	else if(atom->type == forge->Object)
	{
		LV2_Atom_Object *obj = (LV2_Atom_Object *)atom;
		_netatom_deser_uri(netatom, &obj->body.id);
		_netatom_deser_uri(netatom, &obj->body.otype);
		LV2_ATOM_OBJECT_FOREACH(obj, prop)
		{
			_netatom_deser_uri(netatom, &prop->key);
			_netatom_deser_uri(netatom, &prop->context);
			_netatom_deser_atom(netatom, &prop->value);
		}
	}
	else if(atom->type == forge->Tuple)
	{
		LV2_Atom_Tuple *tup = (LV2_Atom_Tuple *)atom;
		LV2_ATOM_TUPLE_FOREACH(tup, item)
		{
			_netatom_deser_atom(netatom, item);
		}
	}
	else if(atom->type == forge->Sequence)
	{
		LV2_Atom_Sequence *seq = (LV2_Atom_Sequence *)atom;
		_netatom_deser_uri(netatom, &seq->body.unit);
		if(netatom->swap)
			seq->body.pad = be32toh(seq->body.pad);
		LV2_ATOM_SEQUENCE_FOREACH(seq, ev)
		{
			_netatom_deser_atom(netatom, &ev->body);
			if(netatom->swap)
				ev->time.frames = be64toh(ev->time.frames);
		}
	}
	else if(atom->type == forge->Vector)
	{
		LV2_Atom_Vector *vec = (LV2_Atom_Vector *)atom;
		_netatom_deser_uri(netatom, &vec->body.child_type);
		if(netatom->swap)
		{
			vec->body.child_size = be32toh(vec->body.child_size);
			if(vec->body.child_size == 4)
			{
				const unsigned n = (vec->atom.size - sizeof(LV2_Atom_Vector_Body)) / 4;
				uint32_t *u = LV2_ATOM_CONTENTS(LV2_Atom_Vector, atom);
				for(unsigned i = 0; i < n; i++)
					u[i] = be32toh(u[i]);
			}
			else if(vec->body.child_size == 8)
			{
				const unsigned n = (vec->atom.size - sizeof(LV2_Atom_Vector_Body)) / 8;
				uint64_t *u = LV2_ATOM_CONTENTS(LV2_Atom_Vector, atom);
				for(unsigned i = 0; i < n; i++)
					u[i] = be64toh(u[i]);
			}
		}
	}
}

NETATOM_API uint8_t *
netatom_serialize(netatom_t *netatom, LV2_Atom *atom, size_t size_rx,
	size_t *size_tx)
{
	if(!netatom || !atom)
		return NULL;

	uint8_t *buf_rx = (uint8_t *)atom;
	const uint32_t tot_size = lv2_atom_pad_size(lv2_atom_total_size(atom));

	netatom->dict.buf = buf_rx + tot_size;
	netatom->dict.cur = netatom->dict.buf;
	netatom->dict.end = buf_rx + size_rx;

	netatom->overflow = false;

	_netatom_ser_atom(netatom, atom);
	_netatom_ser_dict(netatom);

	if(netatom->overflow)
		return NULL;

	const size_t size_dict = netatom->dict.cur - netatom->dict.buf;
	const size_t written = tot_size + size_dict;

	if(size_tx)
		*size_tx = written;

	return buf_rx;
}

NETATOM_API const LV2_Atom *
netatom_deserialize(netatom_t *netatom, uint8_t *buf_tx, size_t size_tx)
{
	if(!netatom || !buf_tx)
		return NULL;

	LV2_Atom *atom = (LV2_Atom *)buf_tx;
	const uint32_t size = netatom->swap
		? be32toh(atom->size)
		: atom->size;

	const uint32_t tot_size = lv2_atom_pad_size(sizeof(LV2_Atom) + size);

	const uint32_t dict_size = size_tx - tot_size;
	netatom->dict.buf = buf_tx + tot_size;
	netatom->dict.cur = netatom->dict.buf + dict_size;
	netatom->dict.end = buf_tx + size_tx;

	netatom->overflow = false;

	_netatom_deser_dict(netatom);
	_netatom_deser_atom(netatom, atom);

	return atom;
}

NETATOM_API netatom_t *
netatom_new(LV2_URID_Map *map, LV2_URID_Unmap *unmap, bool swap)
{
	netatom_t *netatom = calloc(1, sizeof(netatom_t));
	if(!netatom)
		return NULL;

	netatom->swap = swap;
	netatom->map = map;
	netatom->unmap = unmap;

	lv2_atom_forge_init(&netatom->forge, map);

	netatom->MIDI_MidiEvent = map->map(map->handle, LV2_MIDI__MidiEvent);

	return netatom;
}

NETATOM_API void
netatom_free(netatom_t *netatom)
{
	if(!netatom)
		return;

	free(netatom);
}

#endif // NETATOM_IMPLEMENTATION

#ifdef __cplusplus
}
#endif

#endif // _NETATOM_H
