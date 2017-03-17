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

#ifndef _LV2_PROPS_H_
#define _LV2_PROPS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdatomic.h>
#include <stdio.h>

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/patch/patch.h>
#include <lv2/lv2plug.in/ns/ext/state/state.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>

/*****************************************************************************
 * API START
 *****************************************************************************/

// enumerations
typedef enum _props_event_t props_event_t;

// structures
typedef struct _props_def_t props_def_t;
typedef struct _props_impl_t props_impl_t;
typedef struct _props_work_t props_work_t;
typedef struct _props_t props_t;

// function callbacks
typedef void (*props_event_cb_t)(
	void *data,
	LV2_Atom_Forge *forge,
	int64_t frames,
	props_event_t event,
	props_impl_t *impl);

enum _props_event_t {
	PROP_EVENT_GET				= (1 << 0),
	PROP_EVENT_SET				= (1 << 1),
	PROP_EVENT_SAVE				= (1 << 2),
	PROP_EVENT_RESTORE		= (1 << 3),
	PROP_EVENT_REGISTER		= (1 << 4)
};

#define PROP_EVENT_NONE		(0)
#define PROP_EVENT_READ		(PROP_EVENT_GET		| PROP_EVENT_SAVE)
#define PROP_EVENT_WRITE	(PROP_EVENT_SET		| PROP_EVENT_RESTORE)
#define PROP_EVENT_RW			(PROP_EVENT_READ	| PROP_EVENT_WRITE)
#define PROP_EVENT_ALL		(PROP_EVENT_RW		| PROP_EVENT_REGISTER)

struct _props_def_t {
	const char *property;
	const char *type;
	const char *access;
	size_t offset;

	uint32_t max_size;
	props_event_t event_mask;
	props_event_cb_t event_cb;
};

struct _props_impl_t {
	LV2_URID property;
	LV2_URID type;
	LV2_URID access;

	struct {
		uint32_t size;
		void *body;
	} value;
	struct {
		uint32_t size;
		void *body;
	} stash;

	const props_t *props;
	const props_def_t *def;

	atomic_flag lock;
	bool stashing;
};

struct _props_work_t {
	LV2_URID property;
	LV2_URID size;
	LV2_URID type;
	uint8_t body [];
};

struct _props_t {
	struct {
		LV2_URID subject;

		LV2_URID patch_get;
		LV2_URID patch_set;
		LV2_URID patch_put;
		LV2_URID patch_patch;
		LV2_URID patch_wildcard;
		LV2_URID patch_add;
		LV2_URID patch_remove;
		LV2_URID patch_subject;
		LV2_URID patch_body;
		LV2_URID patch_property;
		LV2_URID patch_value;
		LV2_URID patch_writable;
		LV2_URID patch_readable;
		LV2_URID patch_sequence;
		LV2_URID patch_error;
		LV2_URID patch_ack;

		LV2_URID atom_int;
		LV2_URID atom_long;
		LV2_URID atom_float;
		LV2_URID atom_double;
		LV2_URID atom_bool;
		LV2_URID atom_urid;
		LV2_URID atom_path;
		LV2_URID atom_literal;
		LV2_URID atom_vector;
		LV2_URID atom_object;
		LV2_URID atom_sequence;
	} urid;

	LV2_URID_Map *map;
	void *data;

	bool stashing;

	unsigned max_size;
	unsigned max_nimpls;
	unsigned nimpls;
	props_impl_t impls [0];
};

#define PROPS_T(PROPS, MAX_NIMPLS) \
	props_t (PROPS); \
	props_impl_t _impls [(MAX_NIMPLS)];

// rt-safe
static inline int
props_init(props_t *props, const size_t max_nimpls, const char *subject,
	LV2_URID_Map *map, void *data);

// rt-safe
static inline int
props_register(props_t *props, const props_def_t *defs, int num,
	void *value_base, void *stash_base);

// rt-safe
static inline int
props_advance(props_t *props, LV2_Atom_Forge *forge, uint32_t frames,
	const LV2_Atom_Object *obj, LV2_Atom_Forge_Ref *ref);

