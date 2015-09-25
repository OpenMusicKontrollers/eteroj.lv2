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

#ifndef _LIB_OSC_H_
#define _LIB_OSC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#if (defined(_WIN16) || defined(_WIN32) || defined(_WIN64)) && !defined(__WINDOWS__)
#	define __WINDOWS__
#endif

#if defined(__linux__) || defined(__CYGWIN__)
#	include <endian.h>

#elif defined(__APPLE__)
#	include <libkern/OSByteOrder.h>
#	define htobe16(x) OSSwapHostToBigInt16(x)
#	define htole16(x) OSSwapHostToLittleInt16(x)
#	define be16toh(x) OSSwapBigToHostInt16(x)
#	define le16toh(x) OSSwapLittleToHostInt16(x)
#	define htobe32(x) OSSwapHostToBigInt32(x)
#	define htole32(x) OSSwapHostToLittleInt32(x)
#	define be32toh(x) OSSwapBigToHostInt32(x)
#	define le32toh(x) OSSwapLittleToHostInt32(x)
#	define htobe64(x) OSSwapHostToBigInt64(x)
#	define htole64(x) OSSwapHostToLittleInt64(x)
#	define be64toh(x) OSSwapBigToHostInt64(x)
#	define le64toh(x) OSSwapLittleToHostInt64(x)
#	define __BYTE_ORDER    BYTE_ORDER
#	define __BIG_ENDIAN    BIG_ENDIAN
#	define __LITTLE_ENDIAN LITTLE_ENDIAN
#	define __PDP_ENDIAN    PDP_ENDIAN

#elif defined(__OpenBSD__) || defined(__FreeBSD__)
#	include <sys/endian.h>

#elif defined(__NetBSD__) || defined(__DragonFly__)
#	include <sys/endian.h>
#	define be16toh(x) betoh16(x)
#	define le16toh(x) letoh16(x)
#	define be32toh(x) betoh32(x)
#	define le32toh(x) letoh32(x)
#	define be64toh(x) betoh64(x)
#	define le64toh(x) letoh64(x)

#elif defined(__WINDOWS__)
#	include <winsock2.h>
#	include <sys/param.h>
#	if BYTE_ORDER == LITTLE_ENDIAN
#		define htobe16(x) __builtin_bswap16(x)
#		define htole16(x) (x)
#		define be16toh(x) __builtin_bswap16(x)
#		define le16toh(x) (x)
#		define htobe32(x) __builtin_bswap32(x)
#		define htole32(x) (x)
#		define be32toh(x) __builtin_bswap32(x)
#		define le32toh(x) (x)
#		define htobe64(x) __builtin_bswap64(x)
#		define htole64(x) (x)
#		define be64toh(x) __builtin_bswap64(x)
#		define le64toh(x) (x)
#	elif BYTE_ORDER == BIG_ENDIAN
		/* that would be xbox 360 */
#		define htobe16(x) (x)
#		define htole16(x) __builtin_bswap16(x)
#		define be16toh(x) (x)
#		define le16toh(x) __builtin_bswap16(x)
#		define htobe32(x) (x)
#		define htole32(x) __builtin_bswap32(x)
#		define be32toh(x) (x)
#		define le32toh(x) __builtin_bswap32(x)
#		define htobe64(x) (x)
#		define htole64(x) __builtin_bswap64(x)
#		define be64toh(x) (x)
#		define le64toh(x) __builtin_bswap64(x)
#	else
#		error byte order not supported
#	endif
#	define __BYTE_ORDER    BYTE_ORDER
#	define __BIG_ENDIAN    BIG_ENDIAN
#	define __LITTLE_ENDIAN LITTLE_ENDIAN
#	define __PDP_ENDIAN    PDP_ENDIAN

#else
#	error platform not supported
#endif

#define OSC_PADDED_SIZE(size) ( ( (size_t)(size) + 3 ) & ( ~3 ) )

#define OSC_IMMEDIATE 1ULL

typedef uint8_t osc_data_t;
typedef uint64_t osc_time_t;
typedef struct _osc_blob_t osc_blob_t;
typedef union _osc_argument_t osc_argument_t;

typedef int (*osc_method_cb_t)(osc_time_t time, const char *path, const char *fmt,
	const osc_data_t *arg, size_t size, void *data);
