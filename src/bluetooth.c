#include <glib.h>
#include <gio/gio.h>

#include "state.h"

#define AGENT_PATH "/dev/svscagn/wwwidgets/agent"

static const gchar AGENT_XML[] =
    "<node>"
    "  <interface name='org.bluez.Agent1'>"
    "    <method name='Release'/>"
    "    <method name='RequestPinCode'>"
    "      <arg type='o' name='device' direction='in'/>"
    "      <arg type='s' name='pincode' direction='out'/>"
    "    </method>"
    "    <method name='DisplayPinCode'>"
    "      <arg type='o' name='device' direction='in'/>"
    "      <arg type='s' name='pincode' direction='in'/>"
    "    </method>"
    "    <method name='RequestPasskey'>"
    "      <arg type='o' name='device' direction='in'/>"
    "      <arg type='u' name='passkey' direction='out'/>"
    "    </method>"
    "    <method name='DisplayPasskey'>"
    "      <arg type='o' name='device' direction='in'/>"
    "      <arg type='u' name='passkey' direction='in'/>"
    "      <arg type='q' name='entered' direction='in'/>"
    "    </method>"
    "    <method name='RequestConfirmation'>"
    "      <arg type='o' name='device' direction='in'/>"
    "      <arg type='u' name='passkey' direction='in'/>"
    "    </method>"
    "    <method name='RequestAuthorization'>"
    "      <arg type='o' name='device' direction='in'/>"
    "    </method>"
    "    <method name='AuthorizeService'>"
    "      <arg type='o' name='device' direction='in'/>"
    "      <arg type='s' name='uuid' direction='in'/>"
    "    </method>"
    "    <method name='Cancel'/>"
    "  </interface>"
    "</node>";

static const gchar *WATCHED[] = { "Alias", "Icon", "Paired", "Connected", "Powered", "Discovering", "Percentage", NULL };

struct BluetoothAgent {
    GDBusConnection *bus;
    guint object_id;
    guint watch_id;
    GDBusMethodInvocation *pending;
};

static gboolean prop_b(GDBusProxy *p, const gchar *name) {
    g_autoptr(GVariant) v = g_dbus_proxy_get_cached_property(p, name);
    return v ? g_variant_get_boolean(v) : FALSE;
}

static guint8 prop_y(GDBusProxy *p, const gchar *name) {
    g_autoptr(GVariant) v = g_dbus_proxy_get_cached_property(p, name);
    return v ? g_variant_get_byte(v) : 0;
}

static gchar *prop_s(GDBusProxy *p, const gchar *name) {
    g_autoptr(GVariant) v = g_dbus_proxy_get_cached_property(p, name);
    return v ? g_variant_dup_string(v, NULL) : g_strdup("");
}

static GDBusProxy *find_adapter(AppState *state) {
    g_autolist(GDBusObject) objects = g_dbus_object_manager_get_objects(state->bluez);

    for (GList *l = objects; l; l = l->next) {
        GDBusInterface *adapter = g_dbus_object_get_interface(l->data, "org.bluez.Adapter1");

        if (adapter)
            return G_DBUS_PROXY(adapter);
    }

    return NULL;
}

static GVariant *device_variant(GDBusObject *obj, GDBusProxy *d) {
    g_autoptr(GDBusInterface) battery = g_dbus_object_get_interface(obj, "org.bluez.Battery1");
    g_autofree gchar *name = prop_s(d, "Alias");
    g_autofree gchar *address = prop_s(d, "Address");
    g_autofree gchar *icon = prop_s(d, "Icon");
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", "path", g_variant_new_string(g_dbus_object_get_object_path(obj)));
    g_variant_builder_add(&b, "{sv}", "name", g_variant_new_string(name));
    g_variant_builder_add(&b, "{sv}", "address", g_variant_new_string(address));
    g_variant_builder_add(&b, "{sv}", "icon", g_variant_new_string(icon));
    g_variant_builder_add(&b, "{sv}", "paired", g_variant_new_boolean(prop_b(d, "Paired")));
    g_variant_builder_add(&b, "{sv}", "connected", g_variant_new_boolean(prop_b(d, "Connected")));

    if (battery)
        g_variant_builder_add(&b, "{sv}", "battery", g_variant_new_int32(prop_y(G_DBUS_PROXY(battery), "Percentage")));

    return g_variant_builder_end(&b);
}

