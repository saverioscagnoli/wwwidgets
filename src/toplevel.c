#include <gdk/wayland/gdkwayland.h>
#include <wlr-foreign-toplevel-management-unstable-v1-client-protocol.h>

#include "glib.h"
#include "state.h"
#include "toplevel.h"

typedef struct {
    AppState *app;
    struct zwlr_foreign_toplevel_handle_v1 *handle;
    guint32 id;
    gchar *title;
    gchar *app_id;
    guint32 state;
    GPtrArray *outputs;
} Toplevel;

static void toplevel_free(Toplevel *t) {
    zwlr_foreign_toplevel_handle_v1_destroy(t->handle);

    g_ptr_array_unref(t->outputs);
    g_free(t->title);
    g_free(t->app_id);
    g_free(t);
}

static void push(AppState *state) {
    g_autoptr(GVariant) list = toplevel_list(state);

    for (guint i = 0; i < state->views->len; i++) {
        push_event(g_ptr_array_index(state->views, i), "toplevels", list);
    }
}

static void tl_title(void *data, struct zwlr_foreign_toplevel_handle_v1 *h, const gchar *title) {
    (void)h;

    Toplevel *t = data;
    g_free(t->title);
    t->title = g_strdup(title);
}

static void tl_app_id(void *data, struct zwlr_foreign_toplevel_handle_v1 *h, const gchar *app_id) {
    (void)h;

    Toplevel *t = data;
    g_free(t->app_id);
    t->app_id = g_strdup(app_id);
}

static void tl_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_array *states) {
    (void)h;

    Toplevel *t = data;
    guint32 *s;

    t->state = 0;
    wl_array_for_each(s, states) t->state |= 1u << *s;
}

static void tl_output_enter(void *data, struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_output *o) {
    (void)h;
    g_ptr_array_add(((Toplevel *)data)->outputs, o);
}

static void tl_output_leave(void *data, struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_output *o) {
    (void)h;
    g_ptr_array_remove(((Toplevel *)data)->outputs, o);
}

static void tl_parent(void *data, struct zwlr_foreign_toplevel_handle_v1 *h, struct zwlr_foreign_toplevel_handle_v1 *parent) {
    (void)data;
    (void)h;
    (void)parent;
}

static void tl_done(void *data, struct zwlr_foreign_toplevel_handle_v1 *h) {
    (void)h;
    push(((Toplevel *)data)->app);
}

static void tl_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *h) {
    (void)h;

    Toplevel *t = data;
    AppState *state = t->app;

    g_ptr_array_remove(state->toplevels, t);
    push(state);
}

static const struct zwlr_foreign_toplevel_handle_v1_listener tl_listener = {
    .title = tl_title,
    .app_id = tl_app_id,
    .output_enter = tl_output_enter,
    .output_leave = tl_output_leave,
    .state = tl_state,
    .done = tl_done,
    .closed = tl_closed,
    .parent = tl_parent,
};

static void manager_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *m, struct zwlr_foreign_toplevel_handle_v1 *h) {
    (void)m;

    AppState *state = data;
    Toplevel *t = g_new0(Toplevel, 1);

    t->app = state;
    t->handle = h;
    t->id = ++state->tl_next_id;
    t->outputs = g_ptr_array_new();

    zwlr_foreign_toplevel_handle_v1_add_listener(h, &tl_listener, t);
    g_ptr_array_add(state->toplevels, t);
}

static void manager_finished(void *data, struct zwlr_foreign_toplevel_manager_v1 *m) {
    AppState *state = data;

    zwlr_foreign_toplevel_manager_v1_destroy(m);
    state->tl_manager = NULL;
}

static const struct zwlr_foreign_toplevel_manager_v1_listener manager_listener = {
    .toplevel = manager_toplevel,
    .finished = manager_finished,
};

static void registry_global(void *data, struct wl_registry *reg, guint32 name, const gchar *iface, guint32 version) {
    AppState *state = data;

    if (g_str_equal(iface, zwlr_foreign_toplevel_manager_v1_interface.name)) {
        state->tl_manager = wl_registry_bind(reg, name, &zwlr_foreign_toplevel_manager_v1_interface, MIN(version, 3));
        zwlr_foreign_toplevel_manager_v1_add_listener(state->tl_manager, &manager_listener, state);
        g_debug("toplevels: bound %s v%u", iface, MIN(version, 3));
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

void toplevels_init(AppState *state) {
    GdkDisplay *display = gdk_display_get_default();

    if (!GDK_IS_WAYLAND_DISPLAY(display)) {
        g_warning("toplevels: not running on wayland");
        return;
    }

    state->toplevels = g_ptr_array_new_with_free_func((GDestroyNotify)toplevel_free);

    struct wl_display *wl = gdk_wayland_display_get_wl_display(display);
    struct wl_registry *reg = wl_display_get_registry(wl);

    wl_registry_add_listener(reg, &registry_listener, state);
}

GVariant *toplevel_list(AppState *state) {
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE("aa{sv}"));

    for (guint i = 0; state->toplevels && i < state->toplevels->len; i++) {
        Toplevel *t = g_ptr_array_index(state->toplevels, i);
        GVariantBuilder e;

        g_variant_builder_init(&e, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&e, "{sv}", "id", g_variant_new_uint32(t->id));
        g_variant_builder_add(&e, "{sv}", "title", g_variant_new_string(t->title ? t->title : ""));
        g_variant_builder_add(&e, "{sv}", "appId", g_variant_new_string(t->app_id ? t->app_id : ""));
        g_variant_builder_add(&e, "{sv}", "activated", g_variant_new_boolean(t->state & (1u << ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED)));
        g_variant_builder_add(&e, "{sv}", "minimized", g_variant_new_boolean(t->state & (1u << ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED)));
        g_variant_builder_add_value(&b, g_variant_builder_end(&e));
    }

    return g_variant_ref_sink(g_variant_builder_end(&b));
}

gboolean toplevel_action(AppState *state, guint32 id, const gchar *action) {
    for (guint i = 0; state->toplevels && i < state->toplevels->len; i++) {
        Toplevel *t = g_ptr_array_index(state->toplevels, i);

        if (t->id != id)
            continue;

        if (g_str_equal(action, "activate")) {
            GdkSeat *seat = gdk_display_get_default_seat(gdk_display_get_default());
            zwlr_foreign_toplevel_handle_v1_activate(t->handle, gdk_wayland_seat_get_wl_seat(seat));
        } else if (g_str_equal(action, "close")) {
            zwlr_foreign_toplevel_handle_v1_close(t->handle);
        } else if (g_str_equal(action, "minimize")) {
             zwlr_foreign_toplevel_handle_v1_set_minimized(t->handle);
        } else if (g_str_equal(action, "unminimize")) {
             zwlr_foreign_toplevel_handle_v1_unset_minimized(t->handle);
        } else {
             return FALSE;
        }

        return TRUE;
    }

    return FALSE;
}
