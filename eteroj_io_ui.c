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

#include <eteroj.h>

#include <lv2_eo_ui.h>

typedef enum _eteroj_socket_t eteroj_socket_t;
typedef enum _eteroj_version_t eteroj_version_t;
typedef enum _eteroj_role_t eteroj_role_t;
typedef struct _plughandle_t plughandle_t;

enum _eteroj_socket_t {
	ETEROJ_SOCKET_UDP				= 0,
	ETEROJ_SOCKET_TCP				= 1,
	ETEROJ_SOCKET_TCP_SLIP	= 2,
	ETEROJ_SOCKET_PIPE			= 3
};

enum _eteroj_version_t {
	ETEROJ_VERSION_4				= 0,
	ETEROJ_VERSION_6				= 1
};

enum _eteroj_role_t {
	ETEROJ_ROLE_SERVER			= 0,
	ETEROJ_ROLE_CLIENT			= 1
};

static const char *uris [] = {
	[ETEROJ_SOCKET_UDP] = "osc.udp",
	[ETEROJ_SOCKET_TCP] = "osc.tcp",
	[ETEROJ_SOCKET_TCP_SLIP] = "osc.slip.tcp",
	[ETEROJ_SOCKET_PIPE] = "osc.pipe",
};

struct _plughandle_t {
	eo_ui_t eoui;

	struct {
		LV2_URID event_transfer;
		LV2_URID eteroj_event;
		LV2_URID eteroj_url;
	} uri;

	LV2_Atom_Forge forge;

	LV2_URID_Map *map;
	LV2UI_Write_Function write_function;
	LV2UI_Controller controller;

	LV2UI_Port_Map *port_map;
	uint32_t control_port;
	uint32_t notify_port;

	eteroj_socket_t socket;
	eteroj_version_t version;
	eteroj_role_t role;
	uint16_t port;
	char net_address [512];
	char dev_address [512];
	char url [512];

	Evas_Object *hbox;
	Evas_Object *addr;
	Evas_Object *entry;
	Evas_Object *button;
};

static void
_url_update(plughandle_t *handle)
{
	if(handle->socket == ETEROJ_SOCKET_PIPE)
	{
		sprintf(handle->url, "%s://%s",
			uris[handle->socket],
			handle->dev_address);
	}
	else
	{
		sprintf(handle->url, "%s%1i://%s:%hu",
			uris[handle->socket],
			handle->version == ETEROJ_VERSION_4
				? 4
				: 6,
			handle->role == ETEROJ_ROLE_SERVER
				? ""
				: handle->net_address,
			handle->port);
	}

	elm_object_text_set(handle->button, handle->url);

	if(handle->socket == ETEROJ_SOCKET_PIPE)
	{
		elm_object_disabled_set(handle->hbox, EINA_TRUE);
		elm_object_text_set(handle->addr, "Device Address");
		
		elm_object_disabled_set(handle->addr, EINA_FALSE);
	}
	else
	{
		elm_object_disabled_set(handle->hbox, EINA_FALSE);
		elm_object_text_set(handle->addr, "IP Address");

		elm_object_disabled_set(handle->addr, handle->role == ETEROJ_ROLE_SERVER
			? EINA_TRUE
			: EINA_FALSE);
	}
}

static void
_sock_changed(void *data, Evas_Object *obj, void *event_info)
{
	plughandle_t *handle = data;

	handle->socket = elm_radio_state_value_get(obj);
	
	if(handle->socket == ETEROJ_SOCKET_PIPE)
		elm_entry_entry_set(handle->entry, handle->dev_address);
	else
		elm_entry_entry_set(handle->entry, handle->net_address);

	_url_update(handle);
}

static void
_version_changed(void *data, Evas_Object *obj, void *event_info)
{
	plughandle_t *handle = data;

	handle->version = elm_radio_state_value_get(obj);

	_url_update(handle);
}