GVariant *bluetooth_snapshot(AppState *state) {
    g_autoptr(GDBusProxy) adapter = find_adapter(state);
    g_autolist(GDBusObject) objects = g_dbus_object_manager_get_objects(state->bluez);
    GVariantBuilder b, devices;

    g_variant_builder_init(&devices, G_VARIANT_TYPE("aa{sv}"));

    for (GList *l = objects; l; l = l->next) {
        g_autoptr(GDBusInterface) d = g_dbus_object_get_interface(l->data, "org.bluez.Device1");
        g_autoptr(GVariant) has_name = d ? g_dbus_proxy_get_cached_property(G_DBUS_PROXY(d), "Name") : NULL;

        if (d && (has_name || prop_b(G_DBUS_PROXY(d), "Paired")))
            g_variant_builder_add_value(&devices, device_variant(l->data, G_DBUS_PROXY(d)));
    }

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", "available", g_variant_new_boolean(adapter != NULL));
    g_variant_builder_add(&b, "{sv}", "powered", g_variant_new_boolean(adapter && prop_b(adapter, "Powered")));
    g_variant_builder_add(&b, "{sv}", "discovering", g_variant_new_boolean(adapter && prop_b(adapter, "Discovering")));
    g_variant_builder_add(&b, "{sv}", "devices", g_variant_builder_end(&devices));

    return g_variant_ref_sink(g_variant_builder_end(&b));
}

static void bluetooth_push(AppState *state) {
    g_autoptr(GVariant) snapshot = bluetooth_snapshot(state);

    for (guint i = 0; i < state->views->len; i++) {
        push_event(g_ptr_array_index(state->views, i), "bluetooth", snapshot);
    }
}

static void on_object_changed(GDBusObjectManager *manager, GDBusObject *object, gpointer user_data) {
    (void)manager;
    (void)object;

    bluetooth_push(user_data);
}

static void on_interface_changed(GDBusObjectManager *manager, GDBusObject *object, GDBusInterface *iface, gpointer user_data) {
    (void)manager;
    (void)object;
    (void)iface;

    bluetooth_push(user_data);
}

static void on_props_changed(GDBusObjectManagerClient *manager, GDBusObjectProxy *object, GDBusProxy *iface, GVariant *changed, GStrv invalidated, gpointer user_data) {
    (void)manager;
    (void)object;
    (void)iface;
    (void)invalidated;

    for (guint i = 0; WATCHED[i]; i++) {
        g_autoptr(GVariant) v = g_variant_lookup_value(changed, WATCHED[i], NULL);

        if (v) {
            bluetooth_push(user_data);
            return;
        }
    }
}

static void on_call_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    g_autoptr(GError) err = NULL;
    g_autoptr(GVariant) result = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &err);

    if (!result)
        g_warning("bluetooth: %s: %s", (const gchar *)user_data, err->message);
}

static void call(GDBusProxy *p, const gchar *iface, const gchar *method, GVariant *args) {
    g_dbus_connection_call(
          g_dbus_proxy_get_connection(p), "org.bluez", g_dbus_proxy_get_object_path(p),
          iface, method, args, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_call_done, (gpointer)method
    );
}

gboolean bluetooth_set_powered(AppState *state, gboolean powered) {
    g_autoptr(GDBusProxy) adapter = find_adapter(state);

    if (!adapter)
        return FALSE;

    call(adapter, "org.freedesktop.DBus.Properties", "Set", g_variant_new("(ssv)", "org.bluez.Adapter1", "Powered", g_variant_new_boolean(powered)));
    return TRUE;
}

gboolean bluetooth_scan(AppState *state, gboolean on) {
    g_autoptr(GDBusProxy) adapter = find_adapter(state);

    if (!adapter)
        return FALSE;

    call(adapter, "org.bluez.Adapter1", on ? "StartDiscovery" : "StopDiscovering", NULL);
    return TRUE;
}

gboolean bluetooth_connect(AppState *state, const gchar *path, gboolean connect) {
    if (!g_variant_is_object_path(path))
        return FALSE;

    g_autoptr(GDBusInterface) device = g_dbus_object_manager_get_interface(state->bluez, path, "org.bluez.Device1");

    if (!device)
        return FALSE;

    call(G_DBUS_PROXY(device), "org.bluez.Device1", connect ? "Connect" : "Disconnect", NULL);
    return TRUE;
}

