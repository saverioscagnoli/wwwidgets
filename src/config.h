#pragma once

#include <glib.h>
#include <gtk4-layer-shell.h>


typedef struct {
    gdouble value;
    gboolean is_percent;
} Size;

typedef enum {
    EXCLUSIVE_NONE,
    EXCLUSIVE_AUTO,
    EXCLUSIVE_FIXED,
} ExclusiveMode;

typedef struct {
    gchar *path;
    gchar *namespace;
    gboolean all_monitors;
    GStrv monitors;
    Size width;
    Size height;
    GtkLayerShellLayer layer;
    gboolean anchor[GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER];
    gint margin[GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER];
    ExclusiveMode exclusive;
    int exclusive_px;
    GtkLayerShellKeyboardMode keyboard;
    gchar *visible_when;
} WidgetConfig;

static inline void widget_config_free(WidgetConfig *w) {
    g_free(w->path);
    g_free(w->namespace);
    g_free(w->visible_when);
    g_strfreev(w->monitors);
    g_free(w);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(WidgetConfig, widget_config_free)

typedef struct {
    gboolean devtools;
    gboolean workspaces;
    gboolean notifications;
    gboolean tray;
    gboolean audio;
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
