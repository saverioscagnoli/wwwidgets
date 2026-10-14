#include <json-glib/json-glib.h>
#include <gtk4-layer-shell.h>

#include "gio/gio.h"
#include "glib.h"
#include "state.h"
#include "store.h"

void store_init(AppState *state) {
    state->store = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}

void store_clear(AppState *state) {
    g_clear_pointer(&state->store, g_hash_table_unref);
}

const gchar *store_get(AppState *state, const gchar *key) {
    return state->store ? g_hash_table_lookup(state->store, key) : NULL;
}

static gboolean json_truthy(const gchar *json) {
    return json != NULL && !g_str_equal(json, "null") && !g_str_equal(json, "false") && !g_str_equal(json, "0") && !g_str_equal(json, "\"\"");
}

void store_apply_visibility(AppState *state, GtkWindow *window) {
    const gchar *key = g_object_get_data(G_OBJECT(window), "visible-when");
    gboolean open = json_truthy(store_get(state, key));
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));

    if (surface == NULL)
        return;

    g_debug("state: %s window for %s", open ? "enabling" : "disabling", key);

    if (open) {
        gdk_surface_set_input_region(surface, NULL);
        gtk_layer_set_keyboard_mode(window, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(window), "keyboard")));
    } else {
        cairo_region_t *empty = cairo_region_create();

        gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
        gdk_surface_set_input_region(surface, empty);
        cairo_region_destroy(empty);
    }
}

static void store_changed(AppState *state, const gchar *key, JsonNode *value) {
    g_autoptr(JsonBuilder) b = json_builder_new();

    json_builder_begin_object(b);
    json_builder_set_member_name(b, "key");
    json_builder_add_string_value(b, key);
    json_builder_set_member_name(b, "value");
    json_builder_add_value(b, json_node_copy(value));
    json_builder_end_object(b);

    g_autoptr(JsonNode) root = json_builder_get_root(b);
    g_autofree gchar *json = json_to_string(root, FALSE);

    for (guint i = 0; i < state->views->len; i++) {
        push_event_json(g_ptr_array_index(state->views, i), "state", json);
    }

    for (guint i = 0; i < state->windows->len; i++) {
        GtkWindow *window = g_ptr_array_index(state->windows, i);

        if (g_strcmp0(g_object_get_data(G_OBJECT(window), "visible-when"), key) == 0)
            store_apply_visibility(state, window);
    }
}

gboolean store_set(AppState *state, const gchar *key, const gchar *json) {
    g_autoptr(JsonNode) node = json ? json_from_string(json, NULL) : NULL;

    if (!state->store || !node)
        return FALSE;

    g_autofree gchar *value = json_to_string(node, FALSE);

    if (g_strcmp0(g_hash_table_lookup(state->store, key), value) == 0)
        return TRUE;

    g_debug("state: %s = %s", key, value);
    g_hash_table_replace(state->store, g_strdup(key), g_steal_pointer(&value));
    store_changed(state, key, node);

    return TRUE;
}

static gchar *arg_to_json(const gchar *arg) {
    g_autoptr(JsonNode) node = json_from_string(arg, NULL);

    if (!node) {
        node = json_node_new(JSON_NODE_VALUE);
        json_node_set_string(node, arg);
    }

    return json_to_string(node, FALSE);
}

gint store_command(AppState *state, GApplicationCommandLine *cmdline, gint argc, gchar **argv) {
    if (!state->store) {
        g_application_command_line_printerr(cmdline, "wwwidgets is still starting\n");
        return 1;
    }

    if (g_str_equal(argv[0], "get") && argc == 2) {
        const gchar *value = store_get(state, argv[1]);

        g_application_command_line_print(cmdline, "%s\n", value ? value : "null");
        return 0;
    }

    if (g_str_equal(argv[0], "set") && (argc == 2 || argc == 3)) {
        g_autofree gchar *json = argc == 3 ? arg_to_json(argv[2]) : g_strdup("true");
        store_set(state, argv[1], json);
        return 0;
    }

    if (g_str_equal(argv[0], "toggle") && argc == 2) {
        store_set(state, argv[1], g_strcmp0(store_get(state, argv[1]), "true") == 0 ? "false" : "true");
        return 0;
    }

    g_application_command_line_printerr(cmdline, "usage: wwwidgets [get <key> | set <key> [value] | toggle <key>]\n");
    return 2;
}