static void on_pair_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    g_autofree gchar *path = user_data;
    g_autoptr(GError) err = NULL;
    g_autoptr(GVariant) result = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &err);
    GDBusConnection *bus = G_DBUS_CONNECTION(source);

    if (!result) {
        g_warning("bluetooth: Pair: %s", err->message);
        return;
    }

    g_dbus_connection_call(bus, "org.bluez", path, "org.freedesktop.DBus.Properties", "Set", g_variant_new("(ssv)", "org.bluez.Device1", "Trusted", g_variant_new_boolean(TRUE)), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_call_done, "Trusted");
    g_dbus_connection_call(bus, "org.bluez", path, "org.bluez.Device1", "Connect", NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_call_done, "Connect");
}

gboolean bluetooth_pair(AppState *state, const gchar *path) {
    if (!g_variant_is_object_path(path))
        return FALSE;

    g_autoptr(GDBusInterface) device = g_dbus_object_manager_get_interface(state->bluez, path, "org.bluez.Device1");

    if (!device)
        return FALSE;

    // Dont timeout
    g_dbus_connection_call(g_dbus_proxy_get_connection(G_DBUS_PROXY(device)), "org.bluez", path, "org.bluez.Device1", "Pair", NULL, NULL, G_DBUS_CALL_FLAGS_NONE, G_MAXINT, NULL, on_pair_done, g_strdup(path));
    return TRUE;
}

static void request_push(AppState *state, const gchar *type, const gchar *device, const gchar *code) {
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", "type", g_variant_new_string(type));

    if (device) {
        g_autoptr(GDBusInterface) d = g_dbus_object_manager_get_interface(state->bluez, device, "org.bluez.Device1");
        g_autofree gchar *name = d ? prop_s(G_DBUS_PROXY(d), "Alias") : g_strdup(device);

        g_variant_builder_add(&b, "{sv}", "device", g_variant_new_string(device));
        g_variant_builder_add(&b, "{sv}", "name", g_variant_new_string(name));
    }

    if (code)
        g_variant_builder_add(&b, "{sv}", "code", g_variant_new_string(code));

    g_autoptr(GVariant) detail = g_variant_ref_sink(g_variant_builder_end(&b));

    for (guint i = 0; i < state->views->len; i++) {
        push_event(g_ptr_array_index(state->views, i), "bluetooth.request", detail);
    }
}

static void pending_cancel(AppState *state) {
    BluetoothAgent *a = state->bt_agent;

    if (!a->pending)
        return;

    g_dbus_method_invocation_return_dbus_error(g_steal_pointer(&a->pending), "org.bluez.Error.Canceled", "canceled");
    request_push(state, "cancel", NULL, NULL);
}

