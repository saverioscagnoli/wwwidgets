#include <glib.h>
#include <gio/gio.h>

#include "network.h"
#include "state.h"

#define NM_NAME "org.freedesktop.NetworkManager"
#define NM_PATH "/org/freedesktop/NetworkManager"
#define NM_WIRELESS NM_NAME ".Device.Wireless"
#define NM_AP NM_NAME ".AccessPoint"
#define NM_ACTIVE NM_NAME ".Connection.Active"

#define AP_FLAGS_PRIVACY 0x1
#define AP_SEC_KEY_MGMT_PSK 0x100
#define AP_SEC_KEY_MGMT_SAE 0x400

static const gchar *WATCHED[] = { "State", "Connectivity", "WirelessEnabled", "WirelessHardwareEnabled", "PrimaryConnection", "ActiveAccessPoint", "Strength", "Id", NULL };

struct Network {
    GDBusObjectManager *manager;
    guint push_id;
};

typedef struct {
    NetworkConnectCallback callback;
    gpointer user_data;
} ConnectRequest;

static const gchar *state_str(guint32 s) {
    switch (s) {
        case 10: return "asleep";
        case 20: return "disconnected";
        case 30: return "disconnecting";
        case 40: return "connecting";
        case 50: return "connected-local";
        case 60: return "connected-site";
        case 70: return "connected";
        default: return "unknown";
    };
}

static const gchar *connectivity_str(guint32 c) {
    switch (c) {
        case 1: return "none";
        case 2: return "portal";
        case 3: return "limited";
        case 4: return "full";
        default: return "unknown";
    }
}

static gboolean prop_b(GDBusProxy *p, const gchar *name) {
    g_autoptr(GVariant) v = g_dbus_proxy_get_cached_property(p, name);
    return v ? g_variant_get_boolean(v) : FALSE;
}

static guint32 prop_u(GDBusProxy *p, const gchar *name) {
    g_autoptr(GVariant) v = g_dbus_proxy_get_cached_property(p, name);
    return v ? g_variant_get_uint32(v) : 0;
}

static guint8 prop_y(GDBusProxy *p, const gchar *name) {
    g_autoptr(GVariant) v = g_dbus_proxy_get_cached_property(p, name);
    return v ? g_variant_get_byte(v) : 0;
}

static gchar *prop_s(GDBusProxy *p, const gchar *name) {
    g_autoptr(GVariant) v = g_dbus_proxy_get_cached_property(p, name);
    return v ? g_variant_dup_string(v, NULL) : g_strdup("");
}

static GDBusProxy *iface_at(Network *n, const gchar *path, const gchar *iface) {
    if (path == NULL || !g_variant_is_object_path(path) || g_str_equal(path, "/"))
        return NULL;

    GDBusInterface *i = g_dbus_object_manager_get_interface(n->manager, path, iface);
    return i ? G_DBUS_PROXY(i) : NULL;
}

static GDBusProxy *find_wifi(Network *n) {
    g_autolist(GDBusObject) objects = g_dbus_object_manager_get_objects(n->manager);

    for (GList *l = objects; l; l = l->next) {
        GDBusInterface *wifi = g_dbus_object_get_interface(l->data, NM_WIRELESS);

        if (wifi)
            return G_DBUS_PROXY(wifi);
    }

    return NULL;
}

static gchar *ap_ssid(GDBusProxy *ap) {
    g_autoptr(GVariant) v = g_dbus_proxy_get_cached_property(ap, "Ssid");
    gsize len = 0;
    const gchar *bytes = v ? g_variant_get_fixed_array(v, &len, 1) : NULL;

    return g_utf8_make_valid(bytes ? bytes : "", len);
}

static gboolean ap_secure(GDBusProxy *ap) {
    return (prop_u(ap, "Flags") & AP_FLAGS_PRIVACY) || prop_u(ap, "WpaFlags") || prop_u(ap, "RsnFlags");
}

static gboolean ap_better(GDBusProxy *a, GDBusProxy *b, const gchar *active) {
    if (b == NULL || g_strcmp0(g_dbus_proxy_get_object_path(a), active) == 0)
        return TRUE;

    if (g_strcmp0(g_dbus_proxy_get_object_path(b), active) == 0)
        return FALSE;

    return prop_y(a, "Strength") > prop_y(b, "Strength");
}

static GVariant *ap_variant(GDBusProxy *ap, const gchar *ssid, const gchar *active) {
    const gchar *path = g_dbus_proxy_get_object_path(ap);
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", "path", g_variant_new_string(path));
    g_variant_builder_add(&b, "{sv}", "ssid", g_variant_new_string(ssid));
    g_variant_builder_add(&b, "{sv}", "strength", g_variant_new_int32(prop_y(ap, "Strength")));
    g_variant_builder_add(&b, "{sv}", "secure", g_variant_new_boolean(ap_secure(ap)));
    g_variant_builder_add(&b, "{sv}", "active", g_variant_new_boolean(g_strcmp0(path, active) == 0));

    return g_variant_builder_end(&b);
}