// rt-safe
static inline void
props_set(props_t *props, LV2_Atom_Forge *forge, uint32_t frames, LV2_URID property,
	LV2_Atom_Forge_Ref *ref);

// rt-safe
static inline void
props_stash(props_t *props, LV2_URID property);

// rt-safe
static inline LV2_URID
props_map(props_t *props, const char *property);

// rt-safe
static inline const char *
props_unmap(props_t *props, LV2_URID property);

// non-rt
static inline LV2_State_Status
props_save(props_t *props, LV2_State_Store_Function store,
	LV2_State_Handle state, uint32_t flags, const LV2_Feature *const *features);

// non-rt
static inline LV2_State_Status
props_restore(props_t *props, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags, const LV2_Feature *const *features);

// non-rt
static inline LV2_Worker_Status
props_work(props_t *props, LV2_Worker_Respond_Function respond,
LV2_Worker_Respond_Handle handle, uint32_t size, const void *data);

// rt-safe
static inline LV2_Worker_Status
props_work_response(props_t *props, uint32_t size, const void *body);

/*****************************************************************************
 * API END
 *****************************************************************************/

static inline void
_impl_spin_lock(props_impl_t *impl)
{
	while(atomic_flag_test_and_set_explicit(&impl->lock, memory_order_acquire))
	{
		// spin
	}
}

static inline bool
_impl_try_lock(props_impl_t *impl)
{
	return atomic_flag_test_and_set_explicit(&impl->lock, memory_order_acquire) == false;
}

static inline void
_impl_unlock(props_impl_t *impl)
{
	atomic_flag_clear_explicit(&impl->lock, memory_order_release);
}

static inline void
_impl_qsort(props_impl_t *A, int n)
{
	if(n < 2)
		return;

	const props_impl_t *p = A;

	int i = -1;
	int j = n;

	while(true)
	{
		do {
			i += 1;
		} while(A[i].property < p->property);

		do {
			j -= 1;
		} while(A[j].property > p->property);

		if(i >= j)
			break;

		const props_impl_t tmp = A[i];
		A[i] = A[j];
		A[j] = tmp;
	}

	_impl_qsort(A, j + 1);
	_impl_qsort(A + j + 1, n - j - 1);
}

static inline props_impl_t *
_impl_bsearch(LV2_URID p, props_impl_t *a, int n)
{
	props_impl_t *base = a;

	for(int N = n, half; N > 1; N -= half)
	{
		half = N/2;
		props_impl_t *dst = &base[half];
		base = (dst->property > p) ? base : dst;
	}

	return (base->property == p) ? base : NULL;
}

static inline props_impl_t *
_props_impl_search(props_t *props, LV2_URID property)
{
	return _impl_bsearch(property, props->impls, props->nimpls);
}

static inline LV2_Atom_Forge_Ref
_props_get(props_t *props, LV2_Atom_Forge *forge, uint32_t frames,
	props_impl_t *impl, int32_t sequence_num)
{
	LV2_Atom_Forge_Frame obj_frame;

	LV2_Atom_Forge_Ref ref = lv2_atom_forge_frame_time(forge, frames);

	if(ref)
		ref = lv2_atom_forge_object(forge, &obj_frame, 0, props->urid.patch_set);
	{
		if(props->urid.subject) // is optional
		{
			if(ref)
				ref = lv2_atom_forge_key(forge, props->urid.patch_subject);
			if(ref)
				ref = lv2_atom_forge_urid(forge, props->urid.subject);
		}

		if(sequence_num) // is optional
		{
			if(ref)
				ref = lv2_atom_forge_key(forge, props->urid.patch_sequence);
			if(ref)
				ref = lv2_atom_forge_int(forge, sequence_num);
		}

		if(ref)
			ref = lv2_atom_forge_key(forge, props->urid.patch_property);
		if(ref)
			ref = lv2_atom_forge_urid(forge, impl->property);

		if(ref)
			lv2_atom_forge_key(forge, props->urid.patch_value);
		if(ref)
			ref = lv2_atom_forge_atom(forge, impl->value.size, impl->type);
		if(ref)
			ref = lv2_atom_forge_write(forge, impl->value.body, impl->value.size);
	}
	if(ref)
		lv2_atom_forge_pop(forge, &obj_frame);

	return ref;
}

