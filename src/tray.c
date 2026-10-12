#include <gtk/gtk.h>
#include <unistd.h>

#include "config.h"
#include "gio/gio.h"
#include "glib.h"
#include "glibconfig.h"
#include "image.h"
#include "state.h"
#include "tray.h"

#define WATCHER_NAME "org.kde.StatusNotifierWatcher"
#define WATCHER_PATH "/StatusNotifierWatcher"
#define WATCHER_IFACE "org.kde.StatusNotifierWatcher"
#define ITEM_IFACE "org.kde.StatusNotifierItem"
#define ITEM_DEFAULT_PATH "/StatusNotifierItem"
#define MENU_IFACE "com.canonical.dbusmenu"

typedef struct {
    gchar *key;
    guint watch_id;
} Registration;

typedef struct {
    Tray *tray;
    guint32 id;
    guint32 rev;
    gchar *key;
    gchar *bus;
    gchar *path;
    GVariant *props;
    Image icon;
    guint signal_id;
    GCancellable *cancellable;
} TrayItem;

struct Tray {
    AppState *app;
    GDBusConnection *bus;
    guint watcher_object;
    guint watcher_owner;
    gchar *watcher_path;
    guint host_owner;
    gchar *host_name;
    guint watcher_watch;
    guint sig_registered;
    guint sig_unregistered;
    GPtrArray *registrations;
    GPtrArray *items;
    guint32 next_id;
};

static void registration_fee(Registration *r) {
    g_bus_unwatch_name(r->watch_id);
    g_free(r->key);
    g_free(r);
}

static Registration *registration_find(Tray *t, const gchar *key) {
    for (guint i = 0; i < t->registrations->len; i++) {
        Registration *r = g_ptr_array_index(t->registrations, i);

        if (g_str_equal(r->key, key))
            return r;
    }

    return NULL;
}

static void watcher_emit(Tray *t, const gchar *signal, const gchar *key) {
    g_dbus_connection_emit_signal(t->bus, NULL, WATCHER_PATH, WATCHER_IFACE, signal, key ? g_variant_new("(s)", key) : NULL, NULL);
}

static void on_registration_vanished(GDBusConnection *conn, const gchar *name, gpointer user_data) {
    (void)conn;

    Tray *t = user_data;
    gsize len = strlen(name);

    for (guint i = t->registrations->len; i > 0; i--) {
        Registration *r = g_ptr_array_index(t->registrations, i - 1);

        if (strncmp(r->key, name, len) == 0 && r->key[len] == '/') {
            watcher_emit(t, "StatusNotifierItemUnregistered", r->key);
            g_ptr_array_remove_index(t->registrations, i - 1);
        }
    }
}

static void watcher_register_item(Tray *t, GDBusMethodInvocation *inv, const gchar *service) {
    const gchar *sender = g_dbus_method_invocation_get_sender(inv);
    const gchar *bus = service[0] == '/' ? sender : service;
    const gchar *path = service[0] == '/' ? service : ITEM_DEFAULT_PATH;

    g_autofree gchar *key = g_strconcat(bus, path, NULL);

    if (!registration_find(t, key)) {
        Registration *r = g_new0(Registration, 1);

        r->key = g_strdup(key);
        r->watch_id = g_bus_watch_name_on_connection(t->bus, bus, G_BUS_NAME_WATCHER_FLAGS_NONE, NULL, on_registration_vanished, t, NULL);
        g_ptr_array_add(t->registrations, r);
        watcher_emit(t, "StatusNotifierItemRegistered", key);
    }

    g_dbus_method_invocation_return_value(inv, NULL);
}

