#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <signal.h>
#include <stdio.h>
#include <stdnoreturn.h>
#include <webkit/webkit.h>
#include <glib.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>

#include "config.h"
#include "log.h"
#include "reload.h"
#include "state.h"
#include "store.h"

static const gchar *APP_ID = "dev.svscagn.wwwidgets";
static const gchar *CONFIG_DIR_NAME = "wwwidgets";
static const gchar *CONFIG_FILE_NAME = "config.json";

static gboolean on_context_menu(WebKitWebView *view, WebKitContextMenu *menu, WebKitHitTestResult *hit, gpointer user_data) {
    (void)view;
    (void)hit;

    AppState *state = user_data;
    GdkEvent *event = webkit_context_menu_get_event(menu);
    gboolean shift = event && (gdk_event_get_modifier_state(event) & GDK_SHIFT_MASK);

    return !(state->config->devtools && shift);
}

static gint size_to_px(Size s, int monitor_px) {
    return s.is_percent ? (int)(monitor_px * s.value / 100.0) : (int)s.value;
}

static gboolean widget_on_monitor(const WidgetConfig *w, GdkMonitor *monitor) {
    if (w->all_monitors)
        return TRUE;

    const char *connector = gdk_monitor_get_connector(monitor);
    return connector != NULL && g_strv_contains((const gchar *const *)w->monitors, connector);
}

static void apply_initial_visibility(gpointer user_data) {
    GtkWindow *window = user_data;
    AppState *state = g_object_get_data(G_OBJECT(window), "state");

    store_apply_visibility(state, window);
    g_object_unref(window);
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

    gtk_layer_set_keyboard_mode(window, w->keyboard);

    const gchar *connector = gdk_monitor_get_connector(monitor);
    g_object_set_data_full(G_OBJECT(window), "win", g_strdup_printf("widget-%u|%s", n, connector ? connector : ""), g_free);
    g_ptr_array_add(state->windows, window);

    if (w->path == NULL) {
        gtk_window_present(window);
        window_set_input_region(window, "[]");
        return window;
    }

    WebKitWebView *first = state->views->len ? g_ptr_array_index(state->views, 0) : NULL;
    GtkWidget *view = first
        ? g_object_new(WEBKIT_TYPE_WEB_VIEW, "related-view", first, "user-content-manager", state->content_manager, NULL)
        : g_object_new(WEBKIT_TYPE_WEB_VIEW, "user-content-manager", state->content_manager, NULL);
    webkit_settings_set_enable_developer_extras(webkit_web_view_get_settings(WEBKIT_WEB_VIEW(view)), state->config->devtools);
    GdkRGBA transparent = { 0, 0, 0, 0 };
    webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(view), &transparent);
    g_signal_connect(view, "context-menu", G_CALLBACK(on_context_menu), state);

    g_ptr_array_add(state->views, view);
    g_object_set_data(G_OBJECT(window), "view", view);
    g_object_set_data(G_OBJECT(view), "widget", GUINT_TO_POINTER(n));

    g_autofree char *entry = g_path_get_basename(w->path);
    g_autofree char *monitor_arg = g_uri_escape_string(connector ? connector : "", NULL, FALSE);
    g_autofree char *uri = g_strdup_printf("wwwidgets://widget-%u/%s?monitor=%s", n, entry, monitor_arg);
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(view), uri);
    gtk_window_set_child(window, view);

    gtk_window_present(window);

    if (w->visible_when) {
        g_autoptr(GString) key = g_string_new(w->visible_when);
        g_string_replace(key, "{monitor}", connector ? connector : "", 0);

        g_object_set_data_full(G_OBJECT(window), "visible-when", g_string_free(g_steal_pointer(&key), FALSE), g_free);
        g_object_set_data(G_OBJECT(window), "keyboard", GINT_TO_POINTER(w->keyboard));
        g_object_set_data(G_OBJECT(window), "hide", GINT_TO_POINTER(w->hide));
        g_object_set_data(G_OBJECT(window), "state", state);
        g_idle_add_once(apply_initial_visibility, g_object_ref(window));
    }

    return window;
}

static void spawn_monitor(AppState *state, GdkMonitor *monitor) {
    GPtrArray *windows = g_ptr_array_new();
    GdkRectangle geo;

    gdk_monitor_get_geometry(monitor, &geo);
    g_info("detected monitor: %s (%dx%d) at %d,%d", gdk_monitor_get_connector(monitor), geo.width, geo.height, geo.x, geo.y);

    for (guint i = 0; i < state->config->widgets->len; i++) {
        WidgetConfig *w = g_ptr_array_index(state->config->widgets, i);

        if (!widget_on_monitor(w, monitor))
            continue;

        g_info("spawning widget (%dx%d) on %s, path: %s", size_to_px(w->width, geo.width), size_to_px(w->height, geo.height),
               gdk_monitor_get_connector(monitor), w->path ? w->path : "(spacer)");
        g_ptr_array_add(windows, spawn_window(state->app, state, w, i, monitor));
    }

    g_hash_table_insert(state->monitor_windows, g_object_ref(monitor), windows);
}