static void on_agent_method(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface, const gchar *method, GVariant *params, GDBusMethodInvocation *inv, gpointer user_data) {
    (void)conn;
    (void)sender;
    (void)path;
    (void)iface;

    AppState *state = user_data;
    const gchar *device = NULL;
    guint32 passkey = 0;

    if (g_variant_n_children(params) > 0)
        g_variant_get_child(params, 0, "&o", &device);

    if (g_str_equal(method, "Release") || g_str_equal(method, "Cancel")) {
        pending_cancel(state);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_str_equal(method, "DisplayPinCode")) {
        const gchar *pin;

        g_variant_get(params, "(&o&s)", NULL, &pin);
        request_push(state, "display", device, pin);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_str_equal(method, "DisplayPasskey")) {
        g_variant_get(params, "(&ouq)", NULL, &passkey, NULL);
        g_autofree gchar *code = g_strdup_printf("%06u", passkey);

        request_push(state, "display", device, code);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else {
        // everything else needs an answer from the user, keep the call open until bluetooth_reply
        pending_cancel(state);
        state->bt_agent->pending = inv;

        if (g_str_equal(method, "RequestPinCode")) {
            request_push(state, "pin", device, NULL);
        } else if (g_str_equal(method, "RequestPasskey")) {
            request_push(state, "passkey", device, NULL);
        } else if (g_str_equal(method, "RequestConfirmation")) {
            g_variant_get(params, "(&ou)", NULL, &passkey);
            g_autofree gchar *code = g_strdup_printf("%06u", passkey);

            request_push(state, "confirm", device, code);
        } else {
            request_push(state, "authorize", device, NULL);
        }
    }
}

static const GDBusInterfaceVTable agent_vtable = {
    .method_call = on_agent_method,
};

gboolean bluetooth_reply(AppState *state, gboolean accept, const gchar *value) {
    BluetoothAgent *a = state->bt_agent;
    guint64 passkey = 0;

    if (!a || !a->pending)
        return FALSE;

    GDBusMethodInvocation *inv = g_steal_pointer(&a->pending);
    const gchar *method = g_dbus_method_invocation_get_method_name(inv);
    gboolean pin = g_str_equal(method, "RequestPinCode");
    gboolean numeric = g_str_equal(method, "RequestPasskey");
    gsize len = strlen(value);

    if (accept && pin && len >= 1 && len <= 16)
        g_dbus_method_invocation_return_value(inv, g_variant_new("(s)", value));
    else if (accept && numeric && g_ascii_string_to_unsigned(value, 10, 0, 999999, &passkey, NULL))
        g_dbus_method_invocation_return_value(inv, g_variant_new("(u)", (guint32)passkey));
    else if (accept && !pin && !numeric)
        g_dbus_method_invocation_return_value(inv, NULL);
    else
        g_dbus_method_invocation_return_dbus_error(inv, "org.bluez.Error.Rejected", "rejected");

    return TRUE;
}

static void on_bluez_appeared(GDBusConnection *conn, const gchar *name, const gchar *owner, gpointer user_data) {
    (void)name;
    (void)owner;
    (void)user_data;

    g_dbus_connection_call(conn, "org.bluez", "/org/bluez", "org.bluez.AgentManager1", "RegisterAgent", g_variant_new("(os)", AGENT_PATH, "KeyboardDisplay"), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_call_done, "RegisterAgent");
    g_dbus_connection_call(conn, "org.bluez", "/org/bluez", "org.bluez.AgentManager1", "RequestDefaultAgent",
    g_variant_new("(o)", AGENT_PATH), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_call_done, "RequestDefaultAgent");
    g_debug("bluetooth: agent registered");
}

static void on_bluez_vanished(GDBusConnection *conn, const gchar *name, gpointer user_data) {
    (void)conn;
    (void)name;

    pending_cancel(user_data);
}

static void agent_init(AppState *state) {
    g_autoptr(GError) err = NULL;
    g_autoptr(GDBusNodeInfo) info = g_dbus_node_info_new_for_xml(AGENT_XML, &err);
    BluetoothAgent *a = g_new0(BluetoothAgent, 1);

    a->bus = g_object_ref(g_dbus_object_manager_client_get_connection(G_DBUS_OBJECT_MANAGER_CLIENT(state->bluez)));
    state->bt_agent = a;

    a->object_id = g_dbus_connection_register_object(a->bus, AGENT_PATH, info->interfaces[0], &agent_vtable, state, NULL, &err);

    if (!a->object_id)
        g_warning("bluetooth: %s", err->message);

    a->watch_id = g_bus_watch_name_on_connection(a->bus, "org.bluez", G_BUS_NAME_WATCHER_FLAGS_NONE, on_bluez_appeared, on_bluez_vanished, state, NULL);
}

void bluetooth_init(AppState *state) {
    g_autoptr(GError) err = NULL;

    state->bluez = g_dbus_object_manager_client_new_for_bus_sync(G_BUS_TYPE_SYSTEM, G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE, "org.bluez", "/", NULL, NULL, NULL, NULL, &err);

    if (!state->bluez) {
        g_warning("bluetooth: %s", err->message);
        return;
    }

    g_signal_connect(state->bluez, "object-added", G_CALLBACK(on_object_changed), state);
    g_signal_connect(state->bluez, "object-removed", G_CALLBACK(on_object_changed), state);
    g_signal_connect(state->bluez, "interface-added", G_CALLBACK(on_interface_changed), state);
    g_signal_connect(state->bluez, "interface-removed", G_CALLBACK(on_interface_changed), state);
    g_signal_connect(state->bluez, "interface-proxy-properties-changed", G_CALLBACK(on_props_changed), state);
    g_debug("bluetooth signals connected");

    agent_init(state);
}

void bluetooth_clear(AppState *state) {
    BluetoothAgent *a = state->bt_agent;

    if (a) {
        if (a->pending)
            g_dbus_method_invocation_return_dbus_error(g_steal_pointer(&a->pending), "org.bluez.Error.Canceled", "shutting down");

        g_bus_unwatch_name(a->watch_id);

        if (a->object_id)
            g_dbus_connection_unregister_object(a->bus, a->object_id);

        g_object_unref(a->bus);
        g_clear_pointer(&state->bt_agent, g_free);
    }

    g_clear_object(&state->bluez);
}