static void
_role_changed(void *data, Evas_Object *obj, void *event_info)
{
	plughandle_t *handle = data;

	handle->role = elm_radio_state_value_get(obj);

	_url_update(handle);
}

static void
_port_changed(void *data, Evas_Object *obj, void *event_info)
{
	plughandle_t *handle = data;

	const char *port = elm_entry_entry_get(obj);

	if(sscanf(port, "%hu", &handle->port) == 1)
		evas_object_color_set(obj, 0xff, 0xff, 0xff, 0xff);
	else
		evas_object_color_set(obj, 0xff, 0x00, 0x00, 0xff);
	
	_url_update(handle);
}

static void
_address_changed(void *data, Evas_Object *obj, void *event_info)
{
	plughandle_t *handle = data;

	const char *address = elm_entry_entry_get(obj);
	if(handle->socket == ETEROJ_SOCKET_PIPE)
		strcpy(handle->dev_address, address);
	else
		strcpy(handle->net_address, address);
	
	_url_update(handle);
}

static void
_connect(void *data, Evas_Object *obj, void *event_info)
{
	plughandle_t *handle = data;

	uint32_t len = strlen(handle->url) + 1;
	uint32_t size = sizeof(eteroj_event_t) + len;
	eteroj_event_t *ev = calloc(1, size);

	ev->obj.atom.type = handle->forge.Object;
	ev->obj.atom.size = size - sizeof(LV2_Atom);
	ev->obj.body.id = handle->uri.eteroj_event;
	ev->obj.body.otype = handle->uri.eteroj_url;
	ev->prop.key = handle->uri.eteroj_url;
	ev->prop.context = 0;
	ev->prop.value.type = handle->forge.String;
	ev->prop.value.size = len;
	strcpy(ev->url, handle->url);

	handle->write_function(handle->controller, handle->control_port, size,
		handle->uri.event_transfer, ev);
}

static void
_port_markup(void *data, Evas_Object *obj, char **txt)
{
	plughandle_t *handle = data;

	if(!isdigit(*txt[0]))
	{
		free(*txt);
		*txt = strdup("");
	}
}