static inline LV2_Atom_Forge_Ref
_props_error(props_t *props, LV2_Atom_Forge *forge, uint32_t frames, int32_t sequence_num)
{
	LV2_Atom_Forge_Frame obj_frame;

	LV2_Atom_Forge_Ref ref = lv2_atom_forge_frame_time(forge, frames);

	if(ref)
		ref = lv2_atom_forge_object(forge, &obj_frame, 0, props->urid.patch_error);
	{
		if(ref)
			ref = lv2_atom_forge_key(forge, props->urid.patch_sequence);
		if(ref)
			ref = lv2_atom_forge_int(forge, sequence_num);
	}
	if(ref)
		lv2_atom_forge_pop(forge, &obj_frame);

	return ref;
}

static inline LV2_Atom_Forge_Ref
_props_ack(props_t *props, LV2_Atom_Forge *forge, uint32_t frames, int32_t sequence_num)
{
	LV2_Atom_Forge_Frame obj_frame;

	LV2_Atom_Forge_Ref ref = lv2_atom_forge_frame_time(forge, frames);

	if(ref)
		ref = lv2_atom_forge_object(forge, &obj_frame, 0, props->urid.patch_ack);
	{
		if(ref)
			ref = lv2_atom_forge_key(forge, props->urid.patch_sequence);
		if(ref)
			ref = lv2_atom_forge_int(forge, sequence_num);
	}
	if(ref)
		lv2_atom_forge_pop(forge, &obj_frame);

	return ref;
}

static inline void
_props_stash(props_t *props, props_impl_t *impl)
{
	if(_impl_try_lock(impl))
	{
		impl->stash.size = impl->value.size;
		memcpy(impl->stash.body, impl->value.body, impl->value.size);

		_impl_unlock(impl);
	}
	else
	{
		impl->stashing = true;
		props->stashing = true;
	}
}

static inline void
_props_set(props_t *props, props_impl_t *impl, LV2_URID type, uint32_t size, const void *body)
{
	if(  (impl->type == type)
		&& ( (impl->def->max_size == 0) || (size <= impl->def->max_size)) )
	{
		impl->value.size = size;
		memcpy(impl->value.body, body, size);

		_props_stash(props, impl);
	}
}

static inline int
_props_register_single(props_t *props, const props_def_t *def,
	void *value_base, void *stash_base)
{
	if(props->nimpls >= props->max_nimpls)
		return 0;

	if(!def->property || !def->type)
		return 0;

	const LV2_URID type = props->map->map(props->map->handle, def->type);
	const LV2_URID property = props->map->map(props->map->handle, def->property);
	const LV2_URID access = def->access
		? props->map->map(props->map->handle, def->access)
		: props->map->map(props->map->handle, LV2_PATCH__writable);

	if(!type || !property || !access)
		return 0;

	props_impl_t *impl = &props->impls[props->nimpls++];

	impl->props = props;
	impl->property = property;
	impl->access = access;
	impl->def = def;
	impl->value.body = value_base + def->offset;
	impl->stash.body = stash_base + def->offset;

	uint32_t size;
	if(  (type == props->urid.atom_int)
		|| (type == props->urid.atom_float)
		|| (type == props->urid.atom_bool)
		|| (type == props->urid.atom_urid) )
	{
		size = 4;
	}
	else if((type == props->urid.atom_long)
		|| (type == props->urid.atom_double) )
	{
		size = 8;
	}
	else if(type == props->urid.atom_literal)
	{
		size = sizeof(LV2_Atom_Literal_Body);
	}
	else if(type == props->urid.atom_vector)
	{
		size = sizeof(LV2_Atom_Vector_Body);
	}
	else if(type == props->urid.atom_object)
	{
		size = sizeof(LV2_Atom_Object_Body);
	}
	else if(type == props->urid.atom_sequence)
	{
		size = sizeof(LV2_Atom_Sequence_Body);
	}
	else
	{
		size = 0; // assume everything else as having size 0
	}

	impl->type = type;
	impl->value.size = impl->stash.size = size;

	atomic_flag_clear_explicit(&impl->lock, memory_order_relaxed);

	// update maximal value size
	const uint32_t max_size = def->max_size
		? def->max_size
		: size;

	if(max_size > props->max_size)
	{
		props->max_size = max_size;
	}

	//TODO register?

	return 1;
}

