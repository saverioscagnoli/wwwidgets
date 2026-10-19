#include <gdk/wayland/gdkwayland.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#include <ext-data-control-v1-client-protocol.h>
#include <fcntl.h>
#include <glib-unix.h>
#include <unistd.h>

#include "clipboard.h"
#include "gio/gio.h"
#include "glib.h"
#include "glibconfig.h"

#define HISTORY_MAX 50
#define PREVIEW_MAX 400

typedef struct {
    guint32 id;
    gboolean image;
    GBytes *data;
    gint64 time;
} Entry;

struct Clipboard {
    AppState *app;
    struct ext_data_control_manager_v1 *manager;
    struct ext_data_control_device_v1 *device;
    GPtrArray *history;
    guint32 next_id;
};

static void entry_free(Entry *e) {
    g_bytes_unref(e->data);
    g_free(e);
}

static Entry *entry_find(Clipboard *c, guint32 id) {
    for (guint i = 0; i < c->history->len; i++) {
        Entry *e = g_ptr_array_index(c->history, i);

        if (e->id == id)
            return e;
    }

    return NULL;
}

static void push(AppState *state) {
    g_autoptr(GVariant) list = clipboard_history(state);

    for (guint i = 0; i < state->views->len; i++) {
        push_event(g_ptr_array_index(state->views, i), "clipboard", list);
    }
}

static void history_add(Clipboard *c, gboolean image, GBytes *data) {
    for (guint i = 0; i < c->history->len; i++) {
        Entry *e = g_ptr_array_index(c->history, i);

        if (e->image == image && g_bytes_equal(e->data, data)) {
            g_ptr_array_steal_index(c->history, i);
            e->time = g_get_real_time() / 1000;
            g_ptr_array_insert(c->history, 0, e);
            g_bytes_unref(data);
            push(c->app);
            return;
        }
    }

    Entry *e = g_new0(Entry, 1);

    e->id = ++c->next_id;
    e->image = image;
    e->data = data;
    e->time = g_get_real_time() / 1000;
    g_ptr_array_insert(c->history, 0, e);

    if (c->history->len > HISTORY_MAX)
        g_ptr_array_set_size(c->history, HISTORY_MAX);

    push(c->app);
}

static void offer_mime(void *data, struct ext_data_control_offer_v1 *o, const gchar *mime) {
    (void)o;
    g_ptr_array_add(data, g_strdup(mime));
}

static const struct ext_data_control_offer_v1_listener offer_listener = {
    .offer = offer_mime,
};

static void offer_destroy(struct ext_data_control_offer_v1 *o) {
    g_ptr_array_unref(ext_data_control_offer_v1_get_user_data(o));
    ext_data_control_offer_v1_destroy(o);
}

static void device_data_offer(void *data, struct ext_data_control_device_v1 *d, struct ext_data_control_offer_v1 *o) {
    (void)data;
    (void)d;
    ext_data_control_offer_v1_add_listener(o, &offer_listener, g_ptr_array_new_with_free_func(g_free));
}

static const gchar *pick_mime(GPtrArray *mimes) {
    static const gchar *wanted[] = { "image/png", "text/plain;charset=utf-8", "UTF8_STRING", "text/plain" };

    for (guint i = 0; i < mimes->len; i++) {
        if (g_str_equal(g_ptr_array_index(mimes, i), "x-kde-passwordManagerHint"))
            return NULL;
    }

    for (guint w = 0; w < G_N_ELEMENTS(wanted); w++) {
        for (guint i = 0; i < mimes->len; i++) {
            if (g_str_equal(g_ptr_array_index(mimes, i), wanted[w]))
                return wanted[w];
        }
    }

    return NULL;
}

static gboolean is_blank(const gchar *buf, gsize len) {
    for (gsize i = 0; i < len; i++) {
        if (!g_ascii_isspace(buf[i]))
            return FALSE;
    }

    return TRUE;
}

static void on_read(GObject *src, GAsyncResult *res, gpointer data) {
    Clipboard *c = data;
    gboolean image = GPOINTER_TO_INT(g_object_get_data(src, "image"));
    g_autoptr(GError) err = NULL;

    if (g_output_stream_splice_finish(G_OUTPUT_STREAM(src), res, &err) < 0) {
        g_warning("clipboard: read failed: %s", err->message);
        g_object_unref(src);
        return;
    }

    GBytes *bytes = g_memory_output_stream_steal_as_bytes(G_MEMORY_OUTPUT_STREAM(src));
    gsize len;
    const gchar *buf = g_bytes_get_data(bytes, &len);

    g_object_unref(src);

    if (len == 0 || (!image && (!g_utf8_validate(buf, len, NULL) || is_blank(buf, len)))) {
        g_bytes_unref(bytes);
        return;
    }

    history_add(c, image, bytes);
}