typedef void (*osc_bundle_in_cb_t)(osc_time_t time, void *data);
typedef void (*osc_bundle_out_cb_t)(osc_time_t time, void *data);
typedef struct _osc_method_t osc_method_t;

typedef void (*osc_unroll_stamp_inject_cb_t)(osc_time_t tstamp, void *data);
typedef void (*osc_unroll_message_inject_cb_t)(const osc_data_t *buf,
	size_t size, void *data);
typedef void (*osc_unroll_bundle_inject_cb_t)(const osc_data_t *buf,
	size_t size, void *data);
typedef struct _osc_unroll_inject_t osc_unroll_inject_t;

typedef union _swap32_t swap32_t;
typedef union _swap64_t swap64_t;

union _swap32_t {
	uint32_t u;

	int32_t i;
	float f;
};

union _swap64_t {
	uint64_t u;

	int64_t h;
	osc_time_t t;
	double d;
};

typedef enum _osc_type_t {
	OSC_INT32		=	'i',
	OSC_FLOAT		=	'f',
	OSC_STRING	=	's',
	OSC_BLOB		=	'b',
	
	OSC_TRUE		=	'T',
	OSC_FALSE		=	'F',
	OSC_NIL			=	'N',
	OSC_BANG		=	'I',
	
	OSC_INT64		=	'h',
	OSC_DOUBLE	=	'd',
	OSC_TIMETAG	=	't',
	
	OSC_SYMBOL	=	'S',
	OSC_CHAR		=	'c',
	OSC_MIDI		=	'm'
} osc_type_t;

struct _osc_method_t {
	const char *path;
	const char *fmt;
	const osc_method_cb_t cb;
};

struct _osc_blob_t {
	int32_t size;
	const void *payload;
};

union _osc_argument_t {
	int32_t i;
	float f;
	const char *s;
	osc_blob_t b;

	int64_t h;
	double d;
	osc_time_t t;

	const char *S;
	char c;
	const uint8_t *m;
};

typedef enum _osc_unroll_mode_t {
	OSC_UNROLL_MODE_NONE,
	OSC_UNROLL_MODE_PARTIAL,
	OSC_UNROLL_MODE_FULL
} osc_unroll_mode_t;

struct _osc_unroll_inject_t {
	osc_unroll_stamp_inject_cb_t stamp;
	osc_unroll_message_inject_cb_t message;
	osc_unroll_bundle_inject_cb_t bundle;
};

// characters not allowed in OSC path string
static const char invalid_path_chars [] = {
	' ', '#',
	'\0'
};

// allowed characters in OSC format string
static const char valid_format_chars [] = {
	OSC_INT32, OSC_FLOAT, OSC_STRING, OSC_BLOB,
	OSC_TRUE, OSC_FALSE, OSC_NIL, OSC_BANG,
	OSC_INT64, OSC_DOUBLE, OSC_TIMETAG,
	OSC_SYMBOL, OSC_CHAR, OSC_MIDI,
	'\0'
};

// check for valid path string
static inline int
osc_check_path(const char *path)
{
	const char *ptr;

	if(path[0] != '/')
		return 0;

	for(ptr=path+1; *ptr!='\0'; ptr++)
		if( (isprint(*ptr) == 0) || (strchr(invalid_path_chars, *ptr) != NULL) )
			return 0;

	return 1;
}

// check for valid format string 
static inline int
osc_check_fmt(const char *format, int offset)
{
	const char *ptr;

	if(offset)
		if(format[0] != ',')
			return 0;

	for(ptr=format+offset; *ptr!='\0'; ptr++)
		if(strchr(valid_format_chars, *ptr) == NULL)
			return 0;

	return 1;
}

