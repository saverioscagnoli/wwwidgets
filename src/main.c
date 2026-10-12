#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <stdio.h>
#include <webkit/webkit.h>
#include <glib.h>
#include <gdk/gdk.h>
#include <gio/gio.h>

#include "config.h"
#include "log.h"
#include "state.h"

static const gchar *APP_ID = "dev.svscagn.wwwidgets";
static const gchar *CONFIG_DIR_NAME = "wwwidgets";
static const gchar *CONFIG_FILE_NAME = "config.json";

static gint size_to_px(Size s, int monitor_px) {
    return s.is_percent ? (int)(monitor_px * s.value / 100.0) : (int)s.value;
}

static gboolean widget_on_monitor(const WidgetConfig *w, GdkMonitor *monitor) {
    if (w->all_monitors)
        return TRUE;

    const char *connector = gdk_monitor_get_connector(monitor);
    return connector != NULL && g_strv_contains((const gchar *const *)w->monitors, connector);
}

static GtkWindow *spawn_window(GtkApplication *app, AppState *state, WidgetConfig *w, guint n, GdkMonitor *monitor) {
    GtkWindow *window = GTK_WINDOW(gtk_application_window_new(app));

    GdkRectangle geo;
    gdk_monitor_get_geometry(monitor, &geo);
    gtk_window_set_default_size(window, size_to_px(w->width, geo.width), size_to_px(w->height, geo.height));

    // layer shell stuff
    gtk_layer_init_for_window(window);
    gtk_layer_set_namespace(window, w->namespace);
    gtk_layer_set_monitor(window, monitor);
    g_debug("widgets[%u]: layer shell initalized", n);

    gtk_layer_set_layer(window, w->layer);
    g_debug("widgets[%u]: set layer to %d", n, w->layer);

    for (int e = 0; e < GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER; e++) {
        gtk_layer_set_anchor(window, e, w->anchor[e]);
        gtk_layer_set_margin(window, e, w->margin[e]);
    }

    switch (w->exclusive) {
        case EXCLUSIVE_NONE: gtk_layer_set_exclusive_zone(window, 0); break;
        case EXCLUSIVE_FIXED: gtk_layer_set_exclusive_zone(window, w->exclusive_px); break;
        case EXCLUSIVE_AUTO: gtk_layer_auto_exclusive_zone_enable(window); break;
    }

    g_debug("widgets[%u]: set exclusive zone to %d", n, w->exclusive);

    GtkWidget *view = g_object_new(WEBKIT_TYPE_WEB_VIEW, "user-content-manager", state->content_manager, NULL);
    webkit_settings_set_enable_developer_extras(webkit_web_view_get_settings(WEBKIT_WEB_VIEW(view)), state->config->devtools);
    GdkRGBA transparent = { 0, 0, 0, 0 };
    webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(view), &transparent);

    g_ptr_array_add(state->views, view);


    g_autofree char *uri = g_filename_to_uri(w->path, NULL, NULL);
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(view), uri);
    gtk_window_set_child(window, view);

    gtk_window_present(window);
    return window;
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    AppState *state = user_data;

    if (state->monitors != NULL)
        return;

    state_init(state);
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, "window { background: none; }");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    g_debug("state initialized");

    for (guint m = 0; m < g_list_model_get_n_items(state->monitors); m++) {
        g_autoptr(GdkMonitor) monitor = g_list_model_get_item(state->monitors, m);
        GdkRectangle geo;

        gdk_monitor_get_geometry(monitor, &geo);
        g_info("detected monitor: %s (%dx%d) at %d,%d", gdk_monitor_get_connector(monitor), geo.width, geo.height, geo.x, geo.y);

        for (guint i = 0; i < state->config->widgets->len; i++) {
            WidgetConfig *w = g_ptr_array_index(state->config->widgets, i);

            if (widget_on_monitor(w, monitor)) {
                gint width = size_to_px(w->width, geo.width);
                gint height = size_to_px(w->height, geo.height);

                g_info("spawning widget (%dx%d) on %s, path: %s", width, height, gdk_monitor_get_connector(monitor), w->path);
                spawn_window(app, state, w, i, monitor);
            }
        }
    }
}

int main(int argc, char **argv) {
    log_init();

    const gchar *config_dir = g_get_user_config_dir();
    g_debug("user config: %s", config_dir);

    g_autofree gchar *config_file = g_build_filename(config_dir, CONFIG_DIR_NAME, CONFIG_FILE_NAME, NULL);

    AppState state = { 0 };
    state.config = g_new0(Config, 1);
    config_parse(state.config, config_file);

    GtkApplication *app = gtk_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &state);

    gint status = g_application_run(G_APPLICATION(app), argc, argv);

    state_clear(&state);
    g_object_unref(app);

    return status;
}
