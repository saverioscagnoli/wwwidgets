#pragma once

#include <glib.h>

typedef struct {
    gdouble value;
    gboolean is_percent;
} Size;

typedef struct {
    gchar *path;
    gchar *namespace;
    gboolean all_monitors;
    GStrv monitors;
    Size width;
    Size height;
} WidgetConfig;

static inline void widget_config_free(WidgetConfig *w) {
    g_free(w->path);
    g_free(w->namespace);
    g_strfreev(w->monitors);
    g_free(w);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(WidgetConfig, widget_config_free)

typedef struct {
    gboolean devtools;
    gboolean workspaces;
    gboolean notifications;
    gboolean tray;
    gboolean bluetooth;
    gboolean network;
    gboolean battery;
    GPtrArray *widgets;
} Config;


void config_parse(Config *config, const gchar *path);

static inline void config_free(Config *c) {
    if (c->widgets != NULL) {
        g_ptr_array_unref(c->widgets);
    }
    g_free(c);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(Config, config_free)
