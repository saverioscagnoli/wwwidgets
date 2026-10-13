#include <gtk/gtk.h>
#include <gio/gio.h>

#include "notifications.h"
#include "state.h"
#include "image.h"

#define NOTIF_BUS_NAME "org.freedesktop.Notifications"
#define NOTIF_PATH "/org/freedesktop/Notifications"
#define NOTIF_IFACE "org.freedesktop.Notifications"
#define DEFAULT_TIMEOUT_MS 5000

enum {
    CLOSE_EXPIRED = 1,
    CLOSE_DISMISSED = 2,
    CLOSE_CALLED = 3,
};

static const gchar INTROSPECTION_XML[] =
    "<node>"
    "  <interface name='org.freedesktop.Notifications'>"
    "    <method name='GetCapabilities'>"
    "      <arg type='as' name='capabilities' direction='out'/>"
    "    </method>"
    "    <method name='Notify'>"
    "      <arg type='s' name='app_name' direction='in'/>"
    "      <arg type='u' name='replaces_id' direction='in'/>"
    "      <arg type='s' name='app_icon' direction='in'/>"
    "      <arg type='s' name='summary' direction='in'/>"
    "      <arg type='s' name='body' direction='in'/>"
    "      <arg type='as' name='actions' direction='in'/>"
    "      <arg type='a{sv}' name='hints' direction='in'/>"
    "      <arg type='i' name='expire_timeout' direction='in'/>"
    "      <arg type='u' name='id' direction='out'/>"
    "    </method>"
    "    <method name='CloseNotification'>"
    "      <arg type='u' name='id' direction='in'/>"
    "    </method>"
    "    <method name='GetServerInformation'>"
    "      <arg type='s' name='name' direction='out'/>"
    "      <arg type='s' name='vendor' direction='out'/>"
    "      <arg type='s' name='version' direction='out'/>"
    "      <arg type='s' name='spec_version' direction='out'/>"
    "    </method>"
    "    <signal name='NotificationClosed'>"
    "      <arg type='u' name='id'/>"
    "      <arg type='u' name='reason'/>"
    "    </signal>"
    "    <signal name='ActionInvoked'>"
    "      <arg type='u' name='id'/>"
    "      <arg type='s' name='action_key'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

typedef struct {
    AppState *app;
    guint32 id;
    guint32 rev;
    gchar *app_name;
    Image icon;
    Image image;
    gchar *summary;
    gchar *body;
    GStrv actions;
    guint8 urgency;
    gint64 time;
    guint timeout_id;
} Notification;

static gboolean image_load_data(Image *img, GVariant *v) {
    gint32 width, height, rowstride, bits, channels;
    gboolean alpha;
    g_autoptr(GVariant) pixels = NULL;

    g_variant_get(v, "(iiibii@ay)", &width, &height, &rowstride, &alpha, &bits, &channels, &pixels);

    if (width <= 0 || height <= 0 || bits != 8 || channels != (alpha ? 4 : 3))
        return FALSE;

    gsize len;
    const guchar *data = g_variant_get_fixed_array(pixels, &len, 1);

    if (len < (gsize)rowstride * (height - 1) + (gsize)width * channels)
        return FALSE;

    g_autoptr(GBytes) bytes = g_bytes_new(data, len);

    return image_load_pixels(img, width, height, rowstride, alpha ? GDK_MEMORY_R8G8B8A8 : GDK_MEMORY_R8G8B8, bytes);
}

static gboolean image_load_hints(Image *img, GVariant *hints) {
    static const gchar *data_keys[] = { "image-data", "image_data", "icon_data" };
    static const gchar *path_keys[] = { "image-path", "image_path" };

    for (guint i = 0; i < G_N_ELEMENTS(data_keys); i++) {
        g_autoptr(GVariant) v = g_variant_lookup_value(hints, data_keys[i], G_VARIANT_TYPE("(iiibiiay)"));

        if (v)
            return image_load_data(img, v);
    }

    for (guint i = 0; i < G_N_ELEMENTS(path_keys); i++) {
        const gchar *path;

        if (g_variant_lookup(hints, path_keys[i], "&s", &path))
            return image_load_icon(img, path, NULL);
    }

    return FALSE;
}

static gchar *image_url(Notification *n, Image *img, const gchar *kind) {
    if (!img->bytes)
        return g_strdup("");

    return g_strdup_printf("wwwidgets://notifications/%u/%s?%u", n->id, kind, n->rev);
}

static void notification_free(Notification *n) {
    g_clear_handle_id(&n->timeout_id, g_source_remove);
    g_free(n->app_name);
    image_clear(&n->icon);
    image_clear(&n->image);
    g_free(n->summary);
    g_free(n->body);
    g_strfreev(n->actions);
    g_free(n);
}