static inline int
props_init(props_t *props, const size_t max_nimpls, const char *subject,
	LV2_URID_Map *map, void *data)
{
	if(!map)
		return 0;

	props->nimpls = 0;
	props->max_nimpls = max_nimpls;
	props->map = map;
	props->data = data;

	props->urid.subject = subject ? map->map(map->handle, subject) : 0;

	props->urid.patch_get = map->map(map->handle, LV2_PATCH__Get);
	props->urid.patch_set = map->map(map->handle, LV2_PATCH__Set);
	props->urid.patch_put = map->map(map->handle, LV2_PATCH__Put);
	props->urid.patch_patch = map->map(map->handle, LV2_PATCH__Patch);
	props->urid.patch_wildcard = map->map(map->handle, LV2_PATCH__wildcard);
	props->urid.patch_add = map->map(map->handle, LV2_PATCH__add);
	props->urid.patch_remove = map->map(map->handle, LV2_PATCH__remove);
	props->urid.patch_subject = map->map(map->handle, LV2_PATCH__subject);
	props->urid.patch_body = map->map(map->handle, LV2_PATCH__body);
	props->urid.patch_property = map->map(map->handle, LV2_PATCH__property);
	props->urid.patch_value = map->map(map->handle, LV2_PATCH__value);
	props->urid.patch_writable = map->map(map->handle, LV2_PATCH__writable);
	props->urid.patch_readable = map->map(map->handle, LV2_PATCH__readable);
	props->urid.patch_sequence = map->map(map->handle, LV2_PATCH__sequenceNumber);
	props->urid.patch_ack = map->map(map->handle, LV2_PATCH__Ack);
	props->urid.patch_error = map->map(map->handle, LV2_PATCH__Error);

	props->urid.atom_int = map->map(map->handle, LV2_ATOM__Int);
	props->urid.atom_long = map->map(map->handle, LV2_ATOM__Long);
	props->urid.atom_float = map->map(map->handle, LV2_ATOM__Float);
	props->urid.atom_double = map->map(map->handle, LV2_ATOM__Double);
	props->urid.atom_bool = map->map(map->handle, LV2_ATOM__Bool);
	props->urid.atom_urid = map->map(map->handle, LV2_ATOM__URID);
	props->urid.atom_path = map->map(map->handle, LV2_ATOM__Path);
	props->urid.atom_literal = map->map(map->handle, LV2_ATOM__Literal);
	props->urid.atom_vector = map->map(map->handle, LV2_ATOM__Vector);
	props->urid.atom_object = map->map(map->handle, LV2_ATOM__Object);
	props->urid.atom_sequence = map->map(map->handle, LV2_ATOM__Sequence);

	return 1;
}

static inline int
props_register(props_t *props, const props_def_t *defs, int num,
	void *value_base, void *stash_base)
{
	if(!defs || !value_base || !stash_base)
		return 0;

	int status = 1;
	for(int i = 0; i < num; i++)
	{
		status = status &&  _props_register_single(props, &defs[i], value_base, stash_base);
	}

	_impl_qsort(props->impls, props->nimpls);

	return status;
}

