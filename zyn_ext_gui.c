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

#include <uv.h>
#include <lv2_external_ui.h> // kxstudio external-ui extension

#define ZYN_PREFIX	"http://zynaddsubfx.sourceforge.net"
#define ZYN_URI			ZYN_PREFIX"/ext_gui#"
#define ZYN_UI_URI	ZYN_URI"ui"
#define ZYN_KX_URI	ZYN_URI"kx"

typedef struct _UI UI;

struct _UI {
	LV2_Log_Log *log;
	LV2_Log_Logger logger;

	LV2_URID_Map *map;

	LV2UI_Write_Function write_function;
	LV2UI_Controller controller;

	LV2UI_Port_Map *port_map;
	uint32_t osc_port_index;
	uint16_t osc_port;
	char osc_port_uri [128];
	bool osc_port_wait;

	uv_loop_t loop;
	uv_process_t req;
	uv_process_options_t opts;
	int done;

	struct {
		LV2_External_UI_Widget widget;
		const LV2_External_UI_Host *host;
	} kx;
};

static const LV2UI_Descriptor zyn_ui;
static const LV2UI_Descriptor zyn_kx;

static inline void
_err2(UI *ui, const char *from)
{
	if(ui->log)
		lv2_log_error(&ui->logger, "%s", from);
	else
		fprintf(stderr, "%s\n", from);
}

static inline void
_err(UI *ui, const char *from, int ret)
{
	if(ui->log)
		lv2_log_error(&ui->logger, "%s: %s", from, uv_strerror(ret));
	else
		fprintf(stderr, "%s: %s\n", from, uv_strerror(ret));
}

static inline void
_hide(UI *ui)
{
	int ret;

	if(uv_is_active((uv_handle_t *)&ui->req))
	{
		if((ret = uv_process_kill(&ui->req, SIGKILL)))
			_err(ui, "uv_process_kill", ret);
	}
	if(!uv_is_closing((uv_handle_t *)&ui->req))
		uv_close((uv_handle_t *)&ui->req, NULL);

	uv_stop(&ui->loop);
	uv_run(&ui->loop, UV_RUN_DEFAULT); // cleanup
	
	ui->done = 0;
}

static void
_on_exit(uv_process_t *req, int64_t exit_status, int term_signal)
{
	UI *ui = req ? (void *)req - offsetof(UI, req) : NULL;
	if(!ui)
		return;

	ui->done = 1;

	if(ui->kx.host && ui->kx.host->ui_closed && ui->controller)
	{
		_hide(ui);
		ui->kx.host->ui_closed(ui->controller);
	}
}

static inline char **
_parse_env(char *env, char *path)
{
	unsigned n = 0;
	char **args = malloc((n+1) * sizeof(char *));
	if(!args)
		goto fail;
	args[n] = NULL;

	char *pch = strtok(env," \t");
	while(pch)
	{
		args[n++] = pch;
		args = realloc(args, (n+1) * sizeof(char *));
		if(!args)
			goto fail;
		args[n] = NULL;

		pch = strtok(NULL, " \t");
	}

	args[n++] = path;
	args = realloc(args, (n+1) * sizeof(char *));
	if(!args)
		goto fail;
	args[n] = NULL;

	return args;

fail:
	if(args)
		free(args);
	return NULL;
}

static inline void
_show(UI *ui)
{
	if(ui->osc_port_wait)
		return;

#if defined(_WIN32)
	const char *command = "cmd /c zynaddsubfx-ext-gui";
#else // Linux/BSD
	const char *command = "zynaddsubfx-ext-gui";
#endif

	snprintf(ui->osc_port_uri, 128, "osc.udp://localhost:%hu", ui->osc_port);

	// get default editor from environment
	char *dup = strdup(command);
	char **args = dup ? _parse_env(dup, ui->osc_port_uri) : NULL;
	
	ui->opts.exit_cb = _on_exit;
	ui->opts.file = args ? args[0] : NULL;
	ui->opts.args = args;
#if defined(_WIN32)
	ui->opts.flags = UV_PROCESS_WINDOWS_HIDE | UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS;
#endif

	if(!uv_is_active((uv_handle_t *)&ui->req))
	{
		int ret;
		if((ret = uv_spawn(&ui->loop, &ui->req, &ui->opts)))
			_err(ui, "uv_spawn", ret);
	}

	if(dup)
		free(dup);
	if(args)
		free(args);
}

// External-UI Interface
static inline void
_kx_run(LV2_External_UI_Widget *widget)
{
	UI *ui = widget ? (void *)widget - offsetof(UI, kx.widget) : NULL;
	if(ui)
		uv_run(&ui->loop, UV_RUN_NOWAIT);
}

static inline void
_kx_hide(LV2_External_UI_Widget *widget)
{
	UI *ui = widget ? (void *)widget - offsetof(UI, kx.widget) : NULL;
	if(ui)
		_hide(ui);
}

static inline void
_kx_show(LV2_External_UI_Widget *widget)
{
	UI *ui = widget ? (void *)widget - offsetof(UI, kx.widget) : NULL;
	if(ui)
		_show(ui);
}

// Show Interface
static inline int
_show_cb(LV2UI_Handle instance)
{
	UI *ui = instance;
	if(ui)
		_show(ui);
	return 0;
}

static inline int
_hide_cb(LV2UI_Handle instance)
{
	UI *ui = instance;
	if(ui)
		_hide(ui);
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
	UI *ui = instance;
	if(!ui)
		return -1;
	uv_run(&ui->loop, UV_RUN_NOWAIT);
	return ui->done;
}

static const LV2UI_Idle_Interface idle_ext = {
	.idle = _idle_cb
};

static LV2UI_Handle
instantiate(const LV2UI_Descriptor *descriptor, const char *plugin_uri,
	const char *bundle_path, LV2UI_Write_Function write_function,
	LV2UI_Controller controller, LV2UI_Widget *widget,
	const LV2_Feature *const *features)
{
	if(strcmp(plugin_uri, "http://zynaddsubfx.sourceforge.net"))
		return NULL;

	UI *ui = calloc(1, sizeof(UI));
	if(!ui)
		return NULL;

	for(int i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			ui->map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_UI__portMap))
			ui->port_map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			ui->log = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_EXTERNAL_UI__Host) && (descriptor == &zyn_kx))
			ui->kx.host = features[i]->data;
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

	// query port index of "control" port
	ui->osc_port_index = ui->port_map->port_index(ui->port_map->handle, "osc_port");

	if(ui->log)
		lv2_log_logger_init(&ui->logger, ui->map, ui->log);

	ui->write_function = write_function;
	ui->controller = controller;

	int ret;
	if((ret = uv_loop_init(&ui->loop)))
	{
		fprintf(stderr, "%s: %s\n", descriptor->URI, uv_strerror(ret));
		free(ui);
		return NULL;
	}

	ui->osc_port_wait = true;

	return ui;
}

static void
cleanup(LV2UI_Handle handle)
{
	UI *ui = handle;

	uv_loop_close(&ui->loop);
}

static void
port_event(LV2UI_Handle handle, uint32_t port_index, uint32_t buffer_size,
	uint32_t format, const void *buffer)
{
	UI *ui = handle;

	if(port_index == ui->osc_port_index)
	{
		ui->osc_port = *(const float *)buffer;

		if(ui->osc_port_wait)
		{
			ui->osc_port_wait = false;
			_show(ui);
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
