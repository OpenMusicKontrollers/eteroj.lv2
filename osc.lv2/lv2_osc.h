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

#ifndef _LV2_OSC_H_
#define _LV2_OSC_H_

#include <math.h> // INFINITY

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>

#define OSC_URI								"http://opensoundcontrol.org"
#define OSC_PREFIX						OSC_URI "#"	

#define OSC__Event						OSC_PREFIX "Event"						// object id
#define OSC__Bundle						OSC_PREFIX "Bundle"						// object otype
#define OSC__Message					OSC_PREFIX "Message"					// object otype
#define OSC__bundleTimestamp	OSC_PREFIX "bundleTimestamp"	// property key
#define OSC__bundleItems			OSC_PREFIX "bundleItems"			// property key
#define OSC__messagePath			OSC_PREFIX "messagePath"			// property key
#define OSC__messageFormat		OSC_PREFIX "messageFormat"		// property key
#define OSC__messageArguments	OSC_PREFIX "messageArguments"	// property key

typedef struct _osc_forge_t osc_forge_t;

struct _osc_forge_t {
	LV2_URID OSC_Event;

	LV2_URID OSC_Bundle;
	LV2_URID OSC_Message;

	LV2_URID OSC_bundleTimestamp;
	LV2_URID OSC_bundleItems;

	LV2_URID OSC_messagePath;
	LV2_URID OSC_messageFormat;
	LV2_URID OSC_messageArguments;

	LV2_URID MIDI_MidiEvent;
	
	LV2_URID ATOM_Object;
};

static inline void
osc_forge_init(osc_forge_t *oforge, LV2_URID_Map *map)
{
	oforge->OSC_Event = map->map(map->handle, OSC__Event);

	oforge->OSC_Bundle = map->map(map->handle, OSC__Bundle);
	oforge->OSC_Message = map->map(map->handle, OSC__Message);

	oforge->OSC_bundleTimestamp = map->map(map->handle, OSC__bundleTimestamp);
	oforge->OSC_bundleItems = map->map(map->handle, OSC__bundleItems);

	oforge->OSC_messagePath = map->map(map->handle, OSC__messagePath);
	oforge->OSC_messageFormat = map->map(map->handle, OSC__messageFormat);
	oforge->OSC_messageArguments = map->map(map->handle, OSC__messageArguments);
	
	oforge->MIDI_MidiEvent = map->map(map->handle, LV2_MIDI__MidiEvent);
	
	oforge->ATOM_Object = map->map(map->handle, LV2_ATOM__Object);
}

static inline int
osc_atom_is_bundle(osc_forge_t *oforge, const LV2_Atom_Object *obj)
{
	return (obj->atom.type == oforge->ATOM_Object)
		&& (obj->body.id == oforge->OSC_Event)
		&& (obj->body.otype == oforge->OSC_Bundle);
}

static inline void
osc_atom_bundle_unpack(osc_forge_t *oforge, const LV2_Atom_Object *obj,
	const LV2_Atom_Long **timestamp, const LV2_Atom_Tuple **items)
{
	*timestamp = NULL;
	*items = NULL;

	LV2_Atom_Object_Query q [] = {
		{ oforge->OSC_bundleTimestamp, (const LV2_Atom **)timestamp },
		{ oforge->OSC_bundleItems, (const LV2_Atom **)items },
		LV2_ATOM_OBJECT_QUERY_END
	};

	lv2_atom_object_query(obj, q);
}

static inline int
osc_atom_is_message(osc_forge_t *oforge, const LV2_Atom_Object *obj)
{
	return (obj->atom.type == oforge->ATOM_Object)
		&& (obj->body.id == oforge->OSC_Event)
		&& (obj->body.otype == oforge->OSC_Message);
}