static void destroy_monitor(AppState *state, GdkMonitor *monitor) {
    GPtrArray *windows = g_hash_table_lookup(state->monitor_windows, monitor);

    g_info("monitor gone, destroying %u window(s)", windows ? windows->len : 0);

    for (guint i = 0; windows && i < windows->len; i++) {
        GtkWindow *window = g_ptr_array_index(windows, i);
        GtkWidget *view = g_object_get_data(G_OBJECT(window), "view");

        if (view)
            g_ptr_array_remove(state->views, view);

        g_ptr_array_remove(state->windows, window);
        gtk_window_destroy(window);
    }

    g_hash_table_remove(state->monitor_windows, monitor);
}

static void publish_monitors(AppState *state) {
    g_autoptr(JsonBuilder) b = json_builder_new();
    GHashTableIter it;
    gpointer monitor;

    json_builder_begin_array(b);
    g_hash_table_iter_init(&it, state->monitor_windows);

    while (g_hash_table_iter_next(&it, &monitor, NULL)) {
        json_builder_add_string_value(b, gdk_monitor_get_connector(monitor));
    }

    json_builder_end_array(b);

    g_autoptr(JsonNode) root = json_builder_get_root(b);
    g_autofree gchar *json = json_to_string(root, FALSE);

    store_set(state, "monitors", json);
}

static void sync_monitors(AppState *state) {
    g_autoptr(GHashTable) current = g_hash_table_new_full(g_direct_hash, g_direct_equal, g_object_unref, NULL);
    g_autoptr(GPtrArray) gone = g_ptr_array_new();
    GHashTableIter it;
    gpointer monitor;

    for (guint m = 0; m < g_list_model_get_n_items(state->monitors); m++) {
        GdkMonitor *mon = g_list_model_get_item(state->monitors, m);

        if (gdk_monitor_is_valid(mon) && gdk_monitor_get_connector(mon) != NULL)
            g_hash_table_add(current, mon);
        else
            g_object_unref(mon);
    }

    g_hash_table_iter_init(&it, state->monitor_windows);

    while (g_hash_table_iter_next(&it, &monitor, NULL)) {
        if (!g_hash_table_contains(current, monitor))
            g_ptr_array_add(gone, monitor);
    }

    for (guint i = 0; i < gone->len; i++) {
        destroy_monitor(state, g_ptr_array_index(gone, i));
    }

    g_hash_table_iter_init(&it, current);

    while (g_hash_table_iter_next(&it, &monitor, NULL)) {
        if (!g_hash_table_contains(state->monitor_windows, monitor))
            spawn_monitor(state, monitor);
    }

    publish_monitors(state);
}

static void respawn_all(AppState *state) {
    g_autoptr(GPtrArray) monitors = g_hash_table_get_keys_as_ptr_array(state->monitor_windows);

    for (guint i = 0; i < monitors->len; i++) {
        destroy_monitor(state, g_ptr_array_index(monitors, i));
    }

    sync_monitors(state);
}

static gboolean on_monitors_settled(gpointer user_data) {
    AppState *state = user_data;

    state->monitors_sync_id = 0;
    sync_monitors(state);

    return G_SOURCE_REMOVE;
}

static void on_monitors_changed(GListModel *list, guint position, guint removed, guint added, gpointer user_data) {
    AppState *state = user_data;

    g_debug("monitors: %u removed, %u added at %u, now %u", removed, added, position, g_list_model_get_n_items(list));
    g_clear_handle_id(&state->monitors_sync_id, g_source_remove);
    state->monitors_sync_id = g_timeout_add(200, on_monitors_settled, state);
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    AppState *state = user_data;

    if (state->monitors != NULL)
        return;

    state->app = app;
    state_init(state);
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, "window { background: none; }");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    g_debug("state initialized");

    g_signal_connect(state->monitors, "items-changed", G_CALLBACK(on_monitors_changed), state);
    sync_monitors(state);

    if (state->config->hot_reload)
        reload_init(state, respawn_all);
}

static void on_startup(GApplication *app, gpointer user_data) {
    (void)app;

    AppState *state = user_data;

    const gchar *config_dir = g_get_user_config_dir();
    g_debug("user config: %s", config_dir);

    g_autofree gchar *config_file = g_build_filename(config_dir, CONFIG_DIR_NAME, CONFIG_FILE_NAME, NULL);

    g_autoptr(GError) error = NULL;

    state->config = g_new0(Config, 1);

    if (!config_parse(state->config, config_file, &error))
        g_error("config: %s", error->message);

    state->config_path = g_steal_pointer(&config_file);
}

static gint on_command_line(GApplication *app, GApplicationCommandLine *cmdline, gpointer user_data) {
    AppState *state = user_data;
    gint argc;
    g_auto(GStrv) argv = g_application_command_line_get_arguments(cmdline, &argc);

    if (argc < 2) {
        g_application_activate(app);
        return 0;
    }

    if (!g_application_command_line_get_is_remote(cmdline)) {
        g_application_command_line_printerr(cmdline, "wwwidgets is not running\n");
        return 1;
    }

    return store_command(state, cmdline, argc - 1, argv + 1);
}

int main(int argc, char **argv) {
    log_init();
    signal(SIGPIPE, SIG_IGN);

    AppState state = { 0 };

    GtkApplication *app = gtk_application_new(APP_ID, G_APPLICATION_HANDLES_COMMAND_LINE);
    g_signal_connect(app, "startup", G_CALLBACK(on_startup), &state);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &state);
    g_signal_connect(app, "command-line", G_CALLBACK(on_command_line), &state);

    gint status = g_application_run(G_APPLICATION(app), argc, argv);

    if (state.config)
        state_clear(&state);

    g_object_unref(app);


    return status;
}