// extract nested bundles with non-matching timestamps
static inline int
_unroll_partial(osc_data_t *buf, size_t size, const osc_unroll_inject_t *inject, void *data)
{
	if(strncmp((const char *)buf, "#bundle", 8)) // bundle header valid?
		return 0;

	const osc_data_t *end = buf + size;
	osc_data_t *ptr = buf;

	uint64_t timetag = be64toh(*(uint64_t *)(buf + 8));
	inject->stamp(timetag, data);

	int has_messages = 0;
	int has_nested_bundles = 0;

	ptr = buf + 16; // skip bundle header
	while(ptr < end)
	{
		int32_t *nsize = (int32_t *)ptr;
		int32_t hsize = be32toh(*nsize);
		ptr += sizeof(int32_t);

		char c = *(char *)ptr;
		switch(c)
		{
			case '#':
				has_nested_bundles = 1;
				if(!_unroll_partial(ptr, hsize, inject, data))
					return 0;
				break;
			case '/':
				has_messages = 1;
				// ignore for now
				break;
			default:
				return 0;
		}

		ptr += hsize;
	}

	if(!has_nested_bundles)
	{
		if(has_messages)
			inject->bundle(buf, size, data);
		return 1;
	}

	if(!has_messages)
		return 1; // discard empty bundles

	// repack bundle with messages only, ignoring nested bundles
	ptr = buf + 16; // skip bundle header
	osc_data_t *dst = ptr;
	while(ptr < end)
	{
		int32_t *nsize = (int32_t *)ptr;
		int32_t hsize = be32toh(*nsize);
		ptr += sizeof(int32_t);

		char *c = (char *)ptr;
		if(*c == '/')
		{
			memmove(dst, ptr - sizeof(int32_t), sizeof(int32_t) + hsize);
			dst += sizeof(int32_t) + hsize;
		}

		ptr += hsize;
	}

	size_t nlen = dst - buf; 
	inject->bundle(buf, nlen, data);

	return 1;
}

// fully unroll bundle into single messages
static inline int
_unroll_full(const osc_data_t *buf, size_t size, const osc_unroll_inject_t *inject, void *data)
{
	if(strncmp((char *)buf, "#bundle", 8)) // bundle header valid?
		return 0;

	const osc_data_t *end = buf + size;
	const osc_data_t *ptr = buf;

	uint64_t timetag = be64toh(*(const uint64_t *)(buf + 8));
	inject->stamp(timetag, data);

	int has_nested_bundles = 0;

	ptr = buf + 16; // skip bundle header
	while(ptr < end)
	{
		const int32_t *nsize = (const int32_t *)ptr;
		int32_t hsize = be32toh(*nsize);
		ptr += sizeof(int32_t);

		char c = *(const char *)ptr;
		switch(c)
		{
			case '#':
				has_nested_bundles = 1;
				// ignore for now, messages are handled first
				break;
			case '/':
				inject->message(ptr, hsize, data);
				break;
			default:
				return 0;
		}

		ptr += hsize;
	}

	if(!has_nested_bundles)
		return 1;

	ptr = buf + 16; // skip bundle header
	while(ptr < end)
	{
		const int32_t *nsize = (const int32_t *)ptr;
		int32_t hsize = be32toh(*nsize);
		ptr += sizeof(int32_t);

		const char *c = (const char *)ptr;
		if(*c == '#')
			if(!_unroll_full(ptr, hsize, inject, data))
				return 0;

		ptr += hsize;
	}

	return 1;
}

static inline int
osc_unroll_packet(osc_data_t *buf, size_t size, osc_unroll_mode_t mode,
	const osc_unroll_inject_t *inject, void *data)
{
	char c = *(char *)buf;
	switch(c)
	{
		case '#':
			switch(mode)
			{
				case OSC_UNROLL_MODE_NONE:
					inject->bundle(buf, size, data);
					break;
				case OSC_UNROLL_MODE_PARTIAL:
					if(!_unroll_partial(buf, size, inject, data))
						return 0;
					break;
				case OSC_UNROLL_MODE_FULL:
					if(!_unroll_full(buf, size, inject, data))
						return 0;
					break;
			}
			break;
		case '/':
			inject->message(buf, size, data);
			break;
		default:
			return 0;
	}

	return 1;
}

// OSC object lengths
static inline size_t
osc_strlen(const char *buf)
{
	return OSC_PADDED_SIZE(strlen(buf) + 1);
}

static inline size_t
osc_fmtlen(const char *buf)
{
	return OSC_PADDED_SIZE(strlen(buf) + 2) - 1;
}

static inline size_t
osc_blobsize(const osc_data_t *buf)
{
	swap32_t s = {.u = *(const uint32_t *)buf}; 
	s.u = be32toh(s.u);
	return s.i;
}

static inline size_t
osc_bloblen(const osc_data_t *buf)
{
	return 4 + OSC_PADDED_SIZE(osc_blobsize(buf));
}

// get OSC arguments from raw buffer
static inline const osc_data_t *
osc_get_path(const osc_data_t *buf, const char **path)
{
	if(!buf)
		return NULL;
	*path = (const char *)buf;
	return buf + osc_strlen(*path);
}