static void on_watcher_method(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface, const gchar *method, GVariant *params, GDBusMethodInvocation *inv, gpointer user_data)  {
    (void)conn;
    (void)sender;
    (void)path;
    (void)iface;

    Tray *t = user_data;
    const gchar *service;

    g_variant_get(params, "(&s)", &service);

    if (g_str_equal(method, "RegisterStatusNotifierItem")) {
        watcher_register_item(t, inv, service);
    } else if (g_str_equal(method, "RegisterStatusNotifierHost")) {
        watcher_emit(t, "StatusNotifierHostRegistered", NULL);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else {
        g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "unknown method %s", method);
    }
}

static GVariant *on_watcher_property(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface, const gchar *prop, GError **error, gpointer user_data) {
    (void)conn;
    (void)sender;
    (void)path;
    (void)iface;
    (void)error;

    Tray *t = user_data;

    if (g_str_equal(prop, "RegisteredStatusNotifierItems")) {
        GVariantBuilder b;

        g_variant_builder_init(&b, G_VARIANT_TYPE_STRING_ARRAY);

        for (guint i = 0; i < t->registrations->len; i++) {
            g_variant_builder_add(&b, "s", ((Registration *)g_ptr_array_index(t->registrations, i))->key);
        }

        return g_variant_builder_end(&b);
    }

    if (g_str_equal(prop, "IsStatusNotifierHostRegistered"))
        return g_variant_new_boolean(TRUE);

    if (g_str_equal(prop, "ProtocolVersion"))
        return g_variant_new_int32(0);

    return NULL;
}

static const GDBusInterfaceVTable watcher_vtable = {
    .method_call = on_watcher_method,
    .get_property = on_watcher_property
};

static void on_watcher_acquired(GDBusConnection *conn, const gchar *name, gpointer user_data) {
    (void)conn;
    (void)user_data;
    g_debug("tray: owning %s", name);
}

static void on_watcher_lost(GDBusConnection *conn, const gchar *name, gpointer user_data) {
    (void)conn;
    (void)user_data;
    g_debug("tray: %s is owned by someone else, using theirs", name);
}

static void item_free(TrayItem *it) {
    g_cancellable_cancel(it->cancellable);
    g_object_unref(it->cancellable);
    g_dbus_connection_signal_unsubscribe(it->tray->bus, it->signal_id);
    g_free(it->key);
    g_free(it->bus);
    g_free(it->path);
    g_clear_pointer(&it->props, g_variant_unref);
    image_clear(&it->icon);
    g_free(it);
}

static TrayItem *item_find(Tray *t, guint32 id) {
    for (guint i = 0; i < t->items->len; i++) {
        TrayItem *it = g_ptr_array_index(t->items, i);

        if (it->id == id)
            return it;
    }

    return NULL;
}

static TrayItem *item_find_key(Tray *t, const gchar *key) {
    for (guint i = 0; i < t->items->len; i++) {
        TrayItem *it = g_ptr_array_index(t->items, i);

        if (g_str_equal(it->key, key))
            return it;
    }

    return NULL;
}

static const gchar *item_str(TrayItem *it, const gchar *name) {
    const gchar *s = NULL;

    if (it->props)
        g_variant_lookup(it->props, name, "&s", &s);

    return s ? s : "";
}

static const gchar *item_menu(TrayItem *it) {
    const gchar *menu = NULL;

    if (it->props)
        g_variant_lookup(it->props, "Menu", "&o", &menu);

    return menu && !g_str_equal(menu, "/") ? menu : NULL;
}

static gboolean load_pixmap(Image *img, GVariant *pixmaps) {
    GVariantIter iter;
    gint32 w, h, best_w = 0, best_h = 0;
    GVariant *data;
    g_autoptr(GVariant) best = NULL;

    g_variant_iter_init(&iter, pixmaps);

    while (g_variant_iter_next(&iter, "(ii@ay)", &w, &h, &data)) {
        if (w  > 0 && h > 0 && (gint64)w * h > (gint64)best_w * best_h) {
            g_clear_pointer(&best, g_variant_unref);
            best = data;
            best_w = w;
            best_h = h;
        } else {
            g_variant_unref(data);
        }
    }

    if (!best)
        return FALSE;

    gsize len;
    gsize size = (gsize)best_w * best_h * 4;
    const guchar *pixels = g_variant_get_fixed_array(best, &len, 1);

    if (len < size)
        return FALSE;

    g_autoptr(GBytes) bytes = g_bytes_new(pixels, size);

    return image_load_pixels(img, best_w, best_h, (gsize)best_w * 4, GDK_MEMORY_A8R8G8B8, bytes);
}