static inline int
props_advance(props_t *props, LV2_Atom_Forge *forge, uint32_t frames,
	const LV2_Atom_Object *obj, LV2_Atom_Forge_Ref *ref)
{
	if(props->stashing)
	{
		props->stashing = false;

		for(unsigned i = 0; i < props->nimpls; i++)
		{
			props_impl_t *impl = &props->impls[i];

			if(impl->stashing)
			{
				impl->stashing= false;
				_props_stash(props, impl);
			}
		}
	}

	if(!lv2_atom_forge_is_object_type(forge, obj->atom.type))
	{
		return 0;
	}

	if(obj->body.otype == props->urid.patch_get)
	{
		const LV2_Atom_URID *subject = NULL;
		const LV2_Atom_URID *property = NULL;
		const LV2_Atom_Int *sequence = NULL;

		LV2_Atom_Object_Query q [] = {
			{ props->urid.patch_subject, (const LV2_Atom **)&subject },
			{ props->urid.patch_property, (const LV2_Atom **)&property },
			{ props->urid.patch_sequence, (const LV2_Atom **)&sequence },
			LV2_ATOM_OBJECT_QUERY_END
		};
		lv2_atom_object_query(obj, q);

		// check for a matching optional subject
		if(  (subject && props->urid.subject)
			&& ( (subject->atom.type != props->urid.atom_urid)
				|| (subject->body != props->urid.subject) ) )
		{
			return 0;
		}

		int32_t sequence_num = 0;
		if(sequence && (sequence->atom.type == props->urid.atom_int))
		{
			sequence_num = sequence->body;
		}

		if(!property)
		{
			for(unsigned i = 0; i < props->nimpls; i++)
			{
				props_impl_t *impl = &props->impls[i];
				const props_def_t *def = impl->def;

				if(*ref)
					*ref = _props_get(props, forge, frames, impl, sequence_num);
				if(def->event_cb && (def->event_mask & PROP_EVENT_GET) )
					def->event_cb(props->data, forge, frames, PROP_EVENT_GET, impl);
			}

			return 1;
		}
		else if(property->atom.type == props->urid.atom_urid)
		{
			props_impl_t *impl = _props_impl_search(props, property->body);

			if(impl)
			{
				*ref = _props_get(props, forge, frames, impl, sequence_num);

				const props_def_t *def = impl->def;
				if(def->event_cb && (def->event_mask & PROP_EVENT_GET) )
					def->event_cb(props->data, forge, frames, PROP_EVENT_GET, impl);

				return 1;
			}
			else if(sequence_num)
			{
				*ref = _props_error(props, forge, frames, sequence_num);
			}
		}
		else if(sequence_num)
		{
			*ref = _props_error(props, forge, frames, sequence_num);
		}
	}
	else if(obj->body.otype == props->urid.patch_set)
	{
		const LV2_Atom_URID *subject = NULL;
		const LV2_Atom_URID *property = NULL;
		const LV2_Atom_Int *sequence = NULL;
		const LV2_Atom *value = NULL;

		LV2_Atom_Object_Query q [] = {
			{ props->urid.patch_subject, (const LV2_Atom **)&subject },
			{ props->urid.patch_property, (const LV2_Atom **)&property },
			{ props->urid.patch_sequence, (const LV2_Atom **)&sequence },
			{ props->urid.patch_value, &value },
			LV2_ATOM_OBJECT_QUERY_END
		};
		lv2_atom_object_query(obj, q);

		// check for a matching optional subject
		if(  (subject && props->urid.subject)
			&& ( (subject->atom.type != props->urid.atom_urid)
				|| (subject->body != props->urid.subject) ) )
		{
			return 0;
		}

		int32_t sequence_num = 0;
		if(sequence && (sequence->atom.type == props->urid.atom_int))
		{
			sequence_num = sequence->body;
		}

		if(!property || (property->atom.type != props->urid.atom_urid) || !value)
		{
			if(sequence_num)
			{
				*ref = _props_error(props, forge, frames, sequence_num);
			}

			return 0;
		}

		props_impl_t *impl = _props_impl_search(props, property->body);
		if(impl && (impl->access == props->urid.patch_writable) )
		{
			_props_set(props, impl, value->type, value->size, LV2_ATOM_BODY_CONST(value));

			const props_def_t *def = impl->def;
			if(def->event_cb && (def->event_mask & PROP_EVENT_SET) )
				def->event_cb(props->data, forge, frames, PROP_EVENT_SET, impl);

			if(sequence_num)
			{
				*ref = _props_ack(props, forge, frames, sequence_num);
			}

			return 1;
		}
		else if(sequence_num)
		{
			*ref = _props_error(props, forge, frames, sequence_num);
		}
	}
	else if(obj->body.otype == props->urid.patch_put)
	{
		const LV2_Atom_URID *subject = NULL;
		const LV2_Atom_Int *sequence = NULL;
		const LV2_Atom_Object *body = NULL;

		LV2_Atom_Object_Query q [] = {
			{ props->urid.patch_subject, (const LV2_Atom **)&subject },
			{ props->urid.patch_sequence, (const LV2_Atom **)&sequence},
			{ props->urid.patch_body, (const LV2_Atom **)&body },
			LV2_ATOM_OBJECT_QUERY_END
		};
		lv2_atom_object_query(obj, q);

		// check for a matching optional subject
		if(  (subject && props->urid.subject)
			&& ( (subject->atom.type != props->urid.atom_urid)
				|| (subject->body != props->urid.subject) ) )
		{
			return 0;
		}

		int32_t sequence_num = 0;
		if(sequence && (sequence->atom.type == props->urid.atom_int))
		{
			sequence_num = sequence->body;
		}

		if(!body || !lv2_atom_forge_is_object_type(forge, body->atom.type))
		{
			if(sequence_num)
			{
				*ref = _props_error(props, forge, frames, sequence_num);
			}

			return 0;
		}

		LV2_ATOM_OBJECT_FOREACH(body, prop)
		{
			const LV2_URID property = prop->key;
			const LV2_Atom *value = &prop->value;

			props_impl_t *impl = _props_impl_search(props, property);
			if(impl && (impl->access == props->urid.patch_writable) )
			{
				_props_set(props, impl, value->type, value->size, LV2_ATOM_BODY_CONST(value));

				const props_def_t *def = impl->def;
				if(def->event_cb && (def->event_mask & PROP_EVENT_SET) )
					def->event_cb(props->data, forge, frames, PROP_EVENT_SET, impl);
			}
		}

		if(sequence_num)
		{
			*ref = _props_ack(props, forge, frames, sequence_num);
		}

		return 1;
	}

	return 0; // did not handle a patch event
}