static inline void
osc_atom_message_unpack(osc_forge_t *oforge, const LV2_Atom_Object *obj,
	const LV2_Atom_String **path, const LV2_Atom_String **format,
	const LV2_Atom_Tuple **arguments)
{
	*path = NULL;
	*format = NULL;
	*arguments = NULL;

	LV2_Atom_Object_Query q [] = {
		{ oforge->OSC_messagePath, (const LV2_Atom **)path },
		{ oforge->OSC_messageFormat, (const LV2_Atom **)format },
		{ oforge->OSC_messageArguments, (const LV2_Atom **)arguments },
		LV2_ATOM_OBJECT_QUERY_END
	};

	lv2_atom_object_query(obj, q);
}

typedef void (*osc_message_cb_t)(uint64_t timestamp, const char *path,
	const char *fmt, const LV2_Atom_Tuple *arguments, void *data);

static inline void osc_atom_event_unroll(osc_forge_t *oforge,
	const LV2_Atom_Object *obj, osc_message_cb_t cb, void *data);

static inline void
osc_atom_message_unroll(osc_forge_t *oforge, uint64_t timestamp,
	const LV2_Atom_Object *obj, osc_message_cb_t cb, void *data)
{
	if(!cb)
		return;

	const LV2_Atom_String* path;
	const LV2_Atom_String* fmt;
	const LV2_Atom_Tuple* args;

	osc_atom_message_unpack(oforge, obj, &path, &fmt, &args);

	const char *path_str = path ? LV2_ATOM_BODY_CONST(path) : NULL;
	const char *fmt_str = fmt ? LV2_ATOM_BODY_CONST(fmt) : NULL;

	if(path_str && fmt_str)
		cb(timestamp, path_str, fmt_str, args, data);
}

static inline void
osc_atom_bundle_unroll(osc_forge_t *oforge, const LV2_Atom_Object *obj,
	osc_message_cb_t cb, void *data)
{
	if(!cb)
		return;

	const LV2_Atom_Long* timestamp;
	const LV2_Atom_Tuple* items;

	osc_atom_bundle_unpack(oforge, obj, &timestamp, &items);

	if(!items)
		return;

	uint64_t timestamp_body = timestamp ? timestamp->body : 1ULL;

	// iterate over tuple body
	for(const LV2_Atom *itr = lv2_atom_tuple_begin(items);
		!lv2_atom_tuple_is_end(LV2_ATOM_BODY(items), items->atom.size, itr);
		itr = lv2_atom_tuple_next(itr))
	{
		osc_atom_event_unroll(oforge, (const LV2_Atom_Object *)itr, cb, data);
	}
}

static inline void
osc_atom_event_unroll(osc_forge_t *oforge, const LV2_Atom_Object *obj,
	osc_message_cb_t cb, void *data)
{
	if(!cb)
		return;

	if(osc_atom_is_bundle(oforge, obj))
		osc_atom_bundle_unroll(oforge, obj, cb, data);
	else if(osc_atom_is_message(oforge, obj))
		osc_atom_message_unroll(oforge, 1ULL, obj, cb, data);
	else
		; // no OSC packet, obviously
}

static inline LV2_Atom_Forge_Ref
osc_forge_bundle_push(osc_forge_t *oforge, LV2_Atom_Forge *forge,
	LV2_Atom_Forge_Frame frame [2], uint64_t timestamp)
{
	if(!lv2_atom_forge_object(forge, &frame[0], oforge->OSC_Event,
			oforge->OSC_Bundle))
		return 0;

	if(!lv2_atom_forge_key(forge, oforge->OSC_bundleTimestamp))
		return 0;
	if(!lv2_atom_forge_long(forge, timestamp))
		return 0;

	if(!lv2_atom_forge_key(forge, oforge->OSC_bundleItems))
		return 0;

	return lv2_atom_forge_tuple(forge, &frame[1]);
}

static inline void
osc_forge_bundle_pop(osc_forge_t *oforge, LV2_Atom_Forge *forge,
	LV2_Atom_Forge_Frame frame [2])
{
	lv2_atom_forge_pop(forge, &frame[1]);
	lv2_atom_forge_pop(forge, &frame[0]);
}

