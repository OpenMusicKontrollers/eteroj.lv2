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
#include <lv2/lv2plug.in/ns/extensions/units/units.h>

/*****************************************************************************
 * API START
 *****************************************************************************/

// definitions
#define PROPS_TYPE_N 10

// unions
typedef union _props_raw_t props_raw_t;

// enumerations
typedef enum _props_mode_t props_mode_t;
typedef enum _props_event_t props_event_t;

// structures
typedef struct _props_scale_point_t props_scale_point_t;
typedef struct _props_def_t props_def_t;
typedef struct _props_type_t props_type_t;
typedef struct _props_impl_t props_impl_t;
typedef struct _props_t props_t;

// function callbacks
typedef void (*props_event_cb_t)(
	void *data,
	LV2_Atom_Forge *forge,
	int64_t frames,
	props_event_t event,
	props_impl_t *impl);

typedef uint32_t (*props_type_size_cb_t)(
	const void *value);

typedef LV2_Atom_Forge_Ref (*props_type_get_cb_t)(
	LV2_Atom_Forge *forge,
	const void *value);

typedef void (*props_type_set_cb_t)(
	props_impl_t *impl,
	void *value,
	LV2_URID new_type,
	uint32_t sz,
	const void *new_value);

union _props_raw_t {
	const int32_t i;			// Int
	const int64_t h;			// Long
	const float f;				// Float
	const double d;				// Double
	const int32_t b;			// Bool
	const uint32_t u;			// URID
	//TODO more types
};

enum _props_mode_t {
	PROP_MODE_STATIC			= 0,
	PROP_MODE_DYNAMIC			= 1
};

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

struct _props_scale_point_t {
	const char *label;
	props_raw_t value;
};

struct _props_def_t {
	const char *property;
	const char *type;
	const char *access;
	const char *unit;
	props_mode_t mode;
	props_event_t event_mask;
	props_event_cb_t event_cb;
	uint32_t max_size;

	const char *label;
	const char *comment;
	props_raw_t minimum;
	props_raw_t maximum;
	const props_scale_point_t *scale_points;
};

struct _props_type_t {
	LV2_URID urid;
	uint32_t size;
	props_type_size_cb_t size_cb;
	props_type_get_cb_t get_cb;
	props_type_set_cb_t set_cb;
};

struct _props_impl_t {
	LV2_URID property;
	LV2_URID access;
	LV2_URID unit;
	const props_t *props;
	const props_type_t *type;
	const props_def_t *def;
	void *value;
	void *stash;
	atomic_flag lock;
	bool stashing;
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

		LV2_URID rdf_value;

		LV2_URID rdfs_label;
		LV2_URID rdfs_range;
		LV2_URID rdfs_comment;

		LV2_URID lv2_minimum;
		LV2_URID lv2_maximum;
		LV2_URID lv2_scale_point;

		LV2_URID atom_int;
		LV2_URID atom_long;
		LV2_URID atom_float;
		LV2_URID atom_double;
		LV2_URID atom_bool;
		LV2_URID atom_urid;
		LV2_URID atom_string;
		LV2_URID atom_path;
		LV2_URID atom_uri;
		LV2_URID atom_chunk;

		LV2_URID units_unit;
	} urid;

	LV2_URID_Map *map;
	void *data;

	props_type_t types [PROPS_TYPE_N];

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
static inline LV2_URID
props_register(props_t *props, const props_def_t *def, void *value, void *stash);

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

// non-rt
static inline LV2_State_Status
props_save(props_t *props, LV2_Atom_Forge *forge, LV2_State_Store_Function store,
	LV2_State_Handle state, uint32_t flags, const LV2_Feature *const *features);

// non-rt
static inline LV2_State_Status
props_restore(props_t *props, LV2_Atom_Forge *forge, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags, const LV2_Feature *const *features);

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

static inline uint32_t
_impl_size_get(props_impl_t *impl)
{
	return impl->type->size_cb
		? impl->type->size_cb(impl->value)
		: impl->type->size;
}