static inline void
props_set(props_t *props, LV2_Atom_Forge *forge, uint32_t frames, LV2_URID property,
	LV2_Atom_Forge_Ref *ref)
{
	props_impl_t *impl = _props_impl_search(props, property);

	if(impl)
	{
		_props_stash(props, impl);
		if(*ref)
			*ref = _props_get(props, forge, frames, impl, 0); //TODO use patch:sequenceNumber
	}
}

static inline void
props_stash(props_t *props, LV2_URID property)
{
	props_impl_t *impl = _props_impl_search(props, property);

	if(impl)
		_props_stash(props, impl);
}

static inline LV2_URID
props_map(props_t *props, const char *property)
{
	for(unsigned i = 0; i < props->nimpls; i++)
	{
		props_impl_t *impl = &props->impls[i];

		if(!strcmp(impl->def->property, property))
			return impl->property;
	}

	return 0;
}

static inline const char *
props_unmap(props_t *props, LV2_URID property)
{
	props_impl_t *impl = _props_impl_search(props, property);

	if(impl)
		return impl->def->property;

	return NULL;
}

static inline LV2_State_Status
props_save(props_t *props, LV2_State_Store_Function store,
	LV2_State_Handle state, uint32_t flags, const LV2_Feature *const *features)
{
	const LV2_State_Map_Path *map_path = NULL;

	// set POD flag if not already set by host
	flags |= LV2_STATE_IS_POD;

	for(unsigned i = 0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_STATE__mapPath))
		{
			map_path = features[i]->data;
			break;
		}
	}

	void *body = malloc(props->max_size); // create memory to store widest value
	if(body)
	{
		for(unsigned i = 0; i < props->nimpls; i++)
		{
			props_impl_t *impl = &props->impls[i];

			if(impl->access == props->urid.patch_readable)
				continue; // skip read-only, as it makes no sense to restore them

			// create lockfree copy of value, store() may well be blocking
			_impl_spin_lock(impl);

			const uint32_t size = impl->stash.size;
			const LV2_URID type = impl->type;
			memcpy(body, impl->stash.body, impl->stash.size);

			_impl_unlock(impl);

			if( map_path && (type == props->urid.atom_path) )
			{
				const char *path = strstr(body, "file://")
					? body + 7 // skip "file://"
					: body;
				char *abstract = map_path->abstract_path(map_path->handle, path);
				if(abstract && strcmp(abstract, path))
				{
					store(state, impl->property, abstract, strlen(abstract) + 1, type, flags);
					free(abstract);
				}
			}
			else // !Path
			{
				store(state, impl->property, body, size, type, flags);
			}

			const props_def_t *def = impl->def;
			if(def->event_cb && (def->event_mask & PROP_EVENT_SAVE) )
				def->event_cb(props->data, NULL, 0, PROP_EVENT_SAVE, impl);
		}

		free(body);
	}

	return LV2_STATE_SUCCESS;
}

