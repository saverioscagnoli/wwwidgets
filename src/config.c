#include "config.h"
#include "glib-object.h"
#include "glibconfig.h"
#include <glib.h>
#include <json-glib/json-glib.h>

#define BOOL_STR(b) ((b) ? "enabled" : "disabled")

static gchar *resolve_path(const gchar *config_path, const gchar *path, GError **error) {
    g_autofree gchar *expanded = NULL;

    if (g_str_equal(path, "~") || g_str_has_prefix(path, "~"))
        expanded = g_build_filename(g_get_home_dir(), path + 1, NULL);

    g_autofree gchar *base = g_path_get_dirname(config_path);
    g_autofree gchar *resolved = g_canonicalize_filename(expanded ? expanded : path, base);

    if (g_file_test(resolved, G_FILE_TEST_IS_DIR)) {
        gchar *index = g_build_filename(resolved, "index.html", NULL);
        g_free(resolved);
        resolved = index;
    }

    if (!g_file_test(resolved, G_FILE_TEST_IS_REGULAR)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOENT, "\"%s\": %s not found", path, resolved);
        return NULL;
    }

    return g_steal_pointer(&resolved);
}

static gboolean parse_monitors(JsonObject *obj, const gchar *key, WidgetConfig *w, GError **error) {
   w->all_monitors = TRUE;
   w->monitors = NULL;

   if (!json_object_has_member(obj, "monitors"))
       return TRUE;

   JsonNode *node = json_object_get_member(obj, "monitors");

   if (JSON_NODE_HOLDS_VALUE(node) && json_node_get_value_type(node) == G_TYPE_STRING) {
       const gchar *name = json_node_get_string(node);

       if (g_str_equal(name, "all"))
           return TRUE;

       w->all_monitors = FALSE;
       w->monitors = g_strdupv((gchar *[]){ (gchar *)name, NULL });

       return TRUE;
   }

   if (JSON_NODE_HOLDS_ARRAY(node)) {
       JsonArray *array = json_node_get_array(node);
       g_autoptr(GStrvBuilder) builder = g_strv_builder_new();

       for (guint i = 0; i < json_array_get_length(array); i++) {
           JsonNode *item = json_array_get_element(array, i);

           if (!JSON_NODE_HOLDS_VALUE(item) || json_node_get_value_type(item) != G_TYPE_STRING) {
               g_set_error(error, JSON_PARSER_ERROR, JSON_PARSER_ERROR_INVALID_DATA, "\"%s\"[%u]: expected a monitor name", key, i);
               return FALSE;
           }

           g_strv_builder_add(builder, json_node_get_string(item));
       }

       w->all_monitors = FALSE;
       w->monitors = g_strv_builder_end(builder);
       return TRUE;
   }

   g_set_error(error, JSON_PARSER_ERROR, JSON_PARSER_ERROR_INVALID_DATA, "\"%s\": expected \"all\", a monitor name, or a list of names", key);
   return FALSE;
}

static gboolean parse_size(JsonObject *obj, const gchar *key, Size fallback, Size *out, GError **error) {
    *out = fallback;

    if (!json_object_has_member(obj, key))
        return TRUE;

    JsonNode *node = json_object_get_member(obj, key);
    GType type = json_node_get_value_type(node);

    if (type == G_TYPE_INT64 || type == G_TYPE_DOUBLE) {
        out->value = json_node_get_double(node);
        out->is_percent = FALSE;
    } else if (type == G_TYPE_STRING) {
        const gchar *str = json_node_get_string(node);
        char *end = NULL;
        gdouble v = g_ascii_strtod(str, &end);

        if (end == str || !g_str_equal(end, "%")) {
            g_set_error(error, JSON_PARSER_ERROR, JSON_PARSER_ERROR_INVALID_DATA, "\"%s\": expected a number or \"N%%\", got \"%s\"", key, str);
            return FALSE;
        }

        out->value = v;
        out->is_percent = TRUE;
    } else {
        g_set_error(error, JSON_PARSER_ERROR, JSON_PARSER_ERROR_INVALID_DATA, "\"%s\": expected a number or \"N%%\"", key);
        return FALSE;
    }

    if (out->value < 0) {
        g_set_error(error, JSON_PARSER_ERROR, JSON_PARSER_ERROR_INVALID_DATA, "\"%s\": must not be negative", key);
        return FALSE;
    }

    return TRUE;
}

void config_parse(Config *config, const gchar *path) {
    g_autoptr(GError) error = NULL;
    g_autoptr(JsonParser) parser = json_parser_new();

    if (!json_parser_load_from_file(parser, path, &error)) {
        g_error("failed to parse: %s\n", error->message);
    }

    JsonObject *root = json_node_get_object(json_parser_get_root(parser));

    gboolean devtools = json_object_get_boolean_member_with_default(root, "devtools", FALSE);
    gboolean workspaces = json_object_get_boolean_member_with_default(root, "workspaces", FALSE);
    gboolean notifications = json_object_get_boolean_member_with_default(root, "notifications", FALSE);
    gboolean tray = json_object_get_boolean_member_with_default(root, "tray", FALSE);
    gboolean bluetooth = json_object_get_boolean_member_with_default(root, "bluetooth", FALSE);
    gboolean network = json_object_get_boolean_member_with_default(root, "network", FALSE);
    gboolean battery = json_object_get_boolean_member_with_default(root, "battery", FALSE);

    config->devtools = devtools;
    config->workspaces = workspaces;
    config->notifications = notifications;
    config->tray = tray;
    config->bluetooth = bluetooth;
    config->network = network;
    config->battery = battery;

    g_debug("devtools: %s", BOOL_STR(devtools));
    g_debug("workspaces: %s", BOOL_STR(workspaces));
    g_debug("notifications: %s", BOOL_STR(notifications));
    g_debug("tray: %s", BOOL_STR(tray));
    g_debug("bluetooth: %s", BOOL_STR(bluetooth));
    g_debug("network: %s", BOOL_STR(network));
    g_debug("battery: %s", BOOL_STR(battery));

    config->widgets = g_ptr_array_new_with_free_func((GDestroyNotify)widget_config_free);

    if (json_object_has_member(root, "widgets")) {
        JsonArray *widgets = json_object_get_array_member(root, "widgets");

        for (guint i = 0; i < json_array_get_length(widgets); i++) {
            JsonObject *obj = json_array_get_object_element(widgets, i);

            if (obj == NULL || !json_object_has_member(obj, "path")) {
                g_warning("widgets[%u]: missing \"path\", skipping", i);
                continue;
            }

            WidgetConfig *w = g_new0(WidgetConfig, 1);
            g_autoptr(GError) widget_error = NULL;

            w->path = resolve_path(path, json_object_get_string_member(obj, "path"), &widget_error);
            if (w->path == NULL) {
                g_warning("widgets[%u]: %s, skipping", i, widget_error->message);
                widget_config_free(w);
                continue;
            }

            w->namespace = g_strdup(json_object_get_string_member_with_default(obj, "namespace", "wwwidgets"));

            Size fallback = { 100, TRUE };

            if (!parse_size(obj, "width", fallback, &w->width, &widget_error) ||
                !parse_size(obj, "height", fallback, &w->height, &widget_error) ||
                !parse_monitors(obj, "monitors", w, &widget_error)
            ) {
                g_warning("widgets[%u]: %s, skipping", i, widget_error->message);
                widget_config_free(w);
                continue;
            }

            g_ptr_array_add(config->widgets, w);
        }
    }

    if (config->widgets->len == 0) {
        g_error("couldn't find any widgets");
    }

    g_info("found and parsed %u widgets", config->widgets->len);
}
