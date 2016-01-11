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
#include "lv2/lv2plug.in/ns/ext/log/logger.h"
#include "lv2/lv2plug.in/ns/ext/patch/patch.h"
#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"
#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define ETEROJ_URI										"http://open-music-kontrollers.ch/lv2/eteroj"

#define ETEROJ_DRAIN_URI							ETEROJ_URI"#drain"

#define ETEROJ_EVENT_URI							ETEROJ_URI"#event"
#define ETEROJ_URL_URI								ETEROJ_URI"#url"
#define ETEROJ_STAT_URI								ETEROJ_URI"#status"
#define ETEROJ_ERR_URI								ETEROJ_URI"#error"

#define ETEROJ_DISK_RECORD_URI				ETEROJ_URI"#disk_record"
#define ETEROJ_DISK_PATH_URI					ETEROJ_URI"#disk_path"

#define ETEROJ_PACK_PATH_URI					ETEROJ_URI"#pack_path"

// plugin uris
#define ETEROJ_IO_URI									ETEROJ_URI"#io"
#define ETEROJ_DISK_URI								ETEROJ_URI"#disk"
#define ETEROJ_QUERY_URI							ETEROJ_URI"#query"
#define ETEROJ_CLOAK_URI							ETEROJ_URI"#cloak"
#define ETEROJ_PACK_URI								ETEROJ_URI"#pack"

extern const LV2_Descriptor eteroj_io;
extern const LV2_Descriptor eteroj_disk;
extern const LV2_Descriptor eteroj_query;
extern const LV2_Descriptor eteroj_cloak;
extern const LV2_Descriptor eteroj_pack;

#endif // _ETEROJ_LV2_H