static inline const osc_data_t *
osc_get_fmt(const osc_data_t *buf, const char **fmt)
{
	if(!buf)
		return NULL;
	*fmt = (const char *)buf;
	return buf + osc_strlen(*fmt);
}

static inline const osc_data_t *
osc_get_int32(const osc_data_t *buf, int32_t *i)
{
	if(!buf)
		return NULL;
	swap32_t s = {.u = *(const uint32_t *)buf};
	s.u = be32toh(s.u);
	*i = s.i;
	return buf + 4;
}

static inline const osc_data_t *
osc_get_float(const osc_data_t *buf, float *f)
{
	if(!buf)
		return NULL;
	swap32_t s = {.u = *(const uint32_t *)buf};
	s.u = be32toh(s.u);
	*f = s.f;
	return buf + 4;
}

static inline const osc_data_t *
osc_get_string(const osc_data_t *buf, const char **s)
{
	if(!buf)
		return NULL;
	*s = (const char *)buf;
	return buf + osc_strlen(*s);
}

static inline const osc_data_t *
osc_get_blob(const osc_data_t *buf, osc_blob_t *b)
{
	if(!buf)
		return NULL;
	b->size = osc_blobsize(buf);
	b->payload = buf + 4;
	return buf + 4 + OSC_PADDED_SIZE(b->size);
}

static inline const osc_data_t *
osc_get_int64(const osc_data_t *buf, int64_t *h)
{
	if(!buf)
		return NULL;
	swap64_t s = {.u = *(const uint64_t *)buf};
	s.u = be64toh(s.u);
	*h = s.h;
	return buf + 8;
}

static inline const osc_data_t *
osc_get_double(const osc_data_t *buf, double *d)
{
	if(!buf)
		return NULL;
	swap64_t s = {.u = *(const uint64_t *)buf};
	s.u = be64toh(s.u);
	*d = s.d;
	return buf + 8;
}

static inline const osc_data_t *
osc_get_timetag(const osc_data_t *buf, osc_time_t *t)
{
	if(!buf)
		return NULL;
	swap64_t s = {.u = *(const uint64_t *)buf};
	s.u = be64toh(s.u);
	*t = s.t;
	return buf + 8;
}

static inline const osc_data_t *
osc_get_symbol(const osc_data_t *buf, const char **S)
{
	if(!buf)
		return NULL;
	*S = (const char *)buf;
	return buf + osc_strlen(*S);
}

static inline const osc_data_t *
osc_get_char(const osc_data_t *buf, char *c)
{
	if(!buf)
		return NULL;
	swap32_t s = {.u = *(const uint32_t *)buf};
	s.u = be32toh(s.u);
	*c = s.i & 0xff;
	return buf + 4;
}

static inline const osc_data_t *
osc_get_midi(const osc_data_t *buf, const uint8_t **m)
{
	if(!buf)
		return NULL;
	*m = (const uint8_t *)buf;
	return buf + 4;
}

static inline const osc_data_t *
osc_skip(osc_type_t type, const osc_data_t *buf)
{
	switch(type)
	{
		case OSC_INT32:
		case OSC_FLOAT:
		case OSC_MIDI:
		case OSC_CHAR:
			return buf + 4;

		case OSC_STRING:
		case OSC_SYMBOL:
			return buf + osc_strlen((const char *)buf);

		case OSC_BLOB:
			return buf + osc_bloblen(buf);

		case OSC_INT64:
		case OSC_DOUBLE:
		case OSC_TIMETAG:
			return buf + 8;

		case OSC_TRUE:
		case OSC_FALSE:
		case OSC_NIL:
		case OSC_BANG:
			return buf;

		default:
			return NULL;
	}
}