static Notification *notification_find(AppState *state, guint32 id) {
    for (guint i = 0; i < state->notifications->len; i++) {
        Notification *n = g_ptr_array_index(state->notifications, i);

        if (n->id == id)
            return n;
    }

    return NULL;
}

static GVariant *notification_to_variant(Notification *n) {
    GVariantBuilder actions;

    g_variant_builder_init(&actions, G_VARIANT_TYPE("aa{sv}"));

    for (guint i = 0; n->actions[i] && n->actions[i + 1]; i += 2) {
        GVariantBuilder a;

        g_variant_builder_init(&a, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&a, "{sv}", "key", g_variant_new_string(n->actions[i]));
        g_variant_builder_add(&a, "{sv}", "label",  g_variant_new_string(n->actions[i + 1]));
        g_variant_builder_add_value(&actions, g_variant_builder_end(&a));
    }

    g_autofree gchar *icon = image_url(n, &n->icon, "icon");
    g_autofree gchar *image = image_url(n, &n->image, "image");
    GVariantBuilder b;

  g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&b, "{sv}", "id", g_variant_new_uint32(n->id));
  g_variant_builder_add(&b, "{sv}", "app", g_variant_new_string(n->app_name));
  g_variant_builder_add(&b, "{sv}", "icon", g_variant_new_string(icon));
  g_variant_builder_add(&b, "{sv}", "image", g_variant_new_string(image));
  g_variant_builder_add(&b, "{sv}", "summary", g_variant_new_string(n->summary));
  g_variant_builder_add(&b, "{sv}", "body", g_variant_new_string(n->body));
  g_variant_builder_add(&b, "{sv}", "urgency", g_variant_new_byte(n->urgency));
  g_variant_builder_add(&b, "{sv}", "time", g_variant_new_int64(n->time));
  g_variant_builder_add(&b, "{sv}", "actions", g_variant_builder_end(&actions));

  return g_variant_builder_end(&b);
}

static void notifications_changed(AppState *state) {
    g_autoptr(GVariant) list = notifications_list(state);

    for (guint i = 0; i < state->views->len; i++) {
        push_event(g_ptr_array_index(state->views, i), "notifications", list);
    }
}

static void notification_remove(AppState *state, Notification *n, guint32 reason) {
    if (state->notif_bus) {
        g_dbus_connection_emit_signal(state->notif_bus, NULL, NOTIF_PATH, NOTIF_IFACE, "NotificationClosed", g_variant_new("(uu)", n->id, reason), NULL);
    }

    g_ptr_array_remove(state->notifications, n);
    notifications_changed(state);
}

static gboolean on_timeout(gpointer user_data) {
    Notification *n = user_data;

    n->timeout_id = 0;
    notification_remove(n->app, n, CLOSE_EXPIRED);
    return G_SOURCE_REMOVE;
}

static guint32 handle_notify(AppState *state, GVariant *params) {
    const gchar *app_name, *icon, *summary, *body;

    g_autofree const gchar **actions = NULL;
    g_autoptr(GVariant) hints = NULL;
    guint32 replaces_id;
    gint32 timeout;

    g_variant_get(params, "(&su&s&s&s^a&s@a{sv}i)", &app_name, &replaces_id, &icon, &summary, &body, &actions, &hints, &timeout);

    guint8 urgency = 1;
    g_variant_lookup(hints, "urgency", "y", &urgency);

    Notification *n = replaces_id ? notification_find(state, replaces_id) : NULL;

    if (n) {
        g_clear_handle_id(&n->timeout_id, g_source_remove);
        g_free(n->app_name);
        image_clear(&n->icon);
        image_clear(&n->image);
        g_free(n->summary);
        g_free(n->body);
        g_strfreev(n->actions);
    } else {
        n = g_new0(Notification, 1);
        n->app = state;
        n->id = ++state->notif_next_id;
        g_ptr_array_add(state->notifications, n);
    }

    n->app_name = g_strdup(app_name);
    image_load_icon(&n->icon, icon, NULL);
    image_load_hints(&n->image, hints);
    n->rev++;
    n->summary = g_strdup(summary);
    n->body = g_strdup(body);
    n->actions = g_strdupv((gchar **)actions);
    n->urgency = urgency;
    n->time = g_get_real_time() / 1000;

    if (timeout < 0)
        timeout = DEFAULT_TIMEOUT_MS;

    if (timeout > 0 && urgency < 2)
        n->timeout_id = g_timeout_add(timeout, on_timeout, n);

    g_debug("notification %u from %s: %s", n->id, n->app_name, n->summary);
    notifications_changed(state);

    return n->id;
}