static gboolean item_load_icon(TrayItem *it, const gchar *name_prop, const gchar *pixmap_prop) {
    if (image_load_icon(&it->icon, item_str(it, name_prop), item_str(it, "IconThemePath")))
        return TRUE;

    g_autoptr(GVariant) pixmap = g_variant_lookup_value(it->props, pixmap_prop, G_VARIANT_TYPE("a(iiay)"));

    return pixmap && load_pixmap(&it->icon, pixmap);
}

static void item_update_icon(TrayItem *it) {
    image_clear(&it->icon);

    if (g_str_equal(item_str(it, "Status"), "NeedsAttention") && item_load_icon(it, "AttentionIconName", "AttentionIconPixmap"))
        return;

    item_load_icon(it, "IconName", "IconPixmap");
}

static GVariant *item_to_variant(TrayItem *it) {
    g_autofree gchar *icon = it->icon.bytes ? g_strdup_printf("wwwidgets://tray/%u/icon?%u", it->id, it->rev) : g_strdup("");
    g_autoptr(GVariant) tooltip = g_variant_lookup_value(it->props, "ToolTip", G_VARIANT_TYPE("(sa(iiay)ss)"));
    const gchar *tooltip_title = "", *tooltip_body = "";
    gboolean item_is_menu = FALSE;

    if (tooltip) {
        g_variant_get_child(tooltip, 2, "&s", &tooltip_title);
        g_variant_get_child(tooltip, 3, "&s", &tooltip_body);
    }

    g_variant_lookup(it->props, "ItemIsMenu", "b", &item_is_menu);

    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", "id", g_variant_new_uint32(it->id));
    g_variant_builder_add(&b, "{sv}", "appId", g_variant_new_string(item_str(it, "Id")));
    g_variant_builder_add(&b, "{sv}", "title", g_variant_new_string(item_str(it, "Title")));
    g_variant_builder_add(&b, "{sv}", "status", g_variant_new_string(item_str(it, "Status")));
    g_variant_builder_add(&b, "{sv}", "icon", g_variant_new_string(icon));
    g_variant_builder_add(&b, "{sv}", "tooltip", g_variant_new_string(tooltip_title));
    g_variant_builder_add(&b, "{sv}", "tooltipBody", g_variant_new_string(tooltip_body));
    g_variant_builder_add(&b, "{sv}", "itemIsMenu", g_variant_new_boolean(item_is_menu));
    g_variant_builder_add(&b, "{sv}", "hasMenu", g_variant_new_boolean(item_menu(it) != NULL));

    return g_variant_builder_end(&b);
}

static void tray_changed(Tray *t) {
    g_autoptr(GVariant) list = tray_list(t->app);

    for (guint i = 0; i < t->app->views->len; i++) {
        push_event(g_ptr_array_index(t->app->views, i), "tray", list);
    }
}

static void on_item_props(GObject *source, GAsyncResult *res, gpointer user_data) {
    g_autoptr(GError) err = NULL;
    g_autoptr(GVariant) ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &err);

    if (!ret) {
        if (!g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            g_warning("tray: %s", err->message);

        return;
    }

    TrayItem *it = user_data;

    g_clear_pointer(&it->props, g_variant_unref);
    g_variant_get(ret, "(@a{sv})", &it->props);
    item_update_icon(it);

    it->rev++;

    tray_changed(it->tray);
}