static Evas_Object *
_content_get(eo_ui_t *eoui)
{
	plughandle_t *handle = (void *)eoui - offsetof(plughandle_t, eoui);

	Evas_Object *vbox = elm_box_add(eoui->win);
	elm_box_horizontal_set(vbox, EINA_FALSE);
	elm_box_homogeneous_set(vbox, EINA_FALSE);
	elm_box_padding_set(vbox, 0, 0);

	Evas_Object *sock = elm_frame_add(vbox);
	elm_object_text_set(sock, "Socket Type");
	evas_object_size_hint_weight_set(sock, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(sock, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(sock);
	elm_box_pack_end(vbox, sock);
	{
		Evas_Object *box = elm_box_add(sock);
		elm_box_horizontal_set(box, EINA_FALSE);
		elm_box_homogeneous_set(box, EINA_TRUE);
		elm_box_padding_set(box, 0, 0);
		evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(box, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(box);
		elm_object_content_set(sock, box);

		Evas_Object *udp = elm_radio_add(box);
		elm_radio_state_value_set(udp, ETEROJ_SOCKET_UDP);
		elm_object_text_set(udp, "UDP");
		evas_object_size_hint_align_set(udp, 0.f, EVAS_HINT_FILL);
		evas_object_smart_callback_add(udp, "changed", _sock_changed, handle);
		evas_object_show(udp);
		elm_box_pack_end(box, udp);

		Evas_Object *tcp = elm_radio_add(box);
		elm_radio_state_value_set(tcp, ETEROJ_SOCKET_TCP);
		elm_radio_group_add(tcp, udp);
		elm_object_text_set(tcp, "TCP (size prefix framed)");
		evas_object_size_hint_align_set(tcp, 0.f, EVAS_HINT_FILL);
		evas_object_smart_callback_add(tcp, "changed", _sock_changed, handle);
		evas_object_show(tcp);
		elm_box_pack_end(box, tcp);

		Evas_Object *tcp_slip = elm_radio_add(box);
		elm_radio_state_value_set(tcp_slip, ETEROJ_SOCKET_TCP_SLIP);
		elm_radio_group_add(tcp_slip, udp);
		elm_object_text_set(tcp_slip, "TCP (SLIP framed)");
		evas_object_size_hint_align_set(tcp_slip, 0.f, EVAS_HINT_FILL);
		evas_object_smart_callback_add(tcp_slip, "changed", _sock_changed, handle);
		evas_object_show(tcp_slip);
		elm_box_pack_end(box, tcp_slip);

		Evas_Object *pipe = elm_radio_add(box);
		elm_radio_state_value_set(pipe, ETEROJ_SOCKET_PIPE);
		elm_radio_group_add(pipe, udp);
		elm_object_text_set(pipe, "Serial (SLIP framed)");
		evas_object_size_hint_align_set(pipe, 0.f, EVAS_HINT_FILL);
		evas_object_smart_callback_add(pipe, "changed", _sock_changed, handle);
		evas_object_show(pipe);
		elm_box_pack_end(box, pipe);
	}

	Evas_Object *hbox = elm_box_add(vbox);
	elm_box_horizontal_set(hbox, EINA_TRUE);
	elm_box_homogeneous_set(hbox, EINA_TRUE);
	elm_box_padding_set(hbox, 0, 0);
	evas_object_size_hint_weight_set(hbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(hbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(hbox);
	elm_box_pack_end(vbox, hbox);
	handle->hbox = hbox;

	Evas_Object *ipv = elm_frame_add(hbox);
	elm_object_text_set(ipv, "IP Version");
	evas_object_size_hint_weight_set(ipv, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(ipv, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(ipv);
	elm_box_pack_end(hbox, ipv);
	{
		Evas_Object *box = elm_box_add(ipv);
		elm_box_horizontal_set(box, EINA_FALSE);
		elm_box_homogeneous_set(box, EINA_TRUE);
		elm_box_padding_set(box, 0, 0);
		evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(box, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(box);
		elm_object_content_set(ipv, box);

		Evas_Object *ipv4 = elm_radio_add(box);
		elm_radio_state_value_set(ipv4, ETEROJ_VERSION_4);
		elm_object_text_set(ipv4, "IPv4");
		evas_object_size_hint_align_set(ipv4, 0.f, EVAS_HINT_FILL);
		evas_object_smart_callback_add(ipv4, "changed", _version_changed, handle);
		evas_object_show(ipv4);
		elm_box_pack_end(box, ipv4);

		Evas_Object *ipv6 = elm_radio_add(box);
		elm_radio_state_value_set(ipv6, ETEROJ_VERSION_6);
		elm_radio_group_add(ipv6, ipv4);
		elm_object_text_set(ipv6, "IPv6");
		evas_object_size_hint_align_set(ipv6, 0.f, EVAS_HINT_FILL);
		evas_object_smart_callback_add(ipv6, "changed", _version_changed, handle);
		evas_object_show(ipv6);
		elm_box_pack_end(box, ipv6);
	}

	Evas_Object *role = elm_frame_add(hbox);
	elm_object_text_set(role, "IP Role");
	evas_object_size_hint_weight_set(role, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(role, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(role);
	elm_box_pack_end(hbox, role);
	{
		Evas_Object *box = elm_box_add(role);
		elm_box_horizontal_set(box, EINA_FALSE);
		elm_box_homogeneous_set(box, EINA_TRUE);
		elm_box_padding_set(box, 0, 0);
		evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(box, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(box);
		elm_object_content_set(role, box);

		Evas_Object *server = elm_radio_add(box);
		elm_radio_state_value_set(server, ETEROJ_ROLE_SERVER);
		elm_object_text_set(server, "Server");
		evas_object_size_hint_align_set(server, 0.f, EVAS_HINT_FILL);
		evas_object_smart_callback_add(server, "changed", _role_changed, handle);
		evas_object_show(server);
		elm_box_pack_end(box, server);

		Evas_Object *client = elm_radio_add(box);
		elm_radio_state_value_set(client, ETEROJ_ROLE_CLIENT);
		elm_radio_group_add(client, server);
		elm_object_text_set(client, "Client");
		evas_object_size_hint_align_set(client, 0.f, EVAS_HINT_FILL);
		evas_object_smart_callback_add(client, "changed", _role_changed, handle);
		evas_object_show(client);
		elm_box_pack_end(box, client);
	}

	Evas_Object *port = elm_frame_add(hbox);
	elm_object_text_set(port, "IP Port");
	evas_object_size_hint_weight_set(port, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(port, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(port);
	elm_box_pack_end(hbox, port);
	{
		Evas_Object *box = elm_box_add(port);
		elm_box_horizontal_set(box, EINA_FALSE);
		elm_box_homogeneous_set(box, EINA_TRUE);
		elm_box_padding_set(box, 0, 0);
		evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(box, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(box);
		elm_object_content_set(port, box);

		char buf [16];
		sprintf(buf, "%hu", handle->port);

		Evas_Object *entry = elm_entry_add(box);
		elm_entry_entry_set(entry, buf);
		elm_entry_single_line_set(entry, EINA_TRUE);
		elm_entry_editable_set(entry, EINA_TRUE);
		elm_entry_markup_filter_append(entry, _port_markup, handle);
		evas_object_size_hint_weight_set(entry, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(entry, 0.f, 0.f);
		evas_object_smart_callback_add(entry, "changed", _port_changed, handle);
		evas_object_show(entry);
		elm_box_pack_end(box, entry);
	}

	Evas_Object *addr = elm_frame_add(vbox);
	elm_object_text_set(addr, "IP Address");
	evas_object_size_hint_weight_set(addr, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(addr, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(addr);
	elm_box_pack_end(vbox, addr);
	handle->addr = addr;
	{
		Evas_Object *box = elm_box_add(addr);
		elm_box_horizontal_set(box, EINA_FALSE);
		elm_box_homogeneous_set(box, EINA_TRUE);
		elm_box_padding_set(box, 0, 0);
		evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(box, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(box);
		elm_object_content_set(addr, box);

		Evas_Object *entry = elm_entry_add(box);
		elm_entry_entry_set(entry, handle->net_address);
		elm_entry_single_line_set(entry, EINA_TRUE);
		elm_entry_editable_set(entry, EINA_TRUE);
		evas_object_size_hint_weight_set(entry, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(entry, 0.f, 0.f);
		evas_object_smart_callback_add(entry, "changed", _address_changed, handle);
		evas_object_show(entry);
		elm_box_pack_end(box, entry);
		handle->entry = entry;
	}

	Evas_Object *button = elm_button_add(vbox);
	elm_object_text_set(button, "connect");
	evas_object_size_hint_weight_set(button, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(button, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_smart_callback_add(button, "clicked", _connect, handle);
	evas_object_show(button);
	elm_box_pack_end(vbox, button);
	handle->button = button;
	
	_url_update(handle);

	return vbox;
}

static LV2UI_Handle
instantiate(const LV2UI_Descriptor *descriptor, const char *plugin_uri,
	const char *bundle_path, LV2UI_Write_Function write_function,
	LV2UI_Controller controller, LV2UI_Widget *widget,
	const LV2_Feature *const *features)
{
	if(strcmp(plugin_uri, ETEROJ_IO_URI))
		return NULL;

	eo_ui_driver_t driver;
	if(descriptor == &eteroj_io_eo)
		driver = EO_UI_DRIVER_EO;
	else if(descriptor == &eteroj_io_ui)
		driver = EO_UI_DRIVER_UI;
	else if(descriptor == &eteroj_io_x11)
		driver = EO_UI_DRIVER_X11;
	else if(descriptor == &eteroj_io_kx)
		driver = EO_UI_DRIVER_KX;
	else
		return NULL;

	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	strcpy(handle->net_address, "localhost");
	strcpy(handle->dev_address, "/dev/ttyACM0");
	handle->port = 9090;

	eo_ui_t *eoui = &handle->eoui;
	eoui->driver = driver;
	eoui->content_get = _content_get;
	eoui->w = 400,
	eoui->h = 400;

	handle->write_function = write_function;
	handle->controller = controller;

	for(int i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = (LV2_URID_Map *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_UI__portMap))
			handle->port_map = (LV2UI_Port_Map *)features[i]->data;
  }

	if(!handle->map)
	{
		free(handle);
		return NULL;
	}
	if(!handle->port_map)
	{
		fprintf(stderr, "%s: Host does not support ui:portMap\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	// query port index of "control" port
	handle->control_port = handle->port_map->port_index(handle->port_map->handle, "control");
	handle->notify_port = handle->port_map->port_index(handle->port_map->handle, "notify");

	handle->uri.event_transfer = handle->map->map(handle->map->handle,
		LV2_ATOM__eventTransfer);
	handle->uri.eteroj_event = handle->map->map(handle->map->handle,
		ETEROJ_EVENT_URI);
	handle->uri.eteroj_url = handle->map->map(handle->map->handle,
		ETEROJ_URL_URI);

	lv2_atom_forge_init(&handle->forge, handle->map);

	if(eoui_instantiate(eoui, descriptor, plugin_uri, bundle_path, write_function,
		controller, widget, features))
	{
		free(handle);
		return NULL;
	}
	
	uint32_t len = strlen("?") + 1;
	uint32_t size = sizeof(eteroj_event_t) + len;
	eteroj_event_t *ev = calloc(1, size);

	ev->obj.atom.type = handle->forge.Object;
	ev->obj.atom.size = size - sizeof(LV2_Atom);
	ev->obj.body.id = handle->uri.eteroj_event;
	ev->obj.body.otype = handle->uri.eteroj_url;
	ev->prop.key = handle->uri.eteroj_url;
	ev->prop.context = 0;
	ev->prop.value.type = handle->forge.String;
	ev->prop.value.size = len;
	strcpy(ev->url, "?");

	// request current url
	handle->write_function(handle->controller, handle->control_port, size,
		handle->uri.event_transfer, ev);
	
	return handle;
}

static void
cleanup(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;
	if(!handle)
		return;

	eoui_cleanup(&handle->eoui);
	free(handle);
}

static void
port_event(LV2UI_Handle instance, uint32_t port_index, uint32_t size,
	uint32_t format, const void *buffer)
{
	plughandle_t *handle = instance;
			
	if(port_index == handle->notify_port)
	{
		const eteroj_event_t *et = (const eteroj_event_t *)buffer;

		if(  (et->obj.atom.type == handle->forge.Object)
			&& (et->obj.body.id == handle->uri.eteroj_event)
			&& (et->obj.body.otype == handle->uri.eteroj_url) )
		{
			printf("notify: %s\n", et->url);
			//TODO
		}
	}
}

const LV2UI_Descriptor eteroj_io_eo = {
	.URI						= ETEROJ_IO_EO_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_eo_extension_data
};

const LV2UI_Descriptor eteroj_io_ui = {
	.URI						= ETEROJ_IO_UI_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_ui_extension_data
};

const LV2UI_Descriptor eteroj_io_x11 = {
	.URI						= ETEROJ_IO_X11_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_x11_extension_data
};

const LV2UI_Descriptor eteroj_io_kx = {
	.URI						= ETEROJ_IO_KX_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= eoui_kx_extension_data
};