static inline const osc_data_t *
osc_get(osc_type_t type, const osc_data_t *buf, osc_argument_t *arg)
{
	switch(type)
	{
		case OSC_INT32:
			return osc_get_int32(buf, &arg->i);
		case OSC_FLOAT:
			return osc_get_float(buf, &arg->f);
		case OSC_STRING:
			return osc_get_string(buf, &arg->s);
		case OSC_BLOB:
			return osc_get_blob(buf, &arg->b);

		case OSC_INT64:
			return osc_get_int64(buf, &arg->h);
		case OSC_DOUBLE:
			return osc_get_double(buf, &arg->d);
		case OSC_TIMETAG:
			return osc_get_timetag(buf, &arg->t);

		case OSC_TRUE:
		case OSC_FALSE:
		case OSC_NIL:
		case OSC_BANG:
			return buf;

		case OSC_SYMBOL:
			return osc_get_symbol(buf, &arg->S);
		case OSC_CHAR:
			return osc_get_char(buf, &arg->c);
		case OSC_MIDI:
			return osc_get_midi(buf, &arg->m);

		default:
			return NULL;
	}
}

static inline const osc_data_t *
osc_get_vararg(const osc_data_t *buf, const char **path, const char **fmt, ...)
{
	const osc_data_t *ptr = buf;

  va_list args;
  va_start (args, fmt);

	ptr = osc_get_path(ptr, path);
	ptr = osc_get_fmt(ptr, fmt);

  const char *type;
  for(type=*fmt; *type != '\0'; type++)
		switch(*type)
		{
			case OSC_INT32:
				ptr = osc_get_int32(ptr, va_arg(args, int32_t *));
				break;
			case OSC_FLOAT:
				ptr = osc_get_float(ptr, va_arg(args, float *));
				break;
			case OSC_STRING:
				ptr = osc_get_string(ptr, va_arg(args, const char **));
				break;
			case OSC_BLOB:
				ptr = osc_get_blob(ptr, va_arg(args, osc_blob_t *));
				break;

			case OSC_INT64:
				ptr = osc_get_int64(ptr, va_arg(args, int64_t *));
				break;
			case OSC_DOUBLE:
				ptr = osc_get_double(ptr, va_arg(args, double *));
				break;
			case OSC_TIMETAG:
				ptr = osc_get_timetag(ptr, va_arg(args, uint64_t *));
				break;

			case OSC_TRUE:
			case OSC_FALSE:
			case OSC_NIL:
			case OSC_BANG:
				break;

			case OSC_SYMBOL:
				ptr = osc_get_symbol(ptr, va_arg(args, const char **));
				break;
			case OSC_CHAR:
				ptr = osc_get_char(ptr, va_arg(args, char *));
				break;
			case OSC_MIDI:
				ptr = osc_get_midi(ptr, va_arg(args, const uint8_t **));
				break;

			default:
				ptr = NULL;
				break;
		}

  va_end(args);

	return ptr;
}

static inline int
osc_check_message(const osc_data_t *buf, size_t size)
{
	const osc_data_t *ptr = buf;
	const osc_data_t *end = buf + size;

	const char *path = NULL;
	const char *fmt = NULL;

	ptr = osc_get_path(ptr, &path);
	if( (ptr > end) || !osc_check_path(path) )
		return 0;

	ptr = osc_get_fmt(ptr, &fmt);
	if( (ptr > end) || !osc_check_fmt(fmt, 1) )
		return 0;

	const char *type;
	for(type=fmt+1; (*type!='\0') && (ptr <= end); type++)
	{
		switch(*type)
		{
			case OSC_INT32:
			case OSC_FLOAT:
			case OSC_MIDI:
			case OSC_CHAR:
				ptr += 4;
				break;

			case OSC_STRING:
			case OSC_SYMBOL:
				ptr += osc_strlen((const char *)ptr);
				break;

			case OSC_BLOB:
				ptr += osc_bloblen(ptr);
				break;

			case OSC_INT64:
			case OSC_DOUBLE:
			case OSC_TIMETAG:
				ptr += 8;
				break;

			case OSC_TRUE:
			case OSC_FALSE:
			case OSC_NIL:
			case OSC_BANG:
				break;
		}
	}

	return ptr == end;
}

static inline int
osc_check_bundle(const osc_data_t *buf, size_t size)
{
	const osc_data_t *ptr = buf;
	const osc_data_t *end = buf + size;
	
	if(strncmp((const char *)ptr, "#bundle", 8)) // bundle header valid?
		return 0;
	ptr += 16; // skip bundle header

	while(ptr < end)
	{
		const int32_t *len = (const int32_t *)ptr;
		int32_t hlen = be32toh(*len);
		ptr += sizeof(int32_t);

		switch(*ptr)
		{
			case '#':
				if(!osc_check_bundle(ptr, hlen))
					return 0;
				break;
			case '/':
				if(!osc_check_message(ptr, hlen))
					return 0;
				break;
			default:
				return 0;
		}
		ptr += hlen;
	}

	return ptr == end;
}