static GVariant *access_points(Network *n, GDBusProxy *wifi, const gchar *active) {
    g_autoptr(GHashTable) best = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
    g_autoptr(GVariant) paths = wifi ? g_dbus_proxy_get_cached_property(wifi, "AccessPoints") : NULL;
    GVariantBuilder b;
    GHashTableIter it;
    gpointer ssid, ap;

    if (paths) {
        GVariantIter pit;
        const gchar *path;

        g_variant_iter_init(&pit, paths);

        while (g_variant_iter_next(&pit, "&o", &path)) {
            g_autoptr(GDBusProxy) candidate = iface_at(n, path, NM_AP);
            g_autofree gchar *name = candidate ? ap_ssid(candidate) : NULL;

            if (name && *name && ap_better(candidate, g_hash_table_lookup(best, name), active))
                g_hash_table_replace(best, g_steal_pointer(&name), g_steal_pointer(&candidate));
        }
    }

    g_variant_builder_init(&b, G_VARIANT_TYPE("aa{sv}"));
    g_hash_table_iter_init(&it, best);

    while (g_hash_table_iter_next(&it, &ssid, &ap)) {
        g_variant_builder_add_value(&b, ap_variant(ap, ssid, active));
    }

    return g_variant_builder_end(&b);
}

GVariant *network_snapshot(AppState *state) {
    Network *n = state->network;
    g_autoptr(GDBusProxy) nm = iface_at(n, NM_PATH, NM_NAME);
    g_autoptr(GDBusProxy) wifi = find_wifi(n);
    g_autofree gchar *primary_path = nm ? prop_s(nm, "PrimaryConnection") : NULL;
    g_autofree gchar *active_ap = wifi ? prop_s(wifi, "ActiveAccessPoint") : NULL;
    g_autoptr(GDBusProxy) primary = iface_at(n, primary_path, NM_ACTIVE);
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", "available", g_variant_new_boolean(nm != NULL));
    g_variant_builder_add(&b, "{sv}", "state", g_variant_new_string(state_str(nm ? prop_u(nm, "State") : 0)));
    g_variant_builder_add(&b, "{sv}", "connectivity", g_variant_new_string(connectivity_str(nm ? prop_u(nm, "Connectivity") : 0)));
    g_variant_builder_add(&b, "{sv}", "wifiEnabled", g_variant_new_boolean(nm && prop_b(nm, "WirelessEnabled")));
    g_variant_builder_add(&b, "{sv}", "wifiHardware", g_variant_new_boolean(nm && prop_b(nm, "WirelessHardwareEnabled")));

    if (primary) {
        g_autofree gchar *id = prop_s(primary, "Id");
        g_autofree gchar *type = prop_s(primary, "Type");
        GVariantBuilder p;

        g_variant_builder_init(&p, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&p, "{sv}", "name", g_variant_new_string(id));
        g_variant_builder_add(&p, "{sv}", "type", g_variant_new_string(type));
        g_variant_builder_add(&b, "{sv}", "primary", g_variant_builder_end(&p));
    }

    g_variant_builder_add(&b, "{sv}", "accessPoints", access_points(n, wifi, active_ap));

    return g_variant_ref_sink(g_variant_builder_end(&b));
}

static gboolean push_now(gpointer user_data) {
    AppState *state = user_data;

    state->network->push_id = 0;

    g_autoptr(GVariant) snapshot = network_snapshot(state);

    for (guint i = 0; i < state->views->len; i++) {
        push_event(g_ptr_array_index(state->views, i), "network", snapshot);
    }

    return G_SOURCE_REMOVE;
}

static void schedule_push(AppState *state) {
    if (state->network->push_id == 0)
        state->network->push_id = g_idle_add(push_now, state);
}

static void on_object_changed(GDBusObjectManager *manager, GDBusObject *object, gpointer user_data) {
    (void)manager;
    (void)object;

    schedule_push(user_data);
}

static void on_props_changed(GDBusObjectManagerClient *manager, GDBusObjectProxy *object, GDBusProxy *iface, GVariant *changed, GStrv invalidated, gpointer user_data) {
    (void)manager;
    (void)object;
    (void)iface;
    (void)invalidated;

    for (guint i = 0; WATCHED[i]; i++) {
        g_autoptr(GVariant) v = g_variant_lookup_value(changed, WATCHED[i], NULL);

        if (v) {
            schedule_push(user_data);
            return;
        }
    }
}

static void on_call_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    g_autoptr(GError) err = NULL;
    g_autoptr(GVariant) result = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &err);

    if (!result)
        g_warning("network: %s: %s", (const gchar *)user_data, err->message);
}