static LV2_Atom_Forge_Ref
_props_int_get_cb(LV2_Atom_Forge *forge, const void *value)
{
	return lv2_atom_forge_int(forge, *(const int32_t *)value);
}
static LV2_Atom_Forge_Ref
_props_bool_get_cb(LV2_Atom_Forge *forge, const void *value)
{
	return lv2_atom_forge_bool(forge, *(const int32_t *)value);
}
static void
_props_int_set_cb(props_impl_t *impl, void *value,
	LV2_URID new_type, uint32_t sz, const void *new_value)
{
	const props_t *props = impl->props;
	int32_t *ref = value;

	if(new_type == props->urid.atom_int)
		*ref = *(const int32_t *)new_value;
	else if(new_type == props->urid.atom_bool)
		*ref = *(const int32_t *)new_value;
	else if(new_type == props->urid.atom_urid)
		*ref = *(const uint32_t *)new_value;
	else if(new_type == props->urid.atom_long)
		*ref = *(const int64_t *)new_value;

	else if(new_type == props->urid.atom_float)
		*ref = *(const float *)new_value;
	else if(new_type == props->urid.atom_double)
		*ref = *(const double *)new_value;
}

static LV2_Atom_Forge_Ref
_props_long_get_cb(LV2_Atom_Forge *forge, const void *value)
{
	return lv2_atom_forge_long(forge, *(const int64_t *)value);
}
static void
_props_long_set_cb(props_impl_t *impl, void *value,
	LV2_URID new_type, uint32_t sz, const void *new_value)
{
	const props_t *props = impl->props;
	int64_t *ref = value;

	if(new_type == props->urid.atom_long)
		*ref = *(const int64_t *)new_value;
	else if(new_type == props->urid.atom_int)
		*ref = *(const int32_t *)new_value;
	else if(new_type == props->urid.atom_bool)
		*ref = *(const int32_t *)new_value;
	else if(new_type == props->urid.atom_urid)
		*ref = *(const uint32_t *)new_value;

	else if(new_type == props->urid.atom_float)
		*ref = *(const float *)new_value;
	else if(new_type == props->urid.atom_double)
		*ref = *(const double *)new_value;
}

static LV2_Atom_Forge_Ref
_props_float_get_cb(LV2_Atom_Forge *forge, const void *value)
{
	return lv2_atom_forge_float(forge, *(const float *)value);
}
static void
_props_float_set_cb(props_impl_t *impl, void *value,
	LV2_URID new_type, uint32_t sz, const void *new_value)
{
	const props_t *props = impl->props;
	float *ref = value;

	if(new_type == props->urid.atom_float)
		*ref = *(const float *)new_value;
	else if(new_type == props->urid.atom_double)
		*ref = *(const double *)new_value;

	else if(new_type == props->urid.atom_int)
		*ref = *(const int32_t *)new_value;
	else if(new_type == props->urid.atom_bool)
		*ref = *(const int32_t *)new_value;
	else if(new_type == props->urid.atom_urid)
		*ref = *(const uint32_t *)new_value;
	else if(new_type == props->urid.atom_long)
		*ref = *(const int64_t *)new_value;
}

static LV2_Atom_Forge_Ref
_props_double_get_cb(LV2_Atom_Forge *forge, const void *value)
{
	return lv2_atom_forge_double(forge, *(const double *)value);
}
static void
_props_double_set_cb(props_impl_t *impl, void *value,
	LV2_URID new_type, uint32_t sz, const void *new_value)
{
	const props_t *props = impl->props;
	double *ref = value;

	if(new_type == props->urid.atom_double)
		*ref = *(const double *)new_value;
	else if(new_type == props->urid.atom_float)
		*ref = *(const float *)new_value;

	else if(new_type == props->urid.atom_int)
		*ref = *(const int32_t *)new_value;
	else if(new_type == props->urid.atom_bool)
		*ref = *(const int32_t *)new_value;
	else if(new_type == props->urid.atom_urid)
		*ref = *(const uint32_t *)new_value;
	else if(new_type == props->urid.atom_long)
		*ref = *(const int64_t *)new_value;
}

static LV2_Atom_Forge_Ref
_props_urid_get_cb(LV2_Atom_Forge *forge, const void *value)
{
	return lv2_atom_forge_urid(forge, *(const uint32_t *)value);
}
static void
_props_urid_set_cb(props_impl_t *impl, void *value,
	LV2_URID new_type, uint32_t sz, const void *new_value)
{
	const props_t *props = impl->props;
	uint32_t *ref = value;

	if(new_type == props->urid.atom_urid)
		*ref = *(const uint32_t *)new_value;

	else if(new_type == props->urid.atom_int)
		*ref = *(const int32_t *)new_value;
	else if(new_type == props->urid.atom_long)
		*ref = *(const int64_t *)new_value;
	else if(new_type == props->urid.atom_bool)
		*ref = *(const int32_t *)new_value;
	else if(new_type == props->urid.atom_float)
		*ref = *(const float *)new_value;
	else if(new_type == props->urid.atom_double)
		*ref = *(const double *)new_value;
}