static void item_refresh(TrayItem *it) {
    g_dbus_connection_call(it->tray->bus, it->bus, it->path, "org.freedesktop.DBus.Properties", "GetAll", g_variant_new("(s)", ITEM_IFACE), G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, -1, it->cancellable, on_item_props, it);
}

static void on_item_signal(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface, const gchar *signal, GVariant *params, gpointer user_data) {
    (void)conn;
    (void)sender;
    (void)path;
    (void)iface;
    (void)signal;
    (void)params;

    item_refresh(user_data);
}

static void item_add(Tray *t, const gchar *key) {
    if (item_find_key(t, key))
        return;

    const gchar *slash = strchr(key, '/');
    TrayItem *it = g_new0(TrayItem, 1);

    it->tray = t;
    it->id = ++t->next_id;
    it->key = g_strdup(key);
    it->bus = slash ? g_strndup(key, slash - key) : g_strdup(key);
    it->path = g_strdup(slash ? slash : ITEM_DEFAULT_PATH);
    it->cancellable = g_cancellable_new();
    it->signal_id = g_dbus_connection_signal_subscribe(t->bus, it->bus, ITEM_IFACE, NULL, it->path, NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_item_signal, it, NULL);

    g_ptr_array_add(t->items, it);
    g_debug("tray: item %u added (%s)", it->id, key);
    item_refresh(it);
}

static void item_remove(Tray *t, const gchar *key) {
    TrayItem *it = item_find_key(t, key);

    if (!it)
        return;

    g_debug("tray: item %u removed (%s)", it->id, key);
    g_ptr_array_remove(t->items, it);
    tray_changed(t);
}

static void on_watcher_signal(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface, const gchar *signal, GVariant *params, gpointer user_data) {
    (void)conn;
    (void)sender;
    (void)path;
    (void)iface;

    Tray *t = user_data;
    const gchar *key;

    if (!g_variant_is_of_type(params, G_VARIANT_TYPE("(s)")))
        return;

    g_variant_get(params, "(&s)", &key);

    if (g_str_equal(signal, "StatusNotifierItemRegistered"))
        item_add(t, key);
    else
        item_remove(t, key);
}

static void on_registered_items(GObject *source, GAsyncResult *res, gpointer user_data) {
    g_autoptr(GError) err = NULL;
    g_autoptr(GVariant) ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &err);

    if (!ret) {
        g_warning("tray: %s", err->message);
        return;
    }

    Tray *t = user_data;
    g_autoptr(GVariant) value = NULL;
    g_autofree const gchar **keys = NULL;

    g_variant_get(ret, "(v)", &value);
    keys = g_variant_get_strv(value, NULL);

    for (guint i = 0; keys[i]; i++)
        item_add(t, keys[i]);
}

