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

#ifndef _ETEROJ_LV2_H
#define _ETEROJ_LV2_H

#include <stdint.h>

#include "lv2/lv2plug.in/ns/ext/atom/atom.h"
#include "lv2/lv2plug.in/ns/ext/atom/forge.h"
#include "lv2/lv2plug.in/ns/ext/midi/midi.h"
#include "lv2/lv2plug.in/ns/ext/urid/urid.h"
#include "lv2/lv2plug.in/ns/ext/worker/worker.h"
#include "lv2/lv2plug.in/ns/ext/state/state.h"
#include "lv2/lv2plug.in/ns/ext/log/log.h"
#include "lv2/lv2plug.in/ns/ext/patch/patch.h"
#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"
#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define ETEROJ_URI										"http://open-music-kontrollers.ch/lv2/eteroj"

#define ETEROJ_TRIG_URI								ETEROJ_URI"#trig"
#define ETEROJ_DIRTY_URI							ETEROJ_URI"#dirty"

#define ETEROJ_EVENT_URI							ETEROJ_URI"#event"
#define ETEROJ_URL_URI								ETEROJ_URI"#url"
#define ETEROJ_ERR_URI								ETEROJ_URI"#err"

// plugin uris
#define ETEROJ_IO_URI									ETEROJ_URI"#io"

extern const LV2_Descriptor eteroj_io;

#define _ATOM_ALIGNED __attribute__((aligned(8)))

typedef struct _eteroj_event_t eteroj_event_t;

struct _eteroj_event_t {
	LV2_Atom_Object obj _ATOM_ALIGNED;
	LV2_Atom_Property_Body prop _ATOM_ALIGNED;
		char url [0] _ATOM_ALIGNED;
} _ATOM_ALIGNED;

#endif // _ETEROJ_LV2_H
