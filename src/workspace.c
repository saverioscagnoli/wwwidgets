#include <gdk/wayland/gdkwayland.h>
#include <glib.h>
#include <wayland-client-protocol.h>
#include <wayland-util.h>
#include <ext-workspace-v1-client-protocol.h>

#include "state.h"
#include "workspace.h"

typedef struct {
   AppState *app;
   struct ext_workspace_handle_v1 *handle;
   gchar *id;
   gchar *name;
   guint32 state;
   guint32 caps;
} Workspace;

static void workspace_free(Workspace *ws) {
    ext_workspace_handle_v1_destroy(ws->handle);
    g_free(ws->id);
    g_free(ws->name);
    g_free(ws);
}

static const gchar *workspace_key(Workspace *ws) {
    return ws->id ? ws->id : ws->name ? ws->name : "";
}

static GVariant *workspace_to_variant(Workspace *ws) {
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", "id", g_variant_new_string(workspace_key(ws)));
    g_variant_builder_add(&b, "{sv}", "name", g_variant_new_string(ws->name ? ws->name : ""));
    g_variant_builder_add(&b, "{sv}", "active", g_variant_new_boolean(ws->state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE));
    g_variant_builder_add(&b, "{sv}", "urgent", g_variant_new_boolean(ws->state & EXT_WORKSPACE_HANDLE_V1_STATE_URGENT));
    g_variant_builder_add(&b, "{sv}", "hidden", g_variant_new_boolean(ws->state & EXT_WORKSPACE_HANDLE_V1_STATE_HIDDEN));

    return g_variant_builder_end(&b);
}

static void ws_id(void *data, struct ext_workspace_handle_v1 *h, const gchar *id) {
    (void)h;

    Workspace *ws = data;
    g_free(ws->id);
    ws->id = g_strdup(id);
}

static void ws_name(void *data, struct ext_workspace_handle_v1 *h, const gchar *name) {
    (void)h;

    Workspace *ws = data;
    g_free(ws->name);
    ws->name = g_strdup(name);
}

static void ws_coordinates(void *data, struct ext_workspace_handle_v1 *h, struct wl_array *coords) {
    (void)data;
    (void)h;
    (void)coords;
}

static void ws_state(void *data, struct ext_workspace_handle_v1 *h, guint32 state) {
    (void)h;

    Workspace *ws = data;
    ws->state = state;
}

static void ws_capabilities(void *data, struct ext_workspace_handle_v1 *h, guint32 caps) {
    (void)h;

    Workspace *ws = data;
    ws->caps = caps;
}

static void ws_removed(void *data, struct ext_workspace_handle_v1 *h) {
    (void)h;

    Workspace *ws = data;
    g_ptr_array_remove(ws->app->workspaces, ws);
}

static const struct ext_workspace_handle_v1_listener ws_listener = {
    .id = ws_id,
    .name = ws_name,
    .coordinates = ws_coordinates,
    .state = ws_state,
    .capabilities = ws_capabilities,
    .removed = ws_removed,
};

static void group_capabilities(void *data, struct ext_workspace_group_handle_v1 *g, guint32 caps) {
    (void)data;
    (void)g;
    (void)caps;
}

static void group_output(void *data, struct ext_workspace_group_handle_v1 *g, struct wl_output *output) {
    (void)data;
    (void)g;
    (void)output;
}

static void group_workspace(void *data, struct ext_workspace_group_handle_v1 *g, struct ext_workspace_handle_v1 *h) {
    (void)data;
    (void)g;
    (void)h;
}

static void group_removed(void *data, struct ext_workspace_group_handle_v1 *g) {
    (void)data;
    ext_workspace_group_handle_v1_destroy(g);
}

static const struct ext_workspace_group_handle_v1_listener group_listener = {
    .capabilities = group_capabilities,
    .output_enter = group_output,
    .output_leave = group_output,
    .workspace_enter = group_workspace,
    .workspace_leave = group_workspace,
    .removed = group_removed,
};