static uint32_t
_props_string_size_cb(const void *value)
{
	return strlen((const char *)value) + 1;
}
static LV2_Atom_Forge_Ref
_props_string_get_cb(LV2_Atom_Forge *forge, const void *value)
{
	return lv2_atom_forge_string(forge, (const char *)value, strlen((const char *)value));
}
static LV2_Atom_Forge_Ref
_props_path_get_cb(LV2_Atom_Forge *forge, const void *value)
{
	return lv2_atom_forge_path(forge, (const char *)value, strlen((const char *)value));
}
static LV2_Atom_Forge_Ref
_props_uri_get_cb(LV2_Atom_Forge *forge, const void *value)
{
	return lv2_atom_forge_uri(forge, (const char *)value, strlen((const char *)value));
}
static void
_props_string_set_cb(props_impl_t *impl, void *value,
	LV2_URID new_type, uint32_t sz, const void *new_value)
{
	const props_t *props = impl->props;

	if(  (new_type == props->urid.atom_string)
		|| (new_type == props->urid.atom_path)
		|| (new_type == props->urid.atom_uri) )
		strncpy((char *)value, (const char *)new_value, impl->def->max_size);
}

static uint32_t
_props_chunk_size_cb(const void *value)
{
	const uint32_t sz = *(uint32_t *)value;
	return sz;
}
static LV2_Atom_Forge_Ref
_props_chunk_get_cb(LV2_Atom_Forge *forge, const void *value)
{
	const uint32_t sz = *(uint32_t *)value;
	const uint8_t *src = value + sizeof(uint32_t);
	LV2_Atom_Forge_Ref ref;

	return (ref = lv2_atom_forge_atom(forge, sz, forge->Chunk))
		&& (ref = lv2_atom_forge_write(forge, src, sz));
}
static void
_props_chunk_set_cb(props_impl_t *impl, void *value,
	LV2_URID new_type, uint32_t sz, const void *new_value)
{
	const props_t *props = impl->props;

	if(new_type == props->urid.atom_chunk)
	{
		*(uint32_t *)value = sz; // set chunk size
		uint8_t *dst = value + sizeof(uint32_t);
		const uint32_t msz = sz < impl->def->max_size - sizeof(uint32_t)
			? sz
			: impl->def->max_size - sizeof(uint32_t);
		memcpy(dst, new_value, msz);
	}
}

static inline void
_type_qsort(props_type_t *a, unsigned n)
{
	if(n < 2)
		return;

	const props_type_t *p = &a[n/2];

	unsigned i, j;
	for(i=0, j=n-1; ; i++, j--)
	{
		while(a[i].urid < p->urid)
			i++;

		while(p->urid < a[j].urid)
			j--;

		if(i >= j)
			break;

		const props_type_t t = a[i];
		a[i] = a[j];
		a[j] = t;
	}

	_type_qsort(a, i);
	_type_qsort(&a[i], n - i);
}

static inline props_type_t *
_type_bsearch(LV2_URID p, props_type_t *a, unsigned n)
{
	props_type_t *base = a;

	for(unsigned N = n, half; N > 1; N -= half)
	{
		half = N/2;
		props_type_t *dst = &base[half];
		base = (dst->urid > p) ? base : dst;
	}

	return (base->urid == p) ? base : NULL;
}

static inline void
_impl_qsort(props_impl_t *a, unsigned n)
{
	if(n < 2)
		return;

	const props_impl_t *p = &a[n/2];

	unsigned i, j;
	for(i=0, j=n-1; ; i++, j--)
	{
		while(a[i].property < p->property)
			i++;

		while(p->property < a[j].property)
			j--;

		if(i >= j)
			break;

		const props_impl_t t = a[i];
		a[i] = a[j];
		a[j] = t;
	}

	_impl_qsort(a, i);
	_impl_qsort(&a[i], n - i);
}