static void on_method_call(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface, const gchar *method, GVariant *params, GDBusMethodInvocation *inv, gpointer user_data) {
    (void)conn;
    (void)sender;
    (void)path;
    (void)iface;

    AppState *state = user_data;

    if (g_str_equal(method, "Notify")) {
        guint32 id = handle_notify(state, params);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(u)", id));
    } else if (g_str_equal(method, "CloseNotification")) {
        guint32 id;
        g_variant_get(params, "(u)", &id);

        Notification *n = notification_find(state, id);

        if (n)
            notification_remove(state, n, CLOSE_CALLED);

        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_str_equal(method, "GetCapabilities")) {
        const gchar *caps[] = { "body", "actions", "icon-static", NULL };
        g_dbus_method_invocation_return_value(inv, g_variant_new("(^as)", caps));
    } else if (g_str_equal(method, "GetServerInformation")) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(ssss)", "wwwidgets", "svscagn", "0.1", "1.2"));
    } else {
        g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "unknown method %s", method);
    }
}

static const GDBusInterfaceVTable vtable = {
    .method_call = on_method_call
};

static void on_bus_acquired(GDBusConnection *conn, const gchar *name, gpointer user_data) {
    (void)name;

    AppState *state = user_data;
    g_autoptr(GError) err = NULL;
    g_autoptr(GDBusNodeInfo) info = g_dbus_node_info_new_for_xml(INTROSPECTION_XML, &err);

    if (!info) {
        g_warning("notifications: %s", err->message);
        return;
    }

    if (!g_dbus_connection_register_object(conn, NOTIF_PATH, info->interfaces[0], &vtable, state, NULL, &err)) {
        g_warning("notifications: %s", err->message);
        return;
    }

    state->notif_bus = g_object_ref(conn);
}

static void on_name_acquired(GDBusConnection *conn, const gchar *name, gpointer user_data) {
    (void)conn;
    (void)user_data;
    g_debug("notifications: owning %s", name);
}

static void on_name_lost(GDBusConnection *conn, const gchar *name, gpointer user_data) {
    (void)conn;
    (void)user_data;
    g_warning("notifications: could not own %s, is another notification daemon running?", name);
}

void notifications_init(AppState *state) {
    state->notifications = g_ptr_array_new_with_free_func((GDestroyNotify)notification_free);
    state->notif_owner = g_bus_own_name(G_BUS_TYPE_SESSION, NOTIF_BUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE, on_bus_acquired, on_name_acquired, on_name_lost, state, NULL);
}

void notifications_clear(AppState *state) {
    g_clear_handle_id(&state->notif_owner, g_bus_unown_name);
    g_clear_object(&state->notif_bus);
    g_clear_pointer(&state->notifications, g_ptr_array_unref);
}

GVariant *notifications_list(AppState *state) {
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE("aa{sv}"));

    for (guint i = 0; state->notifications && i < state->notifications->len; i++) {
        g_variant_builder_add_value(&b, notification_to_variant(g_ptr_array_index(state->notifications, i)));
    }

    return g_variant_ref_sink(g_variant_builder_end(&b));
}

gboolean notifications_close(AppState *state, guint32 id) {
    Notification *n = state->notifications ? notification_find(state, id) : NULL;

    if (!n)
        return FALSE;

    notification_remove(state, n, CLOSE_DISMISSED);
    return TRUE;
}

gboolean notifications_invoke(AppState *state, guint32 id, const gchar *key) {
    Notification *n = state->notifications ? notification_find(state, id) : NULL;

    if (!n || !g_strv_contains((const gchar *const *)n->actions, key))
        return FALSE;

    if (state->notif_bus)
        g_dbus_connection_emit_signal(state->notif_bus, NULL, NOTIF_PATH, NOTIF_IFACE, "ActionInvoked", g_variant_new("(us)", id, key), NULL);

    notification_remove(state, n, CLOSE_DISMISSED);
    return TRUE;
}

gboolean notifications_serve(AppState *state, WebKitURISchemeRequest *request, const gchar *path) {
    g_auto(GStrv) parts = g_strsplit(path[0] == '/' ? path + 1 : path, "/", 0);

    if (!state->notifications || g_strv_length(parts) != 2)
        return FALSE;

    Notification *n = notification_find(state, (guint32)g_ascii_strtoull(parts[0], NULL, 10));

    if (!n)
        return FALSE;

    Image *img = g_str_equal(parts[1], "icon") ? &n->icon : g_str_equal(parts[1], "image") ? &n->image : NULL;

    if (!img || !img->bytes)
        return FALSE;

    image_serve(img, request);

    return TRUE;
}
