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
#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"
#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define ETEROJ_URI										"http://open-music-kontrollers.ch/lv2/eteroj"

// state keys
#define ETEROJ_TRIG_URI								ETEROJ_URI"#trig"
#define ETEROJ_DIRTY_URI							ETEROJ_URI"#dirty"

// worker keys
#define ETEROJ_URL_URI								ETEROJ_URI"#url"

// plugin uris
#define ETEROJ_IO_URI									ETEROJ_URI"#io"

#define ETEROJ_IO_EO_URI							ETEROJ_URI"#io_eo"
#define ETEROJ_IO_UI_URI							ETEROJ_URI"#io_ui"
#define ETEROJ_IO_KX_URI							ETEROJ_URI"#io_kx"
#define ETEROJ_IO_X11_URI							ETEROJ_URI"#io_x11"

extern const LV2_Descriptor eteroj_io;

extern const LV2UI_Descriptor eteroj_io_eo;
extern const LV2UI_Descriptor eteroj_io_ui;
extern const LV2UI_Descriptor eteroj_io_kx;
extern const LV2UI_Descriptor eteroj_io_x11;

#endif // _ETEROJ_LV2_H
