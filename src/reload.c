#include <glib.h>
#include <gio/gio.h>
#include <webkit/webkit.h>

#include "reload.h"
#include "state.h"

#define WATCH_DEPTH 4

struct Reload {
    AppState *state;
    void (*respawn)(AppState *state);
    GFileMonitor *config_monitor;
    GHashTable *watched;
    GHashTable *dirty;
    gboolean config_dirty;
    guint settle_id;
};

static void unwatch(gpointer monitor) {
    g_file_monitor_cancel(monitor);
    g_object_unref(monitor);
}

static gboolean is_noise(GFile *file) {
    g_autofree gchar *name = file ? g_file_get_basename(file) : NULL;

    return name == NULL || name[0] == '.' || g_str_has_suffix(name, "~") || g_str_equal(name, "4913");
}

static gboolean event_matters(GFileMonitorEvent event) {
    switch (event) {
        case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
        case G_FILE_MONITOR_EVENT_CREATED:
        case G_FILE_MONITOR_EVENT_DELETED:
        case G_FILE_MONITOR_EVENT_MOVED_IN:
        case G_FILE_MONITOR_EVENT_MOVED_OUT:
        case G_FILE_MONITOR_EVENT_RENAMED:
            return TRUE;
        default:
            return FALSE;
    }
}

static void reload_config(Reload *r);

static gboolean on_settled(gpointer user_data) {
    Reload *r = user_data;
    AppState *state = r->state;

    r->settle_id = 0;

    if (r->config_dirty) {
        r->config_dirty = FALSE;
        g_hash_table_remove_all(r->dirty);
        reload_config(r);

        return G_SOURCE_REMOVE;
    }

    for (guint i = 0; i < state->views->len; i++) {
        WebKitWebView *view = g_ptr_array_index(state->views, i);
        gpointer n = g_object_get_data(G_OBJECT(view), "widget");

        if (!g_hash_table_contains(r->dirty, n))
            continue;

        g_info("reload: widgets[%u] changed, reloading view", GPOINTER_TO_UINT(n));
        webkit_web_view_reload_bypass_cache(view);
    }

    g_hash_table_remove_all(r->dirty);

    return G_SOURCE_REMOVE;
}

static void schedule(Reload *r) {
    g_clear_handle_id(&r->settle_id, g_source_remove);
    r->settle_id = g_timeout_add(200, on_settled, r);
}

static void on_widget_changed(GFileMonitor *monitor, GFile *file, GFile *other, GFileMonitorEvent event, gpointer user_data) {
    (void)monitor;
    (void)other;

    Reload *r = user_data;
    Config *config = r->state->config;

    if (!event_matters(event) || is_noise(file))
        return;

    for (guint i = 0; i < config->widgets->len; i++) {
        WidgetConfig *w = g_ptr_array_index(config->widgets, i);

        if (w->path == NULL || w->is_url)
            continue;

        g_autofree gchar *dir = g_path_get_dirname(w->path);
        g_autoptr(GFile) root = g_file_new_for_path(dir);

        if (g_file_has_prefix(file, root))
            g_hash_table_add(r->dirty, GUINT_TO_POINTER(i));
    }

    if (g_hash_table_size(r->dirty) > 0)
        schedule(r);
}

static void on_config_changed(GFileMonitor *monitor, GFile *file, GFile *other, GFileMonitorEvent event, gpointer user_data) {
    (void)monitor;

    Reload *r = user_data;
    g_autofree gchar *name = g_path_get_basename(r->state->config_path);
    g_autofree gchar *a = file ? g_file_get_basename(file) : NULL;
    g_autofree gchar *b = other ? g_file_get_basename(other) : NULL;

    if (!event_matters(event))
        return;

    if (!(a && g_str_equal(a, name)) && !(b && g_str_equal(b, name)))
        return;

    g_debug("reload: config event %d", event);
    r->config_dirty = TRUE;
    schedule(r);
}

