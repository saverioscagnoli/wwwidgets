#include "config.h"
#include "glib-object.h"
#include "glibconfig.h"
#include "gtk4-layer-shell.h"
#include <glib.h>
#include <json-glib/json-glib.h>

#define BOOL_STR(b) ((b) ? "enabled" : "disabled")

static gboolean node_is_string(JsonNode *node) {
    return JSON_NODE_HOLDS_VALUE(node) && json_node_get_value_type(node) == G_TYPE_STRING;
}

static gboolean node_to_px(JsonNode *node, int *out) {
    if (!JSON_NODE_HOLDS_VALUE(node))
        return FALSE;

    GType type = json_node_get_value_type(node);
    if (type != G_TYPE_INT64 && type != G_TYPE_DOUBLE)
        return FALSE;

    *out = (int)json_node_get_double(node);

    return TRUE;
}

static gchar *resolve_path(const gchar *config_path, const gchar *path, GError **error) {
    g_autofree gchar *expanded = NULL;

    if (g_str_equal(path, "~") || g_str_has_prefix(path, "~"))
        expanded = g_build_filename(g_get_home_dir(), path + 1, NULL);

    g_autofree gchar *dir = g_path_get_dirname(config_path);
    g_autofree gchar *base = g_canonicalize_filename(dir, NULL);
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

static gboolean parse_layer(JsonObject *obj, const gchar *key, GtkLayerShellLayer *out, GError **error) {
    *out = GTK_LAYER_SHELL_LAYER_TOP;

    if (!json_object_has_member(obj, key))
        return TRUE;

    JsonNode *node = json_object_get_member(obj, key);
    const gchar *s = node_is_string(node) ? json_node_get_string(node) : "";

    if (g_str_equal(s, "background"))   *out = GTK_LAYER_SHELL_LAYER_BACKGROUND;
    else if (g_str_equal(s, "bottom"))  *out = GTK_LAYER_SHELL_LAYER_BOTTOM;
    else if (g_str_equal(s, "top"))     *out = GTK_LAYER_SHELL_LAYER_TOP;
    else if (g_str_equal(s, "overlay")) *out = GTK_LAYER_SHELL_LAYER_OVERLAY;
    else {
        g_set_error(error, JSON_PARSER_ERROR, JSON_PARSER_ERROR_INVALID_DATA, "\"%s\": expected \"background\", \"bottom\", \"top\" or \"overlay\"", key);
        return FALSE;
    }

    return TRUE;
}

static gboolean parse_anchor_string(const gchar *key, const gchar *s, gboolean anchor[], GError **error) {
    if (g_str_equal(s, "center"))
        return TRUE;

    if (g_str_equal(s, "fill")) {
        for (int e = 0; e < GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER; e++) {
            anchor[e] = TRUE;
        }

        return TRUE;
    }

    g_auto(GStrv) parts = g_strsplit(s, "-", -1);

    for (gchar **p = parts; *p != NULL; p++) {
        if (g_str_equal(*p, "top"))         anchor[GTK_LAYER_SHELL_EDGE_TOP] = TRUE;
        else if (g_str_equal(*p, "bottom")) anchor[GTK_LAYER_SHELL_EDGE_BOTTOM] = TRUE;
        else if (g_str_equal(*p, "left"))   anchor[GTK_LAYER_SHELL_EDGE_LEFT] = TRUE;
        else if (g_str_equal(*p, "right"))  anchor[GTK_LAYER_SHELL_EDGE_RIGHT] = TRUE;
        else {
            g_set_error(error, JSON_PARSER_ERROR, JSON_PARSER_ERROR_INVALID_DATA, "\"%s\": unknown edge \"%s\" (use top, bottom, left, right, center, fill)", key, *p);
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean parse_anchor(JsonObject *obj, const gchar *key, gboolean anchor[], GError **error) {
    for (int e = 0; e < GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER; e++) {
        anchor[e] = FALSE;
    }

    if (!json_object_has_member(obj, key))
        return TRUE;

    JsonNode *node = json_object_get_member(obj, key);

    if (node_is_string(node))
        return parse_anchor_string(key, json_node_get_string(node), anchor, error);

    if (JSON_NODE_HOLDS_ARRAY(node)) {
        JsonArray *array = json_node_get_array(node);

        for (guint i = 0; i < json_array_get_length(array); i++) {
            JsonNode *item = json_array_get_element(array, i);

            if (!node_is_string(item)) {
                g_set_error(error, JSON_PARSER_ERROR, JSON_PARSER_ERROR_INVALID_DATA, "\"%s\"[%u]: expected an edge name", key, i);
                return FALSE;
            }

            if (!parse_anchor_string(key, json_node_get_string(item), anchor, error))
                return FALSE;
        }

        return TRUE;
    }

    g_set_error(error, JSON_PARSER_ERROR, JSON_PARSER_ERROR_INVALID_DATA, "\"%s\": expected an edge name or a list of edges", key);
    return FALSE;
}

static gboolean parse_margins(JsonObject *obj, const gchar *key, gint margins[], GError **error) {
    for (gint e = 0; e < GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER; e++) {
        margins[e] = 0;
    }

    if (!json_object_has_member(obj, key))
        return TRUE;

    JsonNode *node = json_object_get_member(obj, key);
    gint px;

    if (node_to_px(node, &px)) {
        for (gint e = 0; e < GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER; e++) {
            margins[e] = px;
        }

        return TRUE;
    }

    if (!JSON_NODE_HOLDS_OBJECT(node)) {
        g_set_error(error, JSON_PARSER_ERROR, JSON_PARSER_ERROR_INVALID_DATA, "\"%s\": expected a number or {top, bottom, left, right}", key);
        return FALSE;
    }

    static const struct { const char *name; GtkLayerShellEdge edge; } edges[] = {
         { "top", GTK_LAYER_SHELL_EDGE_TOP },
         { "bottom", GTK_LAYER_SHELL_EDGE_BOTTOM },
         { "left", GTK_LAYER_SHELL_EDGE_LEFT },
         { "right", GTK_LAYER_SHELL_EDGE_RIGHT },
    };

    JsonObjectIter iter;
    const gchar *name;
    JsonNode *value;

    json_object_iter_init(&iter, json_node_get_object(node));

    while (json_object_iter_next(&iter, &name, &value)) {
        gsize i = 0;

        while (i < G_N_ELEMENTS(edges) && !g_str_equal(edges[i].name, name)) {
            i++;
        }

        if (i == G_N_ELEMENTS(edges)) {
            g_set_error(error, JSON_PARSER_ERROR, JSON_PARSER_ERROR_INVALID_DATA, "\"%s\": unknown edge \"%s\" (use top, bottom, left, right)", key, name);
            return FALSE;
        }

        if (!node_to_px(value, &margins[edges[i].edge])) {
            g_set_error(error, JSON_PARSER_ERROR, JSON_PARSER_ERROR_INVALID_DATA, "\"%s.%s\": expected a number", key, name);
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean parse_exclusive(JsonObject *obj, const gchar *key, ExclusiveMode *mode, int *px, GError **error) {
    *mode = EXCLUSIVE_NONE;
    *px = 0;

    if (!json_object_has_member(obj, key))
        return TRUE;

    JsonNode *node = json_object_get_member(obj, key);
    GType type = JSON_NODE_HOLDS_VALUE(node) ? json_node_get_value_type(node) : G_TYPE_INVALID;

    if (type == G_TYPE_BOOLEAN) {
        *mode = json_node_get_boolean(node) ? EXCLUSIVE_AUTO : EXCLUSIVE_NONE;
        return TRUE;
    }

    if (type == G_TYPE_INT64 && json_node_get_int(node) >= 0) {
        *mode = EXCLUSIVE_FIXED;
        *px = (int)json_node_get_int(node);
        return TRUE;
    }

    g_set_error(error, JSON_PARSER_ERROR, JSON_PARSER_ERROR_INVALID_DATA, "\"%s\": expected true, false or a number of pixels", key);
    return FALSE;
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
                !parse_monitors(obj, "monitors", w, &widget_error) ||
                !parse_layer(obj, "layer", &w->layer, &widget_error) ||
                !parse_anchor(obj, "anchor", w->anchor, &widget_error) ||
                !parse_margins(obj, "margin", w->margin, &widget_error) ||
                !parse_exclusive(obj, "exclusive", &w->exclusive, &w->exclusive_px, &widget_error)
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

    g_info("found and parsed %u widget(s)", config->widgets->len);
}