static inline void
_props_schedule(const LV2_Worker_Schedule *work_sched, LV2_URID property,
	LV2_URID type, uint32_t size, const void *body)
{
	const uint32_t sz = sizeof(props_work_t) + size;
	props_work_t *job = malloc(sz);
	if(job)
	{
		job->property = property;
		job->type = type;
		job->size = size;
		memcpy(job->body, body, size);

		work_sched->schedule_work(work_sched->handle, sz, job);

		free(job);
	}
}

static inline LV2_State_Status
props_restore(props_t *props, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags, const LV2_Feature *const *features)
{
	const LV2_State_Map_Path *map_path = NULL;
	const LV2_Worker_Schedule *work_sched = NULL;

	for(unsigned i = 0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_STATE__mapPath))
			map_path = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_WORKER__schedule))
			work_sched = features[i]->data;
	}

	for(unsigned i = 0; i < props->nimpls; i++)
	{
		props_impl_t *impl = &props->impls[i];

		if(impl->access == props->urid.patch_readable)
			continue; // skip read-only, as it makes no sense to restore them

		size_t _size;
		uint32_t _type;
		uint32_t _flags;
		const void *body = retrieve(state, impl->property, &_size, &_type, &_flags);

		if(body)
		{
			if( map_path && (_type == props->urid.atom_path) )
			{
				char *absolute = map_path->absolute_path(map_path->handle, body);
				if(absolute)
				{
					const uint32_t sz = strlen(absolute) + 1;
					if(work_sched) // host supports state:threadSafeRestore
					{
						_props_schedule(work_sched, impl->property, _type, sz, absolute);
					}
					else
					{
						_props_set(props, impl, _type, sz, absolute);

						const props_def_t *def = impl->def;
						if(def->event_cb && (def->event_mask & PROP_EVENT_RESTORE) )
							def->event_cb(props->data, NULL, 0, PROP_EVENT_RESTORE, impl);
					}

					free(absolute);
				}
			}
			else // !Path
			{
				if(work_sched) // host supports state:threadSafeRestore
				{
					_props_schedule(work_sched, impl->property, _type, _size, body);
				}
				else
				{
					_props_set(props, impl, _type, _size, body);

					const props_def_t *def = impl->def;
					if(def->event_cb && (def->event_mask & PROP_EVENT_RESTORE) )
						def->event_cb(props->data, NULL, 0, PROP_EVENT_RESTORE, impl);
				}
			}
		}
		else
		{
			fprintf(stderr, "props_restore: no property '%s'.\n", impl->def->property);
		}
	}

	return LV2_STATE_SUCCESS;
}

static inline LV2_Worker_Status
props_work(props_t *props, LV2_Worker_Respond_Function respond,
LV2_Worker_Respond_Handle worker, uint32_t size, const void *body)
{
	return respond(worker, size, body);
}

static inline LV2_Worker_Status
props_work_response(props_t *props, uint32_t size, const void *body)
{
	const props_work_t *job = body;
	if(job)
	{
		props_impl_t *impl = _props_impl_search(props, job->property);
		if(impl)
		{
			_props_set(props, impl, job->type, job->size, job->body);

			const props_def_t *def = impl->def;
			if(def->event_cb && (def->event_mask & PROP_EVENT_RESTORE) )
				def->event_cb(props->data, NULL, 0, PROP_EVENT_RESTORE, impl);
		}
	}

	return LV2_WORKER_SUCCESS;
}

#ifdef __cplusplus
}
#endif

#endif // _LV2_PROPS_H_