static inline int
osc_check_packet(const osc_data_t *buf, size_t size)
{
	const osc_data_t *ptr = buf;
	
	switch(*ptr)
	{
		case '#':
			if(!osc_check_bundle(ptr, size))
				return 0;
			break;
		case '/':
			if(!osc_check_message(ptr, size))
				return 0;
			break;
		default:
			return 0;
	}

	return 1;
}

static inline int
osc_match_method(const osc_method_t *methods, const char *path, const char *fmt)
{
	const osc_method_t *meth;
	for(meth=methods; meth->cb; meth++)
		if(  (!meth->path || !strcmp(meth->path, path))
			&& (!meth->fmt || !strcmp(meth->fmt, fmt+1)) )
			return 1;
	return 0;
}

static inline void
_osc_method_dispatch_message(uint64_t time, const osc_data_t *buf, size_t size,
	const osc_method_t *methods, void *data)
{
	const osc_data_t *ptr = buf;

	const char *path = NULL;
	const char *fmt = NULL;

	ptr = osc_get_path(ptr, &path);
	ptr = osc_get_fmt(ptr, &fmt);

	const osc_method_t *meth;
	for(meth=methods; meth->cb; meth++)
	{
		if(  (!meth->path || !strcmp(meth->path, path))
			&& (!meth->fmt || !strcmp(meth->fmt, fmt+1)) )
		{
			if(meth->cb(time, path, fmt+1, ptr, size-(ptr-buf), data))
				break;
		}
	}
}

static inline void
_osc_method_dispatch_bundle(const osc_data_t *buf, size_t size,
	const osc_method_t *methods, osc_bundle_in_cb_t bundle_in,
	osc_bundle_out_cb_t bundle_out, void *data)
{
	const osc_data_t *ptr = buf;
	const osc_data_t *end = buf + size;

	uint64_t time = be64toh(*(const uint64_t *)(ptr + 8));
	ptr += 16; // skip bundle header

	if(bundle_in)
		bundle_in(time, data);

	while(ptr < end)
	{
		int32_t len = be32toh(*((const int32_t *)ptr));
		ptr += sizeof(int32_t);
		switch(*ptr)
		{
			case '#':
				_osc_method_dispatch_bundle(ptr, len, methods, bundle_in,
					bundle_out, data);
				break;
			case '/':
				_osc_method_dispatch_message(time, ptr, len, methods, data);
				break;
		}
		ptr += len;
	}

	if(bundle_out)
		bundle_out(time, data);
}

static inline void
osc_dispatch_method(const osc_data_t *buf, size_t size,
	const osc_method_t *methods, osc_bundle_in_cb_t bundle_in,
	osc_bundle_out_cb_t bundle_out, void *data)
{
	switch(*buf)
	{
		case '#':
			_osc_method_dispatch_bundle(buf, size, methods, bundle_in,
				bundle_out, data);
			break;
		case '/':
			_osc_method_dispatch_message(OSC_IMMEDIATE, buf, size, methods, data);
			break;
	}
}

// write OSC argument to raw buffer
static inline osc_data_t *
osc_set_path(osc_data_t *buf, const osc_data_t *end, const char *path)
{
	const size_t len = osc_strlen(path);
	if(!buf || (buf + len > end) )
		return NULL;
	strncpy((char *)buf, path, len);
	return buf + len;
}

static inline osc_data_t *
osc_set_fmt(osc_data_t *buf, const osc_data_t *end, const char *fmt)
{
	const size_t len = osc_fmtlen(fmt);
	if(!buf || (buf + len > end) )
		return NULL;
	*buf++ = ',';
	strncpy((char *)buf, fmt, len);
	return buf + len;
}

static inline osc_data_t *
osc_set_int32(osc_data_t *buf, const osc_data_t *end, int32_t i)
{
	if(!buf || (buf + 4 > end) )
		return NULL;
	swap32_t *s = (swap32_t *)buf;
	s->i = i;
	s->u = htobe32(s->u);
	return buf + 4;
}

static inline osc_data_t *
osc_set_float(osc_data_t *buf, const osc_data_t *end, float f)
{
	if(!buf || (buf + 4 > end) )
		return NULL;
	swap32_t *s = (swap32_t *)buf;
	s->f = f;
	s->u = htobe32(s->u);
	return buf + 4;
}