static void device_selection(void *data, struct ext_data_control_device_v1 *d, struct ext_data_control_offer_v1 *o) {
    (void)d;

    Clipboard *c = data;
    int fds[2];

    if (o == NULL)
        return;

    const gchar *mime = pick_mime(ext_data_control_offer_v1_get_user_data(o));

    if (mime == NULL || !g_unix_open_pipe(fds, O_CLOEXEC, NULL)) {
        offer_destroy(o);
        return;
    }

    ext_data_control_offer_v1_receive(o, mime, fds[1]);
    close(fds[1]);
    wl_display_flush(gdk_wayland_display_get_wl_display(gdk_display_get_default()));

    GInputStream *in = g_unix_input_stream_new(fds[0], TRUE);
    GOutputStream *out = g_memory_output_stream_new_resizable();

    g_object_set_data(G_OBJECT(out), "image", GINT_TO_POINTER(g_str_equal(mime, "image/png")));
    g_output_stream_splice_async(out, in, G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE | G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET, G_PRIORITY_DEFAULT, NULL, on_read, c);
    g_object_unref(in);
    offer_destroy(o);
}

static void device_primary(void *data, struct ext_data_control_device_v1 *d, struct ext_data_control_offer_v1 *o) {
    (void)data;
    (void)d;

    if (o)
        offer_destroy(o);
}

static void device_finished(void *data, struct ext_data_control_device_v1 *d) {
    ext_data_control_device_v1_destroy(d);
    ((Clipboard *)data)->device = NULL;
}

static const struct ext_data_control_device_v1_listener device_listener = {
    .data_offer = device_data_offer,
    .selection = device_selection,
    .finished = device_finished,
    .primary_selection = device_primary,
};

static void on_written(GObject *src, GAsyncResult *res, gpointer data) {
    g_output_stream_write_all_finish(G_OUTPUT_STREAM(src), res, NULL, NULL);
    g_output_stream_close(G_OUTPUT_STREAM(src), NULL, NULL);
    g_object_unref(src);
    g_bytes_unref(data);
}

static void source_send(void *data, struct ext_data_control_source_v1 *s, const gchar *mime, int32_t fd) {
    (void)s;
    (void)mime;

    GBytes *bytes = data;
    GOutputStream *out = g_unix_output_stream_new(fd, TRUE);
    gsize len;
    const void *buf = g_bytes_get_data(bytes, &len);

    g_output_stream_write_all_async(out, buf, len, G_PRIORITY_DEFAULT, NULL, on_written, g_bytes_ref(bytes));
}

static void source_cancelled(void *data, struct ext_data_control_source_v1 *s) {
    g_bytes_unref(data);
    ext_data_control_source_v1_destroy(s);
}

static const struct ext_data_control_source_v1_listener source_listener = {
    .send = source_send,
    .cancelled = source_cancelled,
};

static gboolean set_selection(Clipboard *c, gboolean image, GBytes *data) {
    static const gchar *text_mimes[] = { "text/plain;charset=utf-8", "text/plain", "UTF8_STRING", "TEXT", "STRING" };

    if (c == NULL || c->manager == NULL || c->device == NULL)
        return FALSE;

    struct ext_data_control_source_v1 *s = ext_data_control_manager_v1_create_data_source(c->manager);

    ext_data_control_source_v1_add_listener(s, &source_listener, g_bytes_ref(data));

    if (image)
        ext_data_control_source_v1_offer(s, "image/png");
    else
        for (guint i = 0; i < G_N_ELEMENTS(text_mimes); i++)
            ext_data_control_source_v1_offer(s, text_mimes[i]);

    ext_data_control_device_v1_set_selection(c->device, s);
    return TRUE;
}

gboolean clipboard_set(AppState *state, const gchar *text) {
    g_autoptr(GBytes) data = g_bytes_new(text, strlen(text));
    return set_selection(state->clipboard, FALSE, data);
}

gboolean clipboard_copy(AppState *state, guint32 id) {
    Entry *e = state->clipboard ? entry_find(state->clipboard, id) : NULL;
    return e && set_selection(state->clipboard, e->image, e->data);
}