static inline props_impl_t *
_impl_bsearch(LV2_URID p, props_impl_t *a, unsigned n)
{
	props_impl_t *base = a;

	for(unsigned N = n, half; N > 1; N -= half)
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
			ref = impl->type->get_cb(forge, impl->value);
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
		const uint32_t size = _impl_size_get(impl);
		memcpy(impl->stash, impl->value, size);

		_impl_unlock(impl);
	}
	else
	{
		impl->stashing = true;
		props->stashing= true;
	}
}

static inline void
_props_set(props_t *props, props_impl_t *impl, LV2_URID type, uint32_t sz, const void *value)
{
	impl->type->set_cb(impl, impl->value, type, sz, value);
	_props_stash(props, impl);
}

static inline LV2_Atom_Forge_Ref
_props_reg(props_t *props, LV2_Atom_Forge *forge, uint32_t frames,
	props_impl_t *impl, int32_t sequence_num)
{
	const props_def_t *def = impl->def;
	LV2_Atom_Forge_Frame obj_frame;
	LV2_Atom_Forge_Frame add_frame;
	LV2_Atom_Forge_Frame remove_frame;

	LV2_Atom_Forge_Ref ref = lv2_atom_forge_frame_time(forge, frames);
	if(ref)
		ref = lv2_atom_forge_object(forge, &obj_frame, 0, props->urid.patch_patch);
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
			ref = lv2_atom_forge_key(forge, props->urid.patch_remove);
		if(ref)
			ref = lv2_atom_forge_object(forge, &remove_frame, 0, 0);
		{
			if(ref)
				ref = lv2_atom_forge_key(forge, impl->access);
			if(ref)
				ref = lv2_atom_forge_urid(forge, impl->property);
		}
		if(ref)
			lv2_atom_forge_pop(forge, &remove_frame);

		if(ref)
			ref = lv2_atom_forge_key(forge, props->urid.patch_add);
		if(ref)
			ref = lv2_atom_forge_object(forge, &add_frame, 0, 0);
		{
			if(ref)
				ref = lv2_atom_forge_key(forge, impl->access);
			if(ref)
				ref = lv2_atom_forge_urid(forge, impl->property);
		}
		if(ref)
			lv2_atom_forge_pop(forge, &add_frame);
	}
	if(ref)
		lv2_atom_forge_pop(forge, &obj_frame);

	if(ref)
		ref = lv2_atom_forge_frame_time(forge, frames);
	if(ref)
		ref = lv2_atom_forge_object(forge, &obj_frame, 0, props->urid.patch_patch);
	{
		if(ref)
			ref = lv2_atom_forge_key(forge, props->urid.patch_subject);
		if(ref)
			ref = lv2_atom_forge_urid(forge, impl->property);

		if(sequence_num) // is optional
		{
			if(ref)
				ref = lv2_atom_forge_key(forge, props->urid.patch_sequence);
			if(ref)
				ref = lv2_atom_forge_int(forge, sequence_num);
		}

		if(ref)
			ref = lv2_atom_forge_key(forge, props->urid.patch_remove);
		if(ref)
			ref = lv2_atom_forge_object(forge, &remove_frame, 0, 0);
		{
			if(ref)
				ref = lv2_atom_forge_key(forge, props->urid.rdfs_range);
			if(ref)
				ref = lv2_atom_forge_urid(forge, props->urid.patch_wildcard);

			if(ref)
				ref = lv2_atom_forge_key(forge, props->urid.rdfs_label);
			if(ref)
				ref = lv2_atom_forge_urid(forge, props->urid.patch_wildcard);

			if(ref)
				ref = lv2_atom_forge_key(forge, props->urid.rdfs_comment);
			if(ref)
				ref = lv2_atom_forge_urid(forge, props->urid.patch_wildcard);

			if(ref)
				ref = lv2_atom_forge_key(forge, props->urid.lv2_minimum);
			if(ref)
				ref = lv2_atom_forge_urid(forge, props->urid.patch_wildcard);

			if(ref)
				ref = lv2_atom_forge_key(forge, props->urid.lv2_maximum);
			if(ref)
				ref = lv2_atom_forge_urid(forge, props->urid.patch_wildcard);

			if(ref)
				ref = lv2_atom_forge_key(forge, props->urid.units_unit);
			if(ref)
				ref = lv2_atom_forge_urid(forge, props->urid.patch_wildcard);

			if(ref)
				ref = lv2_atom_forge_key(forge, props->urid.lv2_scale_point);
			if(ref)
				ref = lv2_atom_forge_urid(forge, props->urid.patch_wildcard);
		}
		if(ref)
			lv2_atom_forge_pop(forge, &remove_frame);

		if(ref)
			ref = lv2_atom_forge_key(forge, props->urid.patch_add);
		if(ref)
			ref = lv2_atom_forge_object(forge, &add_frame, 0, 0);
		{
			if(ref)
				ref = lv2_atom_forge_key(forge, props->urid.rdfs_range);
			if(ref)
				ref = lv2_atom_forge_urid(forge, impl->type->urid);

			if(def->label)
			{
				if(ref)
					ref = lv2_atom_forge_key(forge, props->urid.rdfs_label);
				if(ref)
					ref = lv2_atom_forge_string(forge, def->label, strlen(def->label));
			}

			if(def->comment)
			{
				if(ref)
					ref = lv2_atom_forge_key(forge, props->urid.rdfs_comment);
				if(ref)
					ref = lv2_atom_forge_string(forge, def->comment, strlen(def->comment));
			}

			if(ref)
				ref = lv2_atom_forge_key(forge, props->urid.lv2_minimum);
			if(ref)
				ref = impl->type->get_cb(forge, &def->minimum);

			if(ref)
				ref = lv2_atom_forge_key(forge, props->urid.lv2_maximum);
			if(ref)
				ref = impl->type->get_cb(forge, &def->maximum);

			if(props->urid.units_unit)
			{
				if(ref)
					ref = lv2_atom_forge_key(forge, props->urid.units_unit);
				if(ref)
					ref = lv2_atom_forge_urid(forge, impl->unit);
			}

			if(def->scale_points)
			{
				LV2_Atom_Forge_Frame tuple_frame;
				if(ref)
					ref = lv2_atom_forge_key(forge, props->urid.lv2_scale_point);
				if(ref)
					ref = lv2_atom_forge_tuple(forge, &tuple_frame);

				for(const props_scale_point_t *sp = def->scale_points; sp->label; sp++)
				{
					LV2_Atom_Forge_Frame scale_point_frame;

					if(ref)
						ref = lv2_atom_forge_object(forge, &scale_point_frame, 0, 0);
					{
						if(ref)
							ref = lv2_atom_forge_key(forge, props->urid.rdfs_label);
						if(ref)
							ref = lv2_atom_forge_string(forge, sp->label, strlen(sp->label));

						if(ref)
							ref = lv2_atom_forge_key(forge, props->urid.rdf_value);
						if(ref)
							ref = impl->type->get_cb(forge, &sp->value);
					}
					if(ref)
						lv2_atom_forge_pop(forge, &scale_point_frame);
				}

				if(ref)
					lv2_atom_forge_pop(forge, &tuple_frame);
			}
		}
		if(ref)
			lv2_atom_forge_pop(forge, &add_frame);
	}
	if(ref)
		lv2_atom_forge_pop(forge, &obj_frame);

	return ref;
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

	props->urid.rdf_value = map->map(map->handle,
		"http://www.w3.org/1999/02/22-rdf-syntax-ns#value");

	props->urid.rdfs_label = map->map(map->handle,
		"http://www.w3.org/2000/01/rdf-schema#label");
	props->urid.rdfs_range = map->map(map->handle,
		"http://www.w3.org/2000/01/rdf-schema#range");
	props->urid.rdfs_comment = map->map(map->handle,
		"http://www.w3.org/2000/01/rdf-schema#comment");

	props->urid.lv2_minimum = map->map(map->handle, LV2_CORE__minimum);
	props->urid.lv2_maximum = map->map(map->handle, LV2_CORE__maximum);
	props->urid.lv2_scale_point = map->map(map->handle, LV2_CORE__scalePoint);

	props->urid.atom_int = map->map(map->handle, LV2_ATOM__Int);
	props->urid.atom_long = map->map(map->handle, LV2_ATOM__Long);
	props->urid.atom_float = map->map(map->handle, LV2_ATOM__Float);
	props->urid.atom_double = map->map(map->handle, LV2_ATOM__Double);
	props->urid.atom_bool = map->map(map->handle, LV2_ATOM__Bool);
	props->urid.atom_urid = map->map(map->handle, LV2_ATOM__URID);
	props->urid.atom_string = map->map(map->handle, LV2_ATOM__String);
	props->urid.atom_path = map->map(map->handle, LV2_ATOM__Path);
	props->urid.atom_uri = map->map(map->handle, LV2_ATOM__URI);
	props->urid.atom_chunk = map->map(map->handle, LV2_ATOM__Chunk);

	props->urid.units_unit = map->map(map->handle, LV2_UNITS__unit);

	// Int
	unsigned ptr = 0;
	props->types[ptr].urid = props->urid.atom_int;
	props->types[ptr].size = sizeof(int32_t);
	props->types[ptr].size_cb = NULL;
	props->types[ptr].get_cb = _props_int_get_cb;
	props->types[ptr].set_cb = _props_int_set_cb;
	ptr++;

	// Long
	props->types[ptr].urid = props->urid.atom_long;
	props->types[ptr].size = sizeof(int64_t);
	props->types[ptr].size_cb = NULL;
	props->types[ptr].get_cb = _props_long_get_cb;
	props->types[ptr].set_cb = _props_long_set_cb;
	ptr++;

	// Float
	props->types[ptr].urid = props->urid.atom_float;
	props->types[ptr].size = sizeof(float);
	props->types[ptr].size_cb = NULL;
	props->types[ptr].get_cb = _props_float_get_cb;
	props->types[ptr].set_cb = _props_float_set_cb;
	ptr++;

	// double
	props->types[ptr].urid = props->urid.atom_double;
	props->types[ptr].size = sizeof(double);
	props->types[ptr].size_cb = NULL;
	props->types[ptr].get_cb = _props_double_get_cb;
	props->types[ptr].set_cb = _props_double_set_cb;
	ptr++;

	// Bool
	props->types[ptr].urid = props->urid.atom_bool;
	props->types[ptr].size = sizeof(int32_t);
	props->types[ptr].size_cb = NULL;
	props->types[ptr].get_cb = _props_bool_get_cb;
	props->types[ptr].set_cb = _props_int_set_cb;
	ptr++;

	// URID
	props->types[ptr].urid = props->urid.atom_urid;
	props->types[ptr].size = sizeof(uint32_t);
	props->types[ptr].size_cb = NULL;
	props->types[ptr].get_cb = _props_urid_get_cb;
	props->types[ptr].set_cb = _props_urid_set_cb;
	ptr++;

	// String
	props->types[ptr].urid = props->urid.atom_string;
	props->types[ptr].size = 0;
	props->types[ptr].size_cb = _props_string_size_cb;
	props->types[ptr].get_cb = _props_string_get_cb;
	props->types[ptr].set_cb = _props_string_set_cb;
	ptr++;

	// Path
	props->types[ptr].urid = props->urid.atom_path;
	props->types[ptr].size = 0;
	props->types[ptr].size_cb = _props_string_size_cb;
	props->types[ptr].get_cb = _props_path_get_cb;
	props->types[ptr].set_cb = _props_string_set_cb;
	ptr++;

	// URI
	props->types[ptr].urid = props->urid.atom_uri;
	props->types[ptr].size = 0;
	props->types[ptr].size_cb = _props_string_size_cb;
	props->types[ptr].get_cb = _props_uri_get_cb;
	props->types[ptr].set_cb = _props_string_set_cb;
	ptr++;

	// URI
	props->types[ptr].urid = props->urid.atom_chunk;
	props->types[ptr].size = 0;
	props->types[ptr].size_cb = _props_chunk_size_cb;
	props->types[ptr].get_cb = _props_chunk_get_cb;
	props->types[ptr].set_cb = _props_chunk_set_cb;
	ptr++;

	assert(ptr == PROPS_TYPE_N);
	_type_qsort(props->types, PROPS_TYPE_N);

	return 1;
}

