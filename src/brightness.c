#include <glib.h>
#include <gio/gio.h>
#include <gudev/gudev.h>

#include "brightness.h"
#include "state.h"

struct Brightness {
    GUdevClient *udev;
    GUdevDevice *device;
    GDBusProxy *session;
};

static GUdevDevice *pick_device(GUdevClient *udev) {
    static const gchar *order[] = { "firmware", "platform", "raw" };
    GList *devices = g_udev_client_query_by_subsystem(udev, "backlight");
    GUdevDevice *best = NULL;

    for (guint i = 0; i < G_N_ELEMENTS(order) && !best; i++) {
        for (GList *l = devices; l && !best; l = l->next) {
            if (g_strcmp0(g_udev_device_get_sysfs_attr(l->data, "type"), order[i]) == 0)
                best = g_object_ref(l->data);
        }
    }

    g_list_free_full(devices, g_object_unref);
    return best;
}

GVariant *brightness_snapshot(AppState *state) {
    GUdevDevice *d = state->brightness->device;
    gint max = g_udev_device_get_sysfs_attr_as_int_uncached(d, "max_brightness");
    gint raw = g_udev_device_get_sysfs_attr_as_int_uncached(d, "brightness");
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", "device", g_variant_new_string(g_udev_device_get_name(d)));
    g_variant_builder_add(&b, "{sv}", "value", g_variant_new_double(max > 0 ? (gdouble)raw / max : 0));
    g_variant_builder_add(&b, "{sv}", "raw", g_variant_new_int32(raw));
    g_variant_builder_add(&b, "{sv}", "max", g_variant_new_int32(max));

    return g_variant_ref_sink(g_variant_builder_end(&b));
}

static void on_uevent(GUdevClient *udev, const gchar *action, GUdevDevice *device, gpointer user_data) {
    (void)udev;
    AppState *state = user_data;

    if (!g_str_equal(action, "change"))
        return;

    if (g_strcmp0(g_udev_device_get_sysfs_path(device), g_udev_device_get_sysfs_path(state->brightness->device)) != 0)
        return;

    g_autoptr(GVariant) snap = brightness_snapshot(state);

    for (guint i = 0; i < state->views->len; i++) {
        push_event(g_ptr_array_index(state->views, i), "brightness", snap);
    }
}

static void on_set_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    (void)user_data;
    g_autoptr(GError) err = NULL;
    g_autoptr(GVariant) ret = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &err);

    if (!ret)
        g_warning("brightness: %s", err->message);
}

gboolean brightness_set(AppState *state, gdouble value) {
    Brightness *b = state->brightness;

    if (!b || !b->session)
        return FALSE;

    gint max = g_udev_device_get_sysfs_attr_as_int_uncached(b->device, "max_brightness");

    if (max <= 0)
        return FALSE;

    guint32 raw = CLAMP((guint32)(CLAMP(value, 0, 1) * max + 0.5), 1, (guint32)max);

    g_dbus_proxy_call(
        b->session, "SetBrightness",
        g_variant_new("(ssu)", "backlight", g_udev_device_get_name(b->device), raw),
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_set_done, NULL
    );

    return TRUE;
}

void brightness_init(AppState *state) {
    static const gchar *subsystems[] = { "backlight", NULL };
    g_autoptr(GError) err = NULL;
    GUdevClient *udev = g_udev_client_new(subsystems);
    GUdevDevice *device = pick_device(udev);

    if (!device) {
        g_warning("brightness: no backlight device");
        g_object_unref(udev);
        return;
    }

    state->brightness = g_new0(Brightness, 1);
    state->brightness->udev = udev;
    state->brightness->device = device;
    state->brightness->session = g_dbus_proxy_new_for_bus_sync(
        G_BUS_TYPE_SYSTEM,
        G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS, NULL,
        "org.freedesktop.login1",
        "/org/freedesktop/login1/session/auto",
        "org.freedesktop.login1.Session", NULL, &err
    );

    if (!state->brightness->session)
        g_warning("brightness: %s", err->message);

    g_signal_connect(udev, "uevent", G_CALLBACK(on_uevent), state);
    g_debug("brightness: using %s", g_udev_device_get_name(device));
}

void brightness_clear(AppState *state) {
    Brightness *b = state->brightness;

    if (!b)
        return;

    g_clear_object(&b->session);
    g_object_unref(b->device);
    g_object_unref(b->udev);
    g_clear_pointer(&state->brightness, g_free);
}