static inline osc_data_t *
osc_set_string(osc_data_t *buf, const osc_data_t *end, const char *s)
{
	const size_t len = osc_strlen(s);
	if(!buf || (buf + len > end) )
		return NULL;
	strncpy((char *)buf, s, len);
	return buf + len;
}

static inline osc_data_t *
osc_set_blob(osc_data_t *buf, const osc_data_t *end, int32_t size, const void *payload)
{
	const size_t len = OSC_PADDED_SIZE(size);
	if(!buf || (buf + 4 + len > end) )
		return NULL;
	swap32_t *s = (swap32_t *)buf;
	s->i = size;
	s->u = htobe32(s->u);
	memcpy(buf + 4, payload, size);
	memset(buf + 4 + size, '\0', len-size); // zero padding
	return buf + 4 + len;
}

static inline osc_data_t *
osc_set_blob_inline(osc_data_t *buf, const osc_data_t *end, int32_t size, void **payload)
{
	const size_t len = OSC_PADDED_SIZE(size);
	if(!buf || (buf + 4 + len > end) )
		return NULL;
	swap32_t *s = (swap32_t *)buf;
	s->i = size;
	s->u = htobe32(s->u);
	*payload = buf + 4;
	memset(buf + 4 + size, '\0', len-size); // zero padding
	return buf + 4 +len;
}

static inline osc_data_t *
osc_set_int64(osc_data_t *buf, const osc_data_t *end, int64_t h)
{
	if(!buf || (buf + 8 > end) )
		return NULL;
	swap64_t *s = (swap64_t *)buf;
	s->h = h;
	s->u = htobe64(s->u);
	return buf + 8;
}

static inline osc_data_t *
osc_set_double(osc_data_t *buf, const osc_data_t *end, double d)
{
	if(!buf || (buf + 8 > end) )
		return NULL;
	swap64_t *s = (swap64_t *)buf;
	s->d = d;
	s->u = htobe64(s->u);
	return buf + 8;
}

static inline osc_data_t *
osc_set_timetag(osc_data_t *buf, const osc_data_t *end, osc_time_t t)
{
	if(!buf || (buf + 8 > end) )
		return NULL;
	swap64_t *s = (swap64_t *)buf;
	s->t = t;
	s->u = htobe64(s->u);
	return buf + 8;
}

static inline osc_data_t *
osc_set_symbol(osc_data_t *buf, const osc_data_t *end, const char *S)
{
	const size_t len = osc_strlen(S);
	if(!buf || (buf + len > end) )
		return NULL;
	strncpy((char *)buf, S, len);
	return buf + len;
}

static inline osc_data_t *
osc_set_char(osc_data_t *buf, const osc_data_t *end, char c)
{
	int32_t i = c;
	return osc_set_int32(buf, end, i);
}

static inline osc_data_t *
osc_set_midi(osc_data_t *buf, const osc_data_t *end, const uint8_t *m)
{
	if(!buf || (buf + 4 > end) )
		return NULL;
	buf[0] = m[0];
	buf[1] = m[1];
	buf[2] = m[2];
	buf[3] = m[3];
	return buf + 4;
}

static inline osc_data_t *
osc_set_midi_inline(osc_data_t *buf, const osc_data_t *end, uint8_t **m)
{
	if(!buf || (buf + 4 > end) )
		return NULL;
	*m = buf;
	return buf + 4;
}

static inline osc_data_t *
osc_start_bundle(osc_data_t *buf, const osc_data_t *end, osc_time_t t, osc_data_t **bndl)
{
	if(!buf || (buf + 16 > end) )
		return NULL;
	*bndl = buf;
	strncpy((char *)buf, "#bundle", 8);
	buf += 8;
	return osc_set_timetag(buf, end, t);
}

static inline osc_data_t *
osc_end_bundle(osc_data_t *buf, const osc_data_t *end, osc_data_t *bndl)
{
	const size_t len =  buf - (bndl + 16);
	if(len > 0)
		return buf;
	else // empty bundle
		return bndl;
}

static inline osc_data_t *
osc_start_bundle_item(osc_data_t *buf, const osc_data_t *end, osc_data_t **itm)
{
	if(!buf || (buf + 4 > end) )
		return NULL;
	*itm = buf;
	return buf + 4;
}