static inline LV2_URID
props_register(props_t *props, const props_def_t *def, void *value, void *stash)
{
	if(props->nimpls >= props->max_nimpls)
		return 0;

	if(!def || !def->property || !def->access || !def->type || !value || !stash)
		return 0;

	const LV2_URID type = props->map->map(props->map->handle, def->type);
	const props_type_t *props_type = _type_bsearch(type, props->types, PROPS_TYPE_N);
	const LV2_URID property = props->map->map(props->map->handle, def->property);
	const LV2_URID access = props->map->map(props->map->handle, def->access);

	if(!props_type || !property || !access)
		return 0;

	props_impl_t *impl = &props->impls[props->nimpls++];

	impl->props = props;
	impl->property = property;
	impl->access = access;
	impl->unit = def->unit ? props->map->map(props->map->handle, def->unit) : 0;
	impl->type = props_type;
	impl->def = def;
	impl->value = value;
	impl->stash = stash;
	atomic_flag_clear_explicit(&impl->lock, memory_order_relaxed);

	// update maximal value size
	if(props_type->size && (props_type->size > props->max_size) )
		props->max_size = props_type->size;
	else if(def->max_size && (def->max_size > props->max_size) )
		props->max_size = def->max_size;
	
	_impl_qsort(props->impls, props->nimpls);

	//TODO register?

	return property;
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

				if(impl->def->mode == PROP_MODE_DYNAMIC)
				{
					if(*ref)
						*ref = _props_reg(props, forge, frames, impl, sequence_num);
					if(def->event_cb && (def->event_mask & PROP_EVENT_REGISTER) )
						def->event_cb(props->data, forge, frames, PROP_EVENT_REGISTER, impl);
				}

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

static inline LV2_State_Status
props_save(props_t *props, LV2_Atom_Forge *forge, LV2_State_Store_Function store,
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

	void *value = malloc(props->max_size); // create memory to store widest value
	if(value)
	{
		for(unsigned i = 0; i < props->nimpls; i++)
		{
			props_impl_t *impl = &props->impls[i];

			if(impl->access == props->urid.patch_readable)
				continue; // skip read-only, as it makes no sense to restore them

			// create lockfree copy of value, store() may well be blocking
			_impl_spin_lock(impl);

			const uint32_t size = _impl_size_get(impl);
			memcpy(value, impl->stash, size);

			_impl_unlock(impl);

			if( map_path && (impl->type->urid == forge->Path) )
			{
				const char *path = strstr(value, "file://")
					? value + 7 // skip "file://"
					: value;
				char *abstract = map_path->abstract_path(map_path->handle, path);
				if(abstract && strcmp(abstract, path))
				{
					store(state, impl->property, abstract, strlen(abstract) + 1, impl->type->urid, flags);
					free(abstract);
				}
			}
			else // !Path
			{
				store(state, impl->property, value, size, impl->type->urid, flags);
			}

			const props_def_t *def = impl->def;
			if(def->event_cb && (def->event_mask & PROP_EVENT_SAVE) )
				def->event_cb(props->data, forge, 0, PROP_EVENT_SAVE, impl);
		}

		free(value);
	}

	return LV2_STATE_SUCCESS;
}

static inline LV2_State_Status
props_restore(props_t *props, LV2_Atom_Forge *forge, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags, const LV2_Feature *const *features)
{
	const LV2_State_Map_Path *map_path = NULL;

	for(unsigned i = 0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_STATE__mapPath))
			map_path = features[i]->data;
	}

	for(unsigned i = 0; i < props->nimpls; i++)
	{
		props_impl_t *impl = &props->impls[i];

		if(impl->access == props->urid.patch_readable)
			continue; // skip read-only, as it makes no sense to restore them

		size_t size;
		uint32_t type;
		uint32_t _flags;
		const void *value = retrieve(state, impl->property, &size, &type, &_flags);

		if(value)
		{
			if( map_path && (impl->type->urid == forge->Path) )
			{
				char *absolute = map_path->absolute_path(map_path->handle, value);
				if(absolute)
				{
					_props_set(props, impl, type, strlen(absolute) + 1, absolute);
					free(absolute);
				}
			}
			else // !Path
			{
				_props_set(props, impl, type, size, value);
			}

			const props_def_t *def = impl->def;
			if(def->event_cb && (def->event_mask & PROP_EVENT_RESTORE) )
				def->event_cb(props->data, forge, 0, PROP_EVENT_RESTORE, impl);
		}
		else
		{
			fprintf(stderr, "props_restore: no property '%s'.\n", impl->def->property);
		}
	}

	return LV2_STATE_SUCCESS;
}

// undefinitions
#undef PROPS_TYPE_N

#ifdef __cplusplus
}
#endif

#endif // _LV2_PROPS_H_