static inline LV2_Atom_Forge_Ref
osc_forge_message_push(osc_forge_t *oforge, LV2_Atom_Forge *forge,
	LV2_Atom_Forge_Frame frame [2], const char *path, const char *fmt)
{
	if(!lv2_atom_forge_object(forge, &frame[0], oforge->OSC_Event,
			oforge->OSC_Message))
		return 0;

	if(!lv2_atom_forge_key(forge, oforge->OSC_messagePath))
		return 0;
	if(!lv2_atom_forge_string(forge, path, strlen(path)))
		return 0;

	if(!lv2_atom_forge_key(forge, oforge->OSC_messageFormat))
		return 0;
	if(!lv2_atom_forge_string(forge, fmt, strlen(fmt)))
		return 0;

	if(!lv2_atom_forge_key(forge, oforge->OSC_messageArguments))
		return 0;

	return lv2_atom_forge_tuple(forge, &frame[1]);
}

static inline void
osc_forge_message_pop(osc_forge_t *oforge, LV2_Atom_Forge *forge,
	LV2_Atom_Forge_Frame frame [2])
{
	lv2_atom_forge_pop(forge, &frame[1]);
	lv2_atom_forge_pop(forge, &frame[0]);
}
	
static inline LV2_Atom_Forge_Ref
osc_forge_int32(osc_forge_t *oforge, LV2_Atom_Forge *forge, int32_t i)
{
	return lv2_atom_forge_int(forge, i);
}

static inline LV2_Atom_Forge_Ref
osc_forge_float(osc_forge_t *oforge, LV2_Atom_Forge *forge, float f)
{
	return lv2_atom_forge_float(forge, f);
}

static inline LV2_Atom_Forge_Ref
osc_forge_string(osc_forge_t *oforge, LV2_Atom_Forge *forge, const char *s)
{
	return lv2_atom_forge_string(forge, s, strlen(s));
}

static inline LV2_Atom_Forge_Ref
osc_forge_symbol(osc_forge_t *oforge, LV2_Atom_Forge *forge, const char *s)
{
	return lv2_atom_forge_string(forge, s, strlen(s));
}

static inline LV2_Atom_Forge_Ref
osc_forge_blob(osc_forge_t *oforge, LV2_Atom_Forge *forge, int32_t size,
	const uint8_t *b)
{
	LV2_Atom_Forge_Ref ref;
	if(!(ref = lv2_atom_forge_atom(forge, size, forge->Chunk)))
		return 0;
	if(!(ref = lv2_atom_forge_raw(forge, b, size)))
		return 0;
	lv2_atom_forge_pad(forge, size);

	return ref;
}

static inline LV2_Atom_Forge_Ref
osc_forge_int64(osc_forge_t *oforge, LV2_Atom_Forge *forge, int64_t h)
{
	return lv2_atom_forge_long(forge, h);
}

static inline LV2_Atom_Forge_Ref
osc_forge_double(osc_forge_t *oforge, LV2_Atom_Forge *forge, double d)
{
	return lv2_atom_forge_double(forge, d);
}

static inline LV2_Atom_Forge_Ref
osc_forge_timestamp(osc_forge_t *oforge, LV2_Atom_Forge *forge, uint64_t t)
{
	return lv2_atom_forge_long(forge, t);
}

static inline LV2_Atom_Forge_Ref
osc_forge_char(osc_forge_t *oforge, LV2_Atom_Forge *forge, char c)
{
	return lv2_atom_forge_int(forge, c);
}

static inline LV2_Atom_Forge_Ref
osc_forge_midi(osc_forge_t *oforge, LV2_Atom_Forge *forge, const uint8_t *m)
{
	LV2_Atom_Forge_Ref ref;
	if(!(ref = lv2_atom_forge_atom(forge, 4, oforge->MIDI_MidiEvent)))
		return 0;
	if(!(ref = lv2_atom_forge_raw(forge, m, 4)))
		return 0;
	lv2_atom_forge_pad(forge, 4);

	return ref;
}