gboolean clipboard_remove(AppState *state, guint32 id) {
    Clipboard *c = state->clipboard;
    Entry *e = c ? entry_find(c, id) : NULL;

    if (e == NULL)
        return FALSE;

    g_ptr_array_remove(c->history, e);
    push(state);
    return TRUE;
}

gboolean clipboard_wipe(AppState *state) {
    if (state->clipboard == NULL)
        return FALSE;

    g_ptr_array_set_size(state->clipboard->history, 0);
    push(state);
    return TRUE;
}

gboolean clipboard_serve(AppState *state, WebKitURISchemeRequest *request, const gchar *path) {
    Entry *e = state->clipboard ? entry_find(state->clipboard, (guint32)g_ascii_strtoull(path[0] == '/' ? path + 1 : path, NULL, 10)) : NULL;

    if (e == NULL || !e->image)
        return FALSE;

    g_autoptr(GInputStream) in = g_memory_input_stream_new_from_bytes(e->data);
    webkit_uri_scheme_request_finish(request, in, g_bytes_get_size(e->data), "image/png");
    return TRUE;
}

static void registry_global(void *data, struct wl_registry *reg, guint32 name, const gchar *iface, guint32 version) {
    (void)version;

    Clipboard *c = data;

    if (!g_str_equal(iface, ext_data_control_manager_v1_interface.name))
        return;

    GdkSeat *seat = gdk_display_get_default_seat(gdk_display_get_default());

    c->manager = wl_registry_bind(reg, name, &ext_data_control_manager_v1_interface, 1);
    c->device = ext_data_control_manager_v1_get_data_device(c->manager, gdk_wayland_seat_get_wl_seat(seat));
    ext_data_control_device_v1_add_listener(c->device, &device_listener, c);
    g_debug("clipboard: bound %s v1", iface);
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

void clipboard_init(AppState *state) {
    GdkDisplay *display = gdk_display_get_default();

    if (!GDK_IS_WAYLAND_DISPLAY(display)) {
        g_warning("clipboard: not running on wayland");
        return;
    }

    Clipboard *c = g_new0(Clipboard, 1);

    c->app = state;
    c->history = g_ptr_array_new_with_free_func((GDestroyNotify)entry_free);
    state->clipboard = c;

    struct wl_registry *reg = wl_display_get_registry(gdk_wayland_display_get_wl_display(display));
    wl_registry_add_listener(reg, &registry_listener, c);
}

void clipboard_clear(AppState *state) {
    Clipboard *c = state->clipboard;

    if (c == NULL)
        return;

    g_clear_pointer(&c->device, ext_data_control_device_v1_destroy);
    g_clear_pointer(&c->manager, ext_data_control_manager_v1_destroy);
    g_ptr_array_unref(c->history);
    g_clear_pointer(&state->clipboard, g_free);
}

GVariant *clipboard_history(AppState *state) {
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE("aa{sv}"));

    for (guint i = 0; state->clipboard && i < state->clipboard->history->len; i++) {
        Entry *e = g_ptr_array_index(state->clipboard->history, i);
        GVariantBuilder v;
        gsize len;
        const gchar *buf = g_bytes_get_data(e->data, &len);

        g_variant_builder_init(&v, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&v, "{sv}", "id", g_variant_new_uint32(e->id));
        g_variant_builder_add(&v, "{sv}", "image", g_variant_new_boolean(e->image));
        g_variant_builder_add(&v, "{sv}", "time", g_variant_new_int64(e->time));
        g_variant_builder_add(&v, "{sv}", "size", g_variant_new_uint64(len));

        if (e->image) {
            g_autofree gchar *url = g_strdup_printf("wwwidgets://clipboard/%u", e->id);
            g_variant_builder_add(&v, "{sv}", "url", g_variant_new_string(url));
        } else {
            const gchar *end = len > PREVIEW_MAX ? buf + PREVIEW_MAX : buf + len;

            if (end < buf + len)
                end = g_utf8_find_prev_char(buf, end + 1);

            g_autofree gchar *text = g_strndup(buf, end - buf);
            g_variant_builder_add(&v, "{sv}", "text", g_variant_new_string(text));
        }

        g_variant_builder_add_value(&b, g_variant_builder_end(&v));
    }

    return g_variant_ref_sink(g_variant_builder_end(&b));
}
