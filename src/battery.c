#include <glib.h>
#include <gio/gio.h>

#include "battery.h"
#include "state.h"

static const gchar *state_str(guint32 s) {
    switch (s) {
        case 1: return "charging";
        case 2: return "discharging";
        case 3: return "empty";
        case 4: return "full";
        case 5: return "pending charge";
        case 6: return "pending discharge";
        default: return "unknown";
    }
}

static gdouble prop_d(GDBusProxy *p, const gchar *name) {
    g_autoptr(GVariant) v = g_dbus_proxy_get_cached_property(p, name);
    return v ? g_variant_get_double(v) : 0;
}

static gint64 prop_x(GDBusProxy *p, const gchar *name) {
    g_autoptr(GVariant) v = g_dbus_proxy_get_cached_property(p, name);
    return v ? g_variant_get_int64(v) : 0;
}

static guint32 prop_u(GDBusProxy *p, const char *name) {
    g_autoptr(GVariant) v = g_dbus_proxy_get_cached_property(p, name);
    return v ? g_variant_get_uint32(v) : 0;
}

GVariant *battery_snapshot(GDBusProxy *p) {
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", "percent", g_variant_new_double(prop_d(p, "Percentage")));
    g_variant_builder_add(&b, "{sv}", "state", g_variant_new_string(state_str(prop_u(p, "State"))));
    g_variant_builder_add(&b, "{sv}", "timeToEmpty", g_variant_new_int64(prop_x(p, "TimeToEmpty")));
    g_variant_builder_add(&b, "{sv}", "timeToFull", g_variant_new_uint64(prop_x(p, "TimeToFull")));

    return g_variant_ref_sink(g_variant_builder_end(&b));
}

static void on_props_changed(GDBusProxy *p, GVariant *changed, GStrv invalidated, gpointer user_data) {
    (void)changed;
    (void)invalidated;
    AppState *state = user_data;

    g_autoptr(GVariant) snap = battery_snapshot(p);

    for (guint i = 0; i < state->views->len; i++) {
        push_event(g_ptr_array_index(state->views, i), "battery", snap);
    }
}

void battery_init(AppState *state) {
    g_autoptr(GError) err = NULL;

    state->upower = g_dbus_proxy_new_for_bus_sync(
        G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
        "org.freedesktop.UPower",
        "/org/freedesktop/UPower/devices/DisplayDevice",
        "org.freedesktop.UPower.Device", NULL, &err
    );

    if (!state->upower) {
        g_warning("battery: %s", err->message);
        return;
    }

    g_signal_connect(state->upower, "g-properties-changed", G_CALLBACK(on_props_changed), state);
    g_debug("battery signal connected");
}