static void watch_tree(Reload *r, GFile *dir, guint depth) {
    g_autoptr(GError) err = NULL;
    g_autofree gchar *path = g_file_get_path(dir);

    if (path == NULL || g_hash_table_contains(r->watched, path))
        return;

    GFileMonitor *monitor = g_file_monitor_directory(dir, G_FILE_MONITOR_WATCH_MOVES, NULL, &err);

    if (monitor == NULL) {
        g_warning("reload: can't watch %s: %s", path, err->message);
        return;
    }

    g_signal_connect(monitor, "changed", G_CALLBACK(on_widget_changed), r);
    g_hash_table_insert(r->watched, g_strdup(path), monitor);
    g_debug("reload: watching %s", path);

    if (depth == 0)
        return;

    g_autoptr(GFileEnumerator) children = g_file_enumerate_children(dir,
        G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
    GFileInfo *info;
    GFile *child;

    while (children && g_file_enumerator_iterate(children, &info, &child, NULL, NULL) && info) {
        const gchar *name = g_file_info_get_name(info);

        if (g_file_info_get_file_type(info) != G_FILE_TYPE_DIRECTORY || name[0] == '.' || g_str_equal(name, "node_modules"))
            continue;

        watch_tree(r, child, depth - 1);
    }
}

static void watch_widgets(Reload *r) {
    Config *config = r->state->config;

    g_hash_table_remove_all(r->watched);

    for (guint i = 0; i < config->widgets->len; i++) {
        WidgetConfig *w = g_ptr_array_index(config->widgets, i);

        if (w->path == NULL || w->is_url)
            continue;

        g_autofree gchar *dir = g_path_get_dirname(w->path);
        g_autoptr(GFile) root = g_file_new_for_path(dir);

        watch_tree(r, root, WATCH_DEPTH);
    }
}

static void warn_restart(const Config *old, const Config *new) {
    if (old->workspaces != new->workspaces || old->notifications != new->notifications ||
        old->tray != new->tray || old->audio != new->audio || old->bluetooth != new->bluetooth ||
        old->network != new->network || old->battery != new->battery || old->exec != new->exec ||
        old->toplevels != new->toplevels)
        g_warning("reload: enabling or disabling services needs a restart");
}

static void reload_config(Reload *r) {
    AppState *state = r->state;
    g_autoptr(GError) err = NULL;
    Config *config = g_new0(Config, 1);

    if (!config_parse(config, state->config_path, &err)) {
        g_warning("reload: %s, keeping the old config", err->message);
        config_free(config);
        return;
    }

    warn_restart(state->config, config);

    g_clear_pointer(&state->config, config_free);
    state->config = config;

    g_info("reload: config changed, respawning widgets");
    watch_widgets(r);
    r->respawn(state);
}

void reload_init(AppState *state, void (*respawn)(AppState *state)) {
    g_autoptr(GError) err = NULL;
    Reload *r = g_new0(Reload, 1);

    r->state = state;
    r->respawn = respawn;
    r->watched = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, unwatch);
    r->dirty = g_hash_table_new(g_direct_hash, g_direct_equal);
    state->reload = r;

    g_autofree gchar *dir = g_path_get_dirname(state->config_path);
    g_autoptr(GFile) config_dir = g_file_new_for_path(dir);

    r->config_monitor = g_file_monitor_directory(config_dir, G_FILE_MONITOR_WATCH_MOVES, NULL, &err);

    if (r->config_monitor)
        g_signal_connect(r->config_monitor, "changed", G_CALLBACK(on_config_changed), r);
    else
        g_warning("reload: can't watch %s: %s", dir, err->message);

    watch_widgets(r);
    g_debug("hot reload enabled");
}

void reload_clear(AppState *state) {
    Reload *r = g_steal_pointer(&state->reload);

    if (r == NULL)
        return;

    g_clear_handle_id(&r->settle_id, g_source_remove);
    g_clear_pointer(&r->config_monitor, unwatch);
    g_clear_pointer(&r->watched, g_hash_table_unref);
    g_clear_pointer(&r->dirty, g_hash_table_unref);
    g_free(r);
}