static void call(Network *n, const gchar *path, const gchar *iface, const gchar *method, GVariant *args) {
    GDBusConnection *bus = g_dbus_object_manager_client_get_connection(G_DBUS_OBJECT_MANAGER_CLIENT(n->manager));

    g_dbus_connection_call(bus, NM_NAME, path, iface, method, args, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_call_done, (gpointer)method);
}

gboolean network_set_wifi(AppState *state, gboolean enabled) {
    call(state->network, NM_PATH, "org.freedesktop.DBus.Properties", "Set", g_variant_new("(ssv)", NM_NAME, "WirelessEnabled", g_variant_new_boolean(enabled)));
    return TRUE;
}

gboolean network_scan(AppState *state) {
    g_autoptr(GDBusProxy) wifi = find_wifi(state->network);

    if (!wifi)
        return FALSE;

    call(state->network, g_dbus_proxy_get_object_path(wifi), NM_WIRELESS, "RequestScan", g_variant_new("(a{sv})", NULL));

    return TRUE;
}

gboolean network_disconnect(AppState *state) {
    g_autoptr(GDBusProxy) wifi = find_wifi(state->network);

    if (!wifi)
        return FALSE;

    call(state->network, g_dbus_proxy_get_object_path(wifi), NM_NAME ".Device", "Disconnect", NULL);

    return TRUE;
}

static void on_connect_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    ConnectRequest *r = user_data;
    g_autoptr(GError) err = NULL;
    g_autoptr(GVariant) result = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &err);

    if (err)
        g_dbus_error_strip_remote_error(err);

    r->callback(err ? err->message : NULL, r->user_data);
    g_free(r);
}

gboolean network_connect(AppState *state, const gchar *ap_path, const gchar *password, NetworkConnectCallback callback, gpointer user_data) {
    Network *n = state->network;
    g_autoptr(GDBusProxy) wifi = find_wifi(n);
    g_autoptr(GDBusProxy) ap = iface_at(n, ap_path, NM_AP);

    if (!wifi || !ap)
        return FALSE;

    GDBusConnection *bus = g_dbus_object_manager_client_get_connection(G_DBUS_OBJECT_MANAGER_CLIENT(n->manager));
    const gchar *device = g_dbus_proxy_get_object_path(wifi);
    ConnectRequest *r = g_new0(ConnectRequest, 1);

    r->callback = callback;
    r->user_data = user_data;

    if (password == NULL) {
        g_dbus_connection_call(bus, NM_NAME, NM_PATH, NM_NAME, "ActivateConnection",
        g_variant_new("(ooo)", "/", device, ap_path), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_connect_done, r);

        return TRUE;
    }

    GVariantBuilder settings;

    g_variant_builder_init(&settings, G_VARIANT_TYPE("a{sa{sv}}"));

    if (ap_secure(ap)) {
        guint32 rsn = prop_u(ap, "RsnFlags");
        gboolean sae_only = (rsn & AP_SEC_KEY_MGMT_SAE) && !(rsn & AP_SEC_KEY_MGMT_PSK);
        GVariantBuilder security;

        g_variant_builder_init(&security, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&security, "{sv}", "key-mgmt", g_variant_new_string(sae_only ? "sae" : "wpa-psk"));
        g_variant_builder_add(&security, "{sv}", "psk", g_variant_new_string(password));
        g_variant_builder_add(&settings, "{sa{sv}}", "802-11-wireless-security", &security);
    }

    g_dbus_connection_call(bus, NM_NAME, NM_PATH, NM_NAME, "AddAndActivateConnection",
    g_variant_new("(a{sa{sv}}oo)", &settings, device, ap_path), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_connect_done, r);

    return TRUE;
}

void network_init(AppState *state) {
    g_autoptr(GError) err = NULL;
    GDBusObjectManager *manager = g_dbus_object_manager_client_new_for_bus_sync(
        G_BUS_TYPE_SYSTEM, G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
        NM_NAME, "/org/freedesktop", NULL, NULL, NULL, NULL, &err
    );

    if (!manager) {
        g_warning("network: %s", err->message);
        return;
    }

    state->network = g_new0(Network, 1);
    state->network->manager = manager;

    g_signal_connect(manager, "object-added", G_CALLBACK(on_object_changed), state);
    g_signal_connect(manager, "object-removed", G_CALLBACK(on_object_changed), state);
    g_signal_connect(manager, "interface-proxy-properties-changed", G_CALLBACK(on_props_changed), state);
    g_debug("network signals connected");
}

void network_clear(AppState *state) {
    Network *n = state->network;

    if (!n)
        return;

    g_clear_handle_id(&n->push_id, g_source_remove);
    g_object_unref(n->manager);
    g_clear_pointer(&state->network, g_free);
}