static inline LV2_Atom_Forge_Ref
osc_forge_true(osc_forge_t *oforge, LV2_Atom_Forge *forge)
{
	return lv2_atom_forge_bool(forge, 1);
}

static inline LV2_Atom_Forge_Ref
osc_forge_false(osc_forge_t *oforge, LV2_Atom_Forge *forge)
{
	return lv2_atom_forge_bool(forge, 0);
}

static inline LV2_Atom_Forge_Ref
osc_forge_nil(osc_forge_t *oforge, LV2_Atom_Forge *forge)
{
	return lv2_atom_forge_atom(forge, 0, 0);
}

static inline LV2_Atom_Forge_Ref
osc_forge_bang(osc_forge_t *oforge, LV2_Atom_Forge *forge)
{
	return lv2_atom_forge_float(forge, INFINITY);
}

static inline LV2_Atom_Forge_Ref
osc_forge_message_varlist(osc_forge_t *oforge, LV2_Atom_Forge *forge,
	const char *path, const char *fmt, va_list args)
{
	LV2_Atom_Forge_Frame frame [2];
	LV2_Atom_Forge_Ref ref;

	if(!(ref = osc_forge_message_push(oforge, forge, frame, path, fmt)))
		return 0;

	for(const char *type = fmt; *type; type++)
	{
		switch(*type)
		{
			case 'i':
				if(!(ref =osc_forge_int32(oforge, forge, va_arg(args, int32_t))))
					return 0;
				break;
			case 'f':
				if(!(ref = osc_forge_float(oforge, forge, (float)va_arg(args, double))))
					return 0;
				break;
			case 's':
				if(!(ref = osc_forge_string(oforge, forge, va_arg(args, const char *))))
					return 0;
				break;
			case 'S':
				if(!(ref = osc_forge_symbol(oforge, forge, va_arg(args, const char *))))
					return 0;
				break;
			case 'b':
			{
				int32_t size = va_arg(args, int32_t);
				const uint8_t *b = va_arg(args, const uint8_t *);
				if(!(ref = osc_forge_blob(oforge, forge, size, b)))
					return 0;
				break;
			}
			
			case 'h':
				if(!(ref = osc_forge_int64(oforge, forge, va_arg(args, int64_t))))
					return 0;
				break;
			case 'd':
				if(!(ref = osc_forge_double(oforge, forge, va_arg(args, double))))
					return 0;
				break;
			case 't':
				if(!(ref = osc_forge_timestamp(oforge, forge, va_arg(args, uint64_t))))
					return 0;
				break;
			
			case 'c':
				if(!(ref = osc_forge_char(oforge, forge, (char)va_arg(args, unsigned int))))
					return 0;
				break;
			case 'm':
				if(!(ref = osc_forge_midi(oforge, forge, va_arg(args, const uint8_t *))))
					return 0;
				break;
			
			case 'T':
				if(!(ref = osc_forge_true(oforge, forge)))
					return 0;
				break;
			case 'F':
				if(!(ref = osc_forge_false(oforge, forge)))
					return 0;
				break;
			case 'N':
				if(!(ref = osc_forge_nil(oforge, forge)))
					return 0;
				break;
			case 'I':
				if(!(ref = osc_forge_bang(oforge, forge)))
					return 0;
				break;

			default: // unknown argument type
				return 0;
		}
	}

	osc_forge_message_pop(oforge, forge, frame);

	return ref;
}

static inline LV2_Atom_Forge_Ref
osc_forge_message_vararg(osc_forge_t *oforge, LV2_Atom_Forge *forge,
	const char *path, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	LV2_Atom_Forge_Ref ref;
	ref = osc_forge_message_varlist(oforge, forge, path, fmt, args);

	va_end(args);

	return ref;
}

#endif // _LV2_OSC_H_