/*
 * Copyright (c) 2016-2021 Hanspeter Portner (dev@open-music-kontrollers.ch)
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
#include <varchunk.h>
#include <osc.lv2/util.h>
#include <osc.lv2/forge.h>
#include <props.h>
#include <jsmn.h>

#define MAX_NPROPS 1
#define MAX_TOKENS 256

typedef struct _plugstate_t plugstate_t;
typedef struct _plughandle_t plughandle_t;

struct _plugstate_t {
	int32_t query_refresh;
};

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_URID_Unmap *unmap;
	struct {
		LV2_URID subject;

		LV2_URID patch_message;
		LV2_URID patch_set;
		LV2_URID patch_get;
		LV2_URID patch_subject;
		LV2_URID patch_property;
		LV2_URID patch_value;
		LV2_URID patch_wildcard;
		LV2_URID patch_patch;
		LV2_URID patch_add;
		LV2_URID patch_remove;
		LV2_URID patch_writable;
		LV2_URID patch_readable;

		LV2_URID rdfs_label;
		LV2_URID rdfs_comment;
		LV2_URID rdfs_range;

		LV2_URID rdf_value;

		LV2_URID lv2_minimum;
		LV2_URID lv2_maximum;
		LV2_URID lv2_scale_point;

		LV2_URID query_refresh;
	} urid;

	LV2_OSC_URID osc_urid;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;
	int64_t frames;

	PROPS_T(props, MAX_NPROPS);

	plugstate_t state;
	plugstate_t stash;

	LV2_Log_Log *log;
	LV2_Log_Logger logger;

	const LV2_Atom_Sequence *osc_in;
	LV2_Atom_Sequence *osc_out;

	int32_t cnt;

	jsmn_parser parser;
	jsmntok_t tokens [MAX_TOKENS];

	varchunk_t *rb;
};

static void
_add(plughandle_t *handle, int64_t frame_time, char type, bool read, bool write,
	const char *json, int range_cnt, jsmntok_t *range, int values_cnt, jsmntok_t *values,
	LV2_URID property, const char *label, uint32_t label_len, const char *comment, uint32_t comment_len)
{
	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Frame obj_frame;
	LV2_Atom_Forge_Ref ref = handle->ref;

	if(write && !read) //TODO handle this
		return;

	if(!range_cnt && !values_cnt) //TODO handle this
		return;

	LV2_URID access = write
		? handle->urid.patch_writable
		: handle->urid.patch_readable;

	LV2_URID _type = forge->Int;
	switch((LV2_OSC_Type)type)
	{
		case LV2_OSC_INT32:
			_type = forge->Int;
			break;
		case LV2_OSC_INT64:
			_type = forge->Long;
			break;
		case LV2_OSC_FLOAT:
			_type = forge->Float;
			break;
		case LV2_OSC_DOUBLE:
			_type = forge->Double;
			break;
		case LV2_OSC_STRING:
			_type = forge->String;
			break;
		default:
			//TODO do other types
			break;
	}

	jsmntok_t *mintok = NULL;
	jsmntok_t *maxtok = NULL;
	double min = 0.f;
	double max = 1.f;

	if(range_cnt && range)
	{
		mintok = &range[0];
		maxtok = &range[1];
		min = strtod(&json[mintok->start], NULL);
		max = strtod(&json[maxtok->start], NULL);
		if( (_type == forge->Int) && (min == 0.0) && (max == 1.0) )
			_type = forge->Bool;
	}

	lv2_atom_forge_frame_time(forge, frame_time);
	lv2_atom_forge_object(forge, &obj_frame, 0, handle->urid.patch_patch);
	{
		lv2_atom_forge_key(forge, handle->urid.patch_subject);
		lv2_atom_forge_urid(forge, handle->urid.subject);

		lv2_atom_forge_key(forge, handle->urid.patch_remove);
		LV2_Atom_Forge_Frame remove_frame;
		lv2_atom_forge_object(forge, &remove_frame, 0, 0);
		{
			lv2_atom_forge_key(forge, access);
			lv2_atom_forge_urid(forge, property);
		}
		lv2_atom_forge_pop(forge, &remove_frame);
		
		lv2_atom_forge_key(forge, handle->urid.patch_add);
		LV2_Atom_Forge_Frame add_frame;
		lv2_atom_forge_object(forge, &add_frame, 0, 0);
		{
			lv2_atom_forge_key(forge, access);
			lv2_atom_forge_urid(forge, property);
		}
		lv2_atom_forge_pop(forge, &add_frame);
	}
	lv2_atom_forge_pop(forge, &obj_frame);

	lv2_atom_forge_frame_time(forge, frame_time);
	lv2_atom_forge_object(forge, &obj_frame, 0, handle->urid.patch_patch);
	{
		lv2_atom_forge_key(forge, handle->urid.patch_subject);
		lv2_atom_forge_urid(forge, property);

		lv2_atom_forge_key(forge, handle->urid.patch_remove);
		LV2_Atom_Forge_Frame remove_frame;
		lv2_atom_forge_object(forge, &remove_frame, 0, 0);
		{
			lv2_atom_forge_key(forge, handle->urid.rdfs_label);
			lv2_atom_forge_urid(forge, handle->urid.patch_wildcard);

			lv2_atom_forge_key(forge, handle->urid.rdfs_range);
			lv2_atom_forge_urid(forge, handle->urid.patch_wildcard);

			lv2_atom_forge_key(forge, handle->urid.rdfs_comment);
			lv2_atom_forge_urid(forge, handle->urid.patch_wildcard);

			lv2_atom_forge_key(forge, handle->urid.lv2_minimum);
			lv2_atom_forge_urid(forge, handle->urid.patch_wildcard);

			lv2_atom_forge_key(forge, handle->urid.lv2_maximum);
			lv2_atom_forge_urid(forge, handle->urid.patch_wildcard);

			lv2_atom_forge_key(forge, handle->urid.lv2_scale_point);
			lv2_atom_forge_urid(forge, handle->urid.patch_wildcard);
		}
		lv2_atom_forge_pop(forge, &remove_frame);
		
		lv2_atom_forge_key(forge, handle->urid.patch_add);
		LV2_Atom_Forge_Frame add_frame;
		lv2_atom_forge_object(forge, &add_frame, 0, 0);
		{
			if(label)
			{
				lv2_atom_forge_key(forge, handle->urid.rdfs_label);
				lv2_atom_forge_string(forge, label, label_len);
			}

			if(comment)
			{
				lv2_atom_forge_key(forge, handle->urid.rdfs_comment);
				lv2_atom_forge_string(forge, comment, comment_len);
			}

			lv2_atom_forge_key(forge, handle->urid.rdfs_range);
			lv2_atom_forge_urid(forge, _type);
	
			if(mintok && (_type != forge->Bool) )
			{
				lv2_atom_forge_key(forge, handle->urid.lv2_minimum);
				if(_type == forge->Int)
					lv2_atom_forge_int(forge, min);
				else if(_type == forge->Long)
					lv2_atom_forge_long(forge, min);
				else if(_type == forge->Float)
					lv2_atom_forge_float(forge, min);
				else if(_type == forge->Double)
					lv2_atom_forge_double(forge, min);
				else
					lv2_atom_forge_atom(forge, 0, 0); // blank
				//TODO do other types
			}

			if(maxtok && (_type != forge->Bool) )
			{
				lv2_atom_forge_key(forge, handle->urid.lv2_maximum);
				if(_type == forge->Int)
					lv2_atom_forge_int(forge, max);
				else if(_type == forge->Long)
					lv2_atom_forge_long(forge, max);
				else if(_type == forge->Float)
					lv2_atom_forge_float(forge, max);
				else if(_type == forge->Double)
					lv2_atom_forge_double(forge, max);
				else
					lv2_atom_forge_atom(forge, 0, 0); // blank
				//TODO do other types
			}

			if(values_cnt && values)
			{
				LV2_Atom_Forge_Frame tuple_frame;
				lv2_atom_forge_key(forge, handle->urid.lv2_scale_point);
				lv2_atom_forge_tuple(forge, &tuple_frame);

				//iterate over all scale_points
				for(int i=0; i<values_cnt; i++)
				{
					jsmntok_t *tok = &values[i];

					LV2_Atom_Forge_Frame scale_point_frame;
					lv2_atom_forge_object(forge, &scale_point_frame, 0, 0);
					{
						lv2_atom_forge_key(forge, handle->urid.rdfs_label);
						lv2_atom_forge_string(forge, &json[tok->start], tok->end - tok->start);

						lv2_atom_forge_key(forge, handle->urid.rdf_value);
						if(_type == forge->Int)
							lv2_atom_forge_int(forge, strtol(&json[tok->start], NULL, 0));
						else if(_type == forge->Long)
							lv2_atom_forge_long(forge, strtol(&json[tok->start], NULL, 0));
						else if(_type == forge->Float)
							lv2_atom_forge_float(forge, strtod(&json[tok->start], NULL));
						else if(_type == forge->Double)
							lv2_atom_forge_double(forge, strtod(&json[tok->start], NULL));
						else if(_type == forge->String)
							lv2_atom_forge_string(forge, &json[tok->start], tok->end - tok->start);
						else
							lv2_atom_forge_atom(forge, 0, 0); // blank
						//TODO do other types
					}
					lv2_atom_forge_pop(forge, &scale_point_frame);
				}
				lv2_atom_forge_pop(forge, &tuple_frame);
			}
		}
		lv2_atom_forge_pop(forge, &add_frame);
	}
	lv2_atom_forge_pop(forge, &obj_frame);

	handle->ref = ref;
}
			
static void
_set(plughandle_t *handle, int64_t frame_time, LV2_URID property, const LV2_Atom *atom)
{
	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Frame obj_frame;
	LV2_Atom_Forge_Ref ref = handle->ref;

	lv2_atom_forge_frame_time(forge, frame_time);
	lv2_atom_forge_object(forge, &obj_frame, 0, handle->urid.patch_set);
	{
		lv2_atom_forge_key(forge, handle->urid.patch_subject);
		lv2_atom_forge_urid(forge, handle->urid.subject);

		lv2_atom_forge_key(forge, handle->urid.patch_property);
		lv2_atom_forge_urid(forge, property);

		lv2_atom_forge_key(forge, handle->urid.patch_value);
		lv2_atom_forge_write(forge, atom, lv2_atom_total_size(atom));
	}
	lv2_atom_forge_pop(forge, &obj_frame);

	handle->ref = ref;
}

static void
_clear(plughandle_t *handle, int64_t frame_time)
{
	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Frame obj_frame;
	LV2_Atom_Forge_Ref ref = handle->ref;

	lv2_atom_forge_frame_time(forge, frame_time);
	lv2_atom_forge_object(forge, &obj_frame, 0, handle->urid.patch_patch);
	{
		lv2_atom_forge_key(forge, handle->urid.patch_subject);
		lv2_atom_forge_urid(forge, handle->urid.subject);

		lv2_atom_forge_key(forge, handle->urid.patch_remove);
		LV2_Atom_Forge_Frame remove_frame;
		lv2_atom_forge_object(forge, &remove_frame, 0, 0);
		{
			lv2_atom_forge_key(forge, handle->urid.patch_writable);
			lv2_atom_forge_urid(forge, handle->urid.patch_wildcard);

			lv2_atom_forge_key(forge, handle->urid.patch_readable);
			lv2_atom_forge_urid(forge, handle->urid.patch_wildcard);
		}
		lv2_atom_forge_pop(forge, &remove_frame);
		
		lv2_atom_forge_key(forge, handle->urid.patch_add);
		LV2_Atom_Forge_Frame add_frame;
		lv2_atom_forge_object(forge, &add_frame, 0, 0);
		{
			// empty
		}
		lv2_atom_forge_pop(forge, &add_frame);
	}
	lv2_atom_forge_pop(forge, &obj_frame);

	handle->ref = ref;
}

static void
_refresh(plughandle_t *handle, int64_t frame_time)
{
	_clear(handle, frame_time);

	const char *destination = "/!";

	// schedule on ring buffer
	size_t len2 = strlen(destination) + 1;
	char *ptr;
	if((ptr = varchunk_write_request(handle->rb, len2)))
	{
		strncpy(ptr, destination, len2);
		varchunk_write_advance(handle->rb, len2);
	}
	else if(handle->log)
		lv2_log_trace(&handle->logger, "eteroj#query: ringbuffer full");
}

static void
_intercept_refresh(void *data, int64_t frames, props_impl_t *impl)
{
	plughandle_t *handle = data;

	_refresh(handle, frames);

	handle->state.query_refresh = false;
	props_set(&handle->props, &handle->forge, frames, handle->urid.query_refresh, &handle->ref);
}

static const props_def_t defs [MAX_NPROPS] = {
	{
		.property = ETEROJ_QUERY_REFRESH_URI,
		.offset = offsetof(plugstate_t, query_refresh),
		.type = LV2_ATOM__Bool,
		.event_cb = _intercept_refresh
	}
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;
	mlock(handle, sizeof(plughandle_t));

	const LV2_State_Make_Path *make_path = NULL;

	for(int i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_URID__unmap))
			handle->unmap = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = features[i]->data;
	}

	if(!handle->map)
	{
		fprintf(stderr,
			"%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	if(!handle->unmap)
	{
		fprintf(stderr,
			"%s: Host does not support urid:unmap\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	if(handle->log)
		lv2_log_logger_init(&handle->logger, handle->map, handle->log);
	lv2_osc_urid_init(&handle->osc_urid, handle->map);
	lv2_atom_forge_init(&handle->forge, handle->map);

	handle->urid.subject = handle->map->map(handle->map->handle,
		descriptor->URI);

	handle->urid.patch_message = handle->map->map(handle->map->handle,
		LV2_PATCH__Message);
	handle->urid.patch_set = handle->map->map(handle->map->handle,
		LV2_PATCH__Set);
	handle->urid.patch_get = handle->map->map(handle->map->handle,
		LV2_PATCH__Get);
	handle->urid.patch_subject = handle->map->map(handle->map->handle,
		LV2_PATCH__subject);
	handle->urid.patch_property = handle->map->map(handle->map->handle,
		LV2_PATCH__property);
	handle->urid.patch_value = handle->map->map(handle->map->handle,
		LV2_PATCH__value);
	handle->urid.patch_wildcard = handle->map->map(handle->map->handle,
		LV2_PATCH__wildcard);
	handle->urid.patch_patch = handle->map->map(handle->map->handle,
		LV2_PATCH__Patch);
	handle->urid.patch_add = handle->map->map(handle->map->handle,
		LV2_PATCH__add);
	handle->urid.patch_remove = handle->map->map(handle->map->handle,
		LV2_PATCH__remove);
	handle->urid.patch_writable = handle->map->map(handle->map->handle,
		LV2_PATCH__writable);
	handle->urid.patch_readable = handle->map->map(handle->map->handle,
		LV2_PATCH__readable);

	handle->urid.rdfs_label = handle->map->map(handle->map->handle,
		"http://www.w3.org/2000/01/rdf-schema#label");
	handle->urid.rdfs_comment = handle->map->map(handle->map->handle,
		"http://www.w3.org/2000/01/rdf-schema#comment");
	handle->urid.rdfs_range = handle->map->map(handle->map->handle,
		"http://www.w3.org/2000/01/rdf-schema#range");

	handle->urid.rdf_value = handle->map->map(handle->map->handle,
		"http://www.w3.org/1999/02/22-rdf-syntax-ns#value");

	handle->urid.lv2_minimum = handle->map->map(handle->map->handle,
		LV2_CORE__minimum);
	handle->urid.lv2_maximum = handle->map->map(handle->map->handle,
		LV2_CORE__maximum);
	handle->urid.lv2_scale_point = handle->map->map(handle->map->handle,
		LV2_CORE__scalePoint);

	handle->cnt = 0;

	handle->rb = varchunk_new(65536, false);

	if(!props_init(&handle->props, descriptor->URI,
		defs, MAX_NPROPS, &handle->state, &handle->stash,
		handle->map, handle))
	{
		free(handle);
		return NULL;
	}

	handle->urid.query_refresh = props_map(&handle->props, ETEROJ_QUERY_REFRESH_URI);

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
		default:
			break;
	}
}

static inline int
json_eq(const char *json, jsmntok_t *tok, const char *s)
{
	if(  (tok->type == JSMN_STRING)
		&& ((int)strlen(s) == tok->end - tok->start)
		&& !strncmp(json + tok->start, s, tok->end - tok->start) )
	{
		return 0;
	}
	
	return -1;
}

static inline const char *
json_string(const char *json, jsmntok_t *tok, size_t *len)
{
	if(tok->type != JSMN_STRING)
		return NULL;

	*len = tok->end - tok->start;
	return json + tok->start;
}

static inline int32_t 
json_int(const char *json, jsmntok_t *tok)
{
	if(tok->type != JSMN_PRIMITIVE)
		return -1;

	return strtol(json + tok->start, NULL, 10);
}

static inline float 
json_float(const char *json, jsmntok_t *tok)
{
	if(tok->type != JSMN_PRIMITIVE)
		return -1;

	return strtod(json + tok->start, NULL);
}

static inline bool 
json_bool(const char *json, jsmntok_t *tok)
{
	if(tok->type != JSMN_PRIMITIVE)
		return false;

	if(json[tok->start] == 't')
		return true;
	else if(json[tok->start] == 'f')
		return false;

	return false;
}

// rt
static void
_message_cb(const char *path, const LV2_Atom_Tuple *body, void *data)
{
	plughandle_t *handle = data;
	LV2_OSC_URID *osc_urid = &handle->osc_urid;
	LV2_Atom_Forge *forge = &handle->forge;
	jsmntok_t *t = handle->tokens;

	const LV2_Atom *atom = lv2_atom_tuple_begin(body);

	if(!strcmp(path, "/success"))
	{
		int32_t id;
		const char *destination;

		atom = lv2_osc_int32_get(osc_urid, atom, &id);
		atom = lv2_osc_string_get(osc_urid, atom, &destination);
		//fprintf(stderr, "success: %s\n", destination);

		if(strrchr(destination, '!'))
		{
			const char *json;
			atom = lv2_osc_string_get(osc_urid, atom, &json);
			jsmn_init(&handle->parser);
			int n_tokens = jsmn_parse(&handle->parser, json, strlen(json),
				handle->tokens, MAX_TOKENS);
			if( (n_tokens < 1) && handle->log)
				lv2_log_trace(&handle->logger, "eror parsing JSON: '%s'", json);

			size_t target_len = 0;
			const char *target = NULL;
			size_t type_len = 0;
			const char *type = NULL;
			size_t desc_len = 0;
			const char *desc = NULL;

			int arg_n = 0;
			char arg_type = 'i';
			bool arg_read = true;
			bool arg_write = false;
			int arg_range_cnt = 0;	
			jsmntok_t *arg_range = NULL;
			int arg_values_cnt = 0;
			jsmntok_t *arg_values = NULL;

			for(int i = 1; i < n_tokens; )
			{
				jsmntok_t *key = &t[i++];
				jsmntok_t *value = &t[i++];

				if(!json_eq(json, key, "path"))
				{
					target = json_string(json, value, &target_len);
				}
				else if(!json_eq(json, key, "type"))
				{
					type = json_string(json, value, &type_len);
				}
				else if(!json_eq(json, key, "description"))
				{
					desc = json_string(json, value, &desc_len);
				}
				else if(!json_eq(json, key, "items"))
				{
					for(int j = 0; j < value->size; j++)
					{
						jsmntok_t *item = &t[i++];
						size_t len = 0;
						const char *s = json_string(json, item, &len);
				
						// add request to ring buffer
						size_t len2 = target_len + len + 2;
						char *ptr;
						if((ptr = varchunk_write_request(handle->rb, len2)))
						{
							strncpy(ptr, target, target_len);
							strncpy(ptr + target_len, s, len);
							ptr[target_len + len] = '!';
							ptr[target_len + len + 1] = 0;

							varchunk_write_advance(handle->rb, len2);
						}
						else if(handle->log)
							lv2_log_trace(&handle->logger, "eteroj#query: ringbuffer full");
					}
				}
				else if(!json_eq(json, key, "arguments"))
				{
					arg_n = value->size;

					for(int j = 0; j < value->size; j++)
					{
						jsmntok_t *item = &t[i++];

						for(int m = 0; m < item->size; m++)
						{
							jsmntok_t *item_key = &t[i++];
							jsmntok_t *item_val = &t[i++];

							if(!json_eq(json, item_key, "type"))
							{
								size_t len;
								const char *s = json_string(json, item_val, &len);

								if(j == 0)
									arg_type = s[0];
							}
							else if(!json_eq(json, item_key, "description"))
							{
								size_t len = 0;
								const char *s = json_string(json, item_val, &len);

								char dest [1024];
								strncpy(dest, s, len);
								dest[len] = 0;
							}
							else if(!json_eq(json, item_key, "read"))
							{
								const bool boolean = json_bool(json, item_val);

								if(j == 0)
									arg_read = boolean;
							}
							else if(!json_eq(json, item_key, "write"))
							{
								const bool boolean = json_bool(json, item_val);

								if(j == 0)
									arg_write = boolean;
							}
							else if(!json_eq(json, item_key, "range"))
							{
								arg_range_cnt = item_val->size;
								arg_range = item_val + 1;

								for(int n = 0; n < item_val->size; n++)
								{
									jsmntok_t *range = &t[i++]; 
								}
							}
							else if(!json_eq(json, item_key, "values"))
							{
								arg_values_cnt = item_val->size;
								arg_values = item_val + 1;

								for(int n = 0; n < item_val->size; n++)
								{
									jsmntok_t *val = &t[i++]; 
								}
							}
						}
					}
				}
			}

			// register methods in UI 
			if(!strncmp(type, "method", type_len) && (arg_n == 1) ) //FIXME handle all arg_n
			{
				char dest[1024];
				strncpy(dest, target, target_len);
				dest[target_len] = 0;
				LV2_URID property = handle->map->map(handle->map->handle, dest);

				_add(handle, 0, arg_type, arg_read, arg_write, json, arg_range_cnt, arg_range,
					arg_values_cnt, arg_values, property, target, target_len, desc, desc_len);
			}
		}
		else
		{
			// is this a reply with arguments?
			if(!lv2_atom_tuple_is_end(LV2_ATOM_BODY_CONST(body), body->atom.size, atom))
			{
				LV2_URID property = handle->map->map(handle->map->handle, destination);
				_set(handle, handle->frames, property, atom);
			}
		}
	}
	else if(!strcmp(path, "/error"))
	{
		int32_t id;
		const char *destination;
		const char *msg;

		atom = lv2_osc_int32_get(osc_urid, atom, &id);
		atom = lv2_osc_string_get(osc_urid, atom, &destination);
		atom = lv2_osc_string_get(osc_urid, atom, &msg);
		if(handle->log)
			lv2_log_trace(&handle->logger, "error: %s (%s)", destination, msg);
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	LV2_Atom_Forge *forge = &handle->forge;
	LV2_Atom_Forge_Frame frame;

	// prepare sequence forges
	const uint32_t capacity = handle->osc_out->atom.size;
	lv2_atom_forge_set_buffer(forge, (uint8_t *)handle->osc_out, capacity);
	handle->ref = lv2_atom_forge_sequence_head(forge, &frame, 0);

	props_idle(&handle->props, &handle->forge, 0, &handle->ref);

	LV2_ATOM_SEQUENCE_FOREACH(handle->osc_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
		const int64_t frames = ev->time.frames;

		if(lv2_osc_is_message_or_bundle_type(&handle->osc_urid, obj->body.otype))
		{
			handle->frames = frames;
			lv2_osc_unroll(&handle->osc_urid, obj, _message_cb, handle);
		}
		else if(obj->atom.type == forge->Object)
		{
			props_advance(&handle->props, forge, frames, obj, &handle->ref);

			if(obj->body.otype == handle->urid.patch_set)
			{
				const LV2_Atom_URID *subject = NULL;
				const LV2_Atom_URID *property = NULL;
				const LV2_Atom *value = NULL;

				lv2_atom_object_get(obj,
					handle->urid.patch_subject, &subject,
					handle->urid.patch_property, &property,
					handle->urid.patch_value, &value,
					0);

				if(!property || !value)
					continue;

				if(subject && (subject->body != handle->urid.subject))
					continue; // subject not matching

				const char *uri = handle->unmap->unmap(handle->unmap->handle, property->body);

				if( (value->type == forge->Int) || (value->type == forge->Bool) )
				{
					if(handle->ref)
						handle->ref = lv2_atom_forge_frame_time(forge, frames);
					if(handle->ref)
						handle->ref = lv2_osc_forge_message_vararg(forge, &handle->osc_urid, uri, "ii",
							handle->cnt++, ((const LV2_Atom_Int *)value)->body);
				}
				else if(value->type == forge->Long)
				{
					if(handle->ref)
						handle->ref = lv2_atom_forge_frame_time(forge, frames);
					if(handle->ref)
						handle->ref = lv2_osc_forge_message_vararg(forge, &handle->osc_urid, uri, "ih",
							handle->cnt++, ((const LV2_Atom_Long *)value)->body);
				}
				else if(value->type == forge->Float)
				{
					if(handle->ref)
						handle->ref = lv2_atom_forge_frame_time(forge, frames);
					if(handle->ref)
						handle->ref = lv2_osc_forge_message_vararg(forge, &handle->osc_urid, uri, "if",
							handle->cnt++, ((const LV2_Atom_Float *)value)->body);
				}
				else if(value->type == forge->Double)
				{
					if(handle->ref)
						handle->ref = lv2_atom_forge_frame_time(forge, frames);
					if(handle->ref)
						handle->ref = lv2_osc_forge_message_vararg(forge, &handle->osc_urid, uri, "id",
							handle->cnt++, ((const LV2_Atom_Double *)value)->body);
				}
				else if(value->type == forge->String)
				{
					if(handle->ref)
						handle->ref = lv2_atom_forge_frame_time(forge, frames);
					if(handle->ref)
						handle->ref = lv2_osc_forge_message_vararg(forge, &handle->osc_urid, uri, "is",
							handle->cnt++, LV2_ATOM_BODY_CONST(value));
				}
			}
			else if(obj->body.otype == handle->urid.patch_get)
			{
				const LV2_Atom_URID *subject = NULL;
				const LV2_Atom_URID *property = NULL;

				lv2_atom_object_get(obj,
					handle->urid.patch_subject, &subject,
					handle->urid.patch_property, &property,
					0);

				if(subject && (subject->body != handle->urid.subject))
					continue; // subject not matching

				if(!property)
				{
					_refresh(handle, frames);
				}
				else
				{
					const char *uri = handle->unmap->unmap(handle->unmap->handle, property->body);

					// schedule on ring buffer
					size_t len2 = strlen(uri) + 1;
					char *ptr;
					if((ptr = varchunk_write_request(handle->rb, len2)))
					{
						strncpy(ptr, uri, len2);
						varchunk_write_advance(handle->rb, len2);
					}
					else if(handle->log)
						lv2_log_trace(&handle->logger, "eteroj#query: ringbuffer full");
				}
			}
		}
	}

	{
		// send requests in ring buffer
		size_t len2;
		const char *ptr;
		int cnt = 0;
		if( (ptr = varchunk_read_request(handle->rb, &len2)) && (cnt++ < 10) ) //TODO how many?
		{
			if(handle->ref)
				handle->ref = lv2_atom_forge_frame_time(forge, nsamples-1);
			if(handle->ref)
				handle->ref = lv2_osc_forge_message_vararg(forge, &handle->osc_urid, ptr, "i", handle->cnt++);

			varchunk_read_advance(handle->rb);
		}
	}

	if(handle->ref)
		lv2_atom_forge_pop(forge, &frame);
	else
		lv2_atom_sequence_clear(handle->osc_out);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	if(handle->rb)
		varchunk_free(handle->rb);
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

static const void *
extension_data(const char *uri)
{
	if(!strcmp(uri, LV2_STATE__interface))
		return &state_iface;

	return NULL;
}

const LV2_Descriptor eteroj_query = {
	.URI						= ETEROJ_QUERY_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};