static void on_watcher_appeared(GDBusConnection *conn, const gchar *name, const gchar *owner, gpointer user_data) {
    (void)name;
    (void)owner;

    Tray *t = user_data;

    g_dbus_connection_call(conn, WATCHER_NAME, WATCHER_PATH, WATCHER_IFACE, "RegisterStatusNotifierHost", g_variant_new("(s)", t->host_name), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    g_dbus_connection_call(conn, WATCHER_NAME, WATCHER_PATH, "org.freedesktop.DBus.Properties", "Get", g_variant_new("(ss)", WATCHER_IFACE, "RegisteredStatusNotifierItems"), G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_registered_items, t);
}

static void on_watcher_vanished(GDBusConnection *conn, const gchar *name, gpointer user_data) {
    (void)conn;
    (void)name;

    Tray *t = user_data;

    if (t->items->len == 0)
        return;

    g_ptr_array_set_size(t->items, 0);
    tray_changed(t);
}

void tray_init(AppState *state) {
    g_autoptr(GError) err = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);

    if (!bus) {
        g_warning("tray: %s", err->message);
        return;
    }

    g_autoptr(GDBusNodeInfo) info = g_dbus_node_info_new_for_xml(WATCHER_XML, &err);
    Tray *t = g_new(Tray, 1);

    t->app = state;
    t->bus = bus;
    t->registrations = g_ptr_array_new_with_free_func((GDestroyNotify)registration_fee);
    t->items = g_ptr_array_new_with_free_func((GDestroyNotify)item_free);
    t->host_name = g_strdup_printf("org.kde.StatusNotifierHost-%d", getpid());

    state->tray = t;

    t->watcher_object = g_dbus_connection_register_object(bus, WATCHER_PATH, info->interfaces[0], &watcher_vtable, t, NULL, &err);

    if (!t->watcher_object)
        g_warning("tray: %s", err->message);

    t->watcher_owner = g_bus_own_name_on_connection(bus, WATCHER_NAME, G_BUS_NAME_OWNER_FLAGS_NONE, on_watcher_acquired, on_watcher_lost, t, NULL);
    t->host_owner = g_bus_own_name_on_connection(bus, t->host_name, G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, NULL, NULL);
    t->sig_registered = g_dbus_connection_signal_subscribe(bus, WATCHER_NAME, WATCHER_IFACE, "StatusNotifierItemRegistered", WATCHER_PATH, NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_watcher_signal, t, NULL);
    t->sig_unregistered = g_dbus_connection_signal_subscribe(bus, WATCHER_NAME, WATCHER_IFACE, "StatusNotifierItemUnregistered", WATCHER_PATH, NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_watcher_signal, t, NULL);
    t->watcher_watch = g_bus_watch_name_on_connection(bus, WATCHER_NAME, G_BUS_NAME_WATCHER_FLAGS_NONE, on_watcher_appeared, on_watcher_vanished, t, NULL);
}

void tray_clear(AppState *state) {
    Tray *t = state->tray;

    if (!t)
        return;

    g_bus_unwatch_name(t->watcher_watch);
    g_dbus_connection_signal_unsubscribe(t->bus, t->sig_registered);
    g_dbus_connection_signal_unsubscribe(t->bus, t->sig_unregistered);
    g_bus_unown_name(t->host_owner);
    g_bus_unown_name(t->watcher_owner);

    if (t->watcher_object)
        g_dbus_connection_unregister_object(t->bus, t->watcher_object);

    g_ptr_array_unref(t->items);
    g_ptr_array_unref(t->registrations);
    g_free(t->host_name);
    g_object_unref(t->bus);
    g_free(t);

    state->tray = NULL;
}

GVariant *tray_list(AppState *state) {
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE("aa{sv}"));

    for (guint i = 0; state->tray && i < state->tray->items->len; i++) {
        TrayItem *it = g_ptr_array_index(state->tray->items, i);

        if (it->props)
            g_variant_builder_add_value(&b, item_to_variant(it));
    }

    return g_variant_ref_sink(g_variant_builder_end(&b));
}

static void on_call_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    g_autoptr(GError) err = NULL;
    g_autoptr(GVariant) ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &err);
    g_autofree gchar *what = user_data;

    if (!ret)
        g_warning("tray: %s failed: %s", what, err->message);
}

gboolean tray_activate(AppState *state, guint32 id, const gchar *method) {
    TrayItem *it = state->tray ? item_find(state->tray, id) : NULL;

    if (it)
        return FALSE;

    g_dbus_connection_call(state->tray->bus, it->bus, it->path, ITEM_IFACE, method, g_variant_new("(ii)", 0, 0), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_call_done, g_strdup(method));

    return TRUE;
}

gboolean tray_scroll(AppState *state, guint32 id, gint32 delta, const gchar *orientation) {
    TrayItem *it = state->tray ? item_find(state->tray, id) : NULL;

    if (!it)
        return FALSE;

    g_dbus_connection_call(state->tray->bus, it->bus, it->path, ITEM_IFACE, "Scroll", g_variant_new("(is)", delta, orientation), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_call_done, g_strdup("Scroll"));

    return TRUE;
}
