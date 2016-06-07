/*
 * Copyright (c) 2016 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/log/log.h>
#include <lv2/lv2plug.in/ns/ext/log/logger.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

#include <private_ui.h>
#include <lv2_external_ui.h> // kxstudio external-ui extension

#define ZYN_PREFIX	"http://zynaddsubfx.sourceforge.net"
#define ZYN_URI			ZYN_PREFIX"/ext_gui#"
#define ZYN_UI_URI	ZYN_URI"ui1_ui"
#define ZYN_KX_URI	ZYN_URI"ui2_kx"

typedef struct _ui_t ui_t;

struct _ui_t{
	LV2_URID_Map *map;

	LV2_Log_Log *log;
	LV2_Log_Logger logger;

	LV2UI_Write_Function write_function;
	LV2UI_Controller controller;

	LV2UI_Port_Map *port_map;
	uint32_t osc_port_index;
	uint16_t osc_port;
	char osc_port_uri [128];
	bool osc_port_wait;
	bool is_visible;

	int done;
	
	spawn_t spawn;

	struct {
		LV2_External_UI_Widget widget;
		const LV2_External_UI_Host *host;
	} kx;
};

static const LV2UI_Descriptor zyn_ui;
static const LV2UI_Descriptor zyn_kx;

// Show Interface
static inline int
_show_cb(LV2UI_Handle instance)
{
	ui_t *ui = instance;

	ui->is_visible = true;
	if(ui->osc_port_wait)
	{
		ui->done = 0; // we need to idle as until we're notified
		return 0; // we need to be notified about OSC port
	}

	if(!ui->done)
		return 0; // already showing

#if defined(_WIN32)
	const char *command = "cmd /c zynaddsubfx-ext-gui";
#else
	const char *command = "zynaddsubfx-ext-gui";
#endif

	snprintf(ui->osc_port_uri, 128, "osc.udp://localhost:%hu", ui->osc_port);

	// get default editor from environment
	char *dup = strdup(command);
	char **args = dup ? _spawn_parse_env(dup, ui->osc_port_uri) : NULL;

	const int status = _spawn_spawn(&ui->spawn, args);

	if(args)
		free(args);
	if(dup)
		free(dup);

	if(status)
		return -1; // failed to spawn

	ui->done = 0;

	return 0;
}

static inline int
_hide_cb(LV2UI_Handle instance)
{
	ui_t *ui = instance;

	if(_spawn_has_child(&ui->spawn))
	{
		_spawn_kill(&ui->spawn);

		_spawn_waitpid(&ui->spawn, true);

		_spawn_invalidate_child(&ui->spawn);
	}

	ui->done = 1;
	ui->is_visible = false;

	return 0;
}

static const LV2UI_Show_Interface show_ext = {
	.show = _show_cb,
	.hide = _hide_cb
};

// Idle interface
static inline int
_idle_cb(LV2UI_Handle instance)
{
	ui_t *ui = instance;

	if(_spawn_has_child(&ui->spawn))
	{
		int res;
		if((res = _spawn_waitpid(&ui->spawn, false)) < 0)
		{
			_spawn_invalidate_child(&ui->spawn);
			ui->done = 1; // xdg-open may return immediately
		}
	}

	return ui->done;
}

static const LV2UI_Idle_Interface idle_ext = {
	.idle = _idle_cb
};

// External-ui_t Interface
static inline void
_kx_run(LV2_External_UI_Widget *widget)
{
	ui_t *handle = (void *)widget - offsetof(ui_t, kx.widget);

	if(_idle_cb(handle))
	{
		if(handle->kx.host && handle->kx.host->ui_closed)
			handle->kx.host->ui_closed(handle->controller);
		_hide_cb(handle);
	}
}

static inline void
_kx_hide(LV2_External_UI_Widget *widget)
{
	ui_t *handle = (void *)widget - offsetof(ui_t, kx.widget);

	_hide_cb(handle);
}

static inline void
_kx_show(LV2_External_UI_Widget *widget)
{
	ui_t *handle = (void *)widget - offsetof(ui_t, kx.widget);

	_show_cb(handle);
}

static LV2UI_Handle
instantiate(const LV2UI_Descriptor *descriptor, const char *plugin_uri,
	const char *bundle_path, LV2UI_Write_Function write_function,
	LV2UI_Controller controller, LV2UI_Widget *widget,
	const LV2_Feature *const *features)
{
	ui_t *ui = calloc(1, sizeof(ui_t));
	if(!ui)
		return NULL;

	for(int i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			ui->map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_UI__portMap))
			ui->port_map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_EXTERNAL_UI__Host) && (descriptor == &zyn_kx))
			ui->kx.host = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			ui->log = features[i]->data;
	}

	ui->kx.widget.run = _kx_run;
	ui->kx.widget.show = _kx_show;
	ui->kx.widget.hide = _kx_hide;

	if(descriptor == &zyn_kx)
		*(LV2_External_UI_Widget **)widget = &ui->kx.widget;
	else
		*(void **)widget = NULL;

	if(!ui->map)
	{
		fprintf(stderr, "%s: Host does not support urid:map\n", descriptor->URI);
		free(ui);
		return NULL;
	}
	if(!ui->port_map)
	{
		fprintf(stderr, "%s: Host does not support ui:portMap\n", descriptor->URI);
		free(ui);
		return NULL;
	}

	lv2_log_logger_init(&ui->logger, ui->map, ui->log);
	ui->spawn.logger = &ui->logger;

	// query port index of "control" port
	ui->osc_port_index = ui->port_map->port_index(ui->port_map->handle, "osc_port");

	ui->write_function = write_function;
	ui->controller = controller;

	ui->osc_port_wait = true;
	ui->done = 1;

	return ui;
}

static void
cleanup(LV2UI_Handle handle)
{
	ui_t *ui = handle;
}

static void
port_event(LV2UI_Handle handle, uint32_t port_index, uint32_t buffer_size,
	uint32_t format, const void *buffer)
{
	ui_t *ui = handle;

	if(port_index == ui->osc_port_index)
	{
		ui->osc_port = *(const float *)buffer;

		if(ui->osc_port_wait)
		{
			ui->osc_port_wait = false;
			ui->done = 1; // reset flag
			if(ui->is_visible)
				_show_cb(ui);
		}
	}
}

static const void *
ui_extension_data(const char *uri)
{
	if(!strcmp(uri, LV2_UI__idleInterface))
		return &idle_ext;
	else if(!strcmp(uri, LV2_UI__showInterface))
		return &show_ext;
		
	return NULL;
}

static const LV2UI_Descriptor zyn_ui = {
	.URI						= ZYN_UI_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= ui_extension_data
};

static const LV2UI_Descriptor zyn_kx = {
	.URI						= ZYN_KX_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= NULL
};

#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
const LV2UI_Descriptor*
lv2ui_descriptor(uint32_t index)
{
	switch(index)
	{
		case 0:
			return &zyn_ui;
		case 1:
			return &zyn_kx;
		default:
			return NULL;
	}
}