static void manager_group(void *data, struct ext_workspace_manager_v1 *m, struct ext_workspace_group_handle_v1 *g) {
    (void)m;
    ext_workspace_group_handle_v1_add_listener(g, &group_listener, data);
}

static void manager_workspace(void *data, struct ext_workspace_manager_v1 *m, struct ext_workspace_handle_v1 *h) {
    (void)m;

    AppState *state = data;
    Workspace *ws = g_new0(Workspace, 1);
    ws->app = state;
    ws->handle = h;

    ext_workspace_handle_v1_add_listener(h, &ws_listener, ws);
    g_ptr_array_add(state->workspaces, ws);
}

static void manager_done(void *data, struct ext_workspace_manager_v1 *m) {
    (void)m;

    AppState *state = data;

    g_autoptr(GVariant) list = workspace_list(state);
    g_autofree gchar *s = g_variant_print(list, FALSE);
    g_debug("workspace: %s", s);

    for (guint i = 0; i < state->views->len; i++) {
        push_event(g_ptr_array_index(state->views, i), "workspace", list);
    }
}

static void manager_finished(void *data, struct ext_workspace_manager_v1 *m) {
    AppState *state = data;
    ext_workspace_manager_v1_destroy(m);
    state->ws_manager = NULL;
}

static const struct ext_workspace_manager_v1_listener manager_listener = {
    .workspace_group = manager_group,
    .workspace = manager_workspace,
    .done = manager_done,
    .finished = manager_finished,
};

static void registry_global(void *data, struct wl_registry *reg, guint32 name, const gchar *iface, guint32 version) {
    (void)version;

    AppState *state = data;

    if (g_str_equal(iface, ext_workspace_manager_v1_interface.name)) {
        state->ws_manager = wl_registry_bind(reg, name, &ext_workspace_manager_v1_interface, 1);
        ext_workspace_manager_v1_add_listener(state->ws_manager, &manager_listener, state);
        g_debug("workspace: bound %s", iface);
    }
}

static void registry_global_remove(void *data, struct wl_registry *reg, guint32 name) {
    (void)data;
    (void)reg;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

void workspace_init(AppState *state) {
    GdkDisplay *display = gdk_display_get_default();

    if (!GDK_IS_WAYLAND_DISPLAY(display)) {
        g_warning("workspace: not running on wayland");
        return;
    }

    state->workspaces = g_ptr_array_new_with_free_func((GDestroyNotify)workspace_free);

    struct wl_display *wl = gdk_wayland_display_get_wl_display(display);
    struct wl_registry *reg = wl_display_get_registry(wl);

    wl_registry_add_listener(reg, &registry_listener, state);
}

GVariant *workspace_list(AppState *state) {
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE("aa{sv}"));

    for (guint i = 0; state->workspaces && i < state->workspaces->len; i++) {
        g_variant_builder_add_value(&b, workspace_to_variant(g_ptr_array_index(state->workspaces, i)));
    }

    return g_variant_ref_sink(g_variant_builder_end(&b));
}

GVariant *workspace_active(AppState *state) {
    for (guint i = 0; state->workspaces && i < state->workspaces->len; i++) {
        Workspace *ws = g_ptr_array_index(state->workspaces, i);

        if (ws->state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE)
            return g_variant_ref_sink(workspace_to_variant(ws));
    }

    return NULL;
}

gboolean workspace_activate(AppState *state, const gchar *id) {
    if (!state->ws_manager)
        return FALSE;

    for (guint i = 0; i < state->workspaces->len; i++) {
        Workspace *ws = g_ptr_array_index(state->workspaces, i);

        if (!g_str_equal(workspace_key(ws), id))
            continue;

        if (!(ws->caps & EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE))
            return FALSE;

        ext_workspace_handle_v1_activate(ws->handle);
        ext_workspace_manager_v1_commit(state->ws_manager);

        return TRUE;
    }

    return FALSE;
}