static inline osc_data_t *
osc_end_bundle_item(osc_data_t *buf, const osc_data_t *end, osc_data_t *itm)
{
	const size_t len = buf - (itm + 4);
	if(len > 0)
	{
		osc_set_int32(itm, end, len);
		return buf;
	}
	else // empty item
		return itm;
}

static inline osc_data_t *
osc_set(osc_data_t *buf, const osc_data_t *end, osc_type_t type, osc_argument_t *arg)
{
	switch(type)
	{
		case OSC_INT32:
			return osc_set_int32(buf, end, arg->i);
		case OSC_FLOAT:
			return osc_set_float(buf, end, arg->f);
		case OSC_STRING:
			return osc_set_string(buf, end, arg->s);
		case OSC_BLOB:
			return osc_set_blob(buf, end, arg->b.size, arg->b.payload);

		case OSC_INT64:
			return osc_set_int64(buf, end, arg->h);
		case OSC_DOUBLE:
			return osc_set_double(buf, end, arg->d);
		case OSC_TIMETAG:
			return osc_set_timetag(buf, end, arg->t);

		case OSC_TRUE:
		case OSC_FALSE:
		case OSC_NIL:
		case OSC_BANG:
			return buf;

		case OSC_SYMBOL:
			return osc_set_symbol(buf, end, arg->S);
		case OSC_CHAR:
			return osc_set_char(buf, end, arg->c);
		case OSC_MIDI:
			return osc_set_midi(buf, end, arg->m);

		default:
			return NULL;
	}
}

static inline osc_data_t *
osc_set_varlist(osc_data_t *buf, const osc_data_t *end, const char *path,
	const char *fmt, va_list args)
{
	osc_data_t *ptr = buf;

	ptr = osc_set_path(ptr, end, path);
	ptr = osc_set_fmt(ptr, end, fmt);

  const char *type;
  for(type=fmt; *type != '\0'; type++)
		switch(*type)
		{
			case OSC_INT32:
				ptr = osc_set_int32(ptr, end, va_arg(args, int32_t));
				break;
			case OSC_FLOAT:
				ptr = osc_set_float(ptr, end, (float)va_arg(args, double));
				break;
			case OSC_STRING:
				ptr = osc_set_string(ptr, end, va_arg(args, const char *));
				break;
			case OSC_BLOB:
				ptr = osc_set_blob(ptr, end, va_arg(args, int32_t), va_arg(args, const void *));
				break;

			case OSC_INT64:
				ptr = osc_set_int64(ptr, end, va_arg(args, int64_t));
				break;
			case OSC_DOUBLE:
				ptr = osc_set_double(ptr, end, va_arg(args, double));
				break;
			case OSC_TIMETAG:
				ptr = osc_set_timetag(ptr, end, va_arg(args, uint64_t));
				break;

			case OSC_TRUE:
			case OSC_FALSE:
			case OSC_NIL:
			case OSC_BANG:
				break;

			case OSC_SYMBOL:
				ptr = osc_set_symbol(ptr, end, va_arg(args, const char *));
				break;
			case OSC_CHAR:
				ptr = osc_set_char(ptr, end, (char)va_arg(args, int));
				break;
			case OSC_MIDI:
				ptr = osc_set_midi(ptr, end, va_arg(args, const uint8_t *));
				break;

			default:
				ptr = NULL;
		}

	return ptr;
}

static inline osc_data_t *
osc_set_vararg(osc_data_t *buf, const osc_data_t *end, const char *path,
	const char *fmt, ...)
{
	osc_data_t *ptr = buf;

  va_list args;
  va_start (args, fmt);

	ptr = osc_set_varlist(ptr, end, path, fmt, args);

  va_end(args);

	return ptr;
}

static inline osc_data_t *
osc_set_bundle_item(osc_data_t *buf, const osc_data_t *end, const char *path,
	const char *fmt, ...)
{
	osc_data_t *ptr = buf;
	osc_data_t *itm;

  va_list args;
  va_start (args, fmt);

	ptr = osc_start_bundle_item(ptr, end, &itm);
	ptr = osc_set_varlist(ptr, end, path, fmt, args);
	ptr = osc_end_bundle_item(ptr, end, itm);

  va_end(args);

	return ptr;
}

#ifdef __cplusplus
}
#endif

#endif // _LIB_OSC_H_
