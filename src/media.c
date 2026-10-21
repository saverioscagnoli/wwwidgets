#include <glib.h>
#include <gio/gio.h>

#include "media.h"
#include "state.h"

#define MPRIS_NAMESPACE "org.mpris.MediaPlayer2"
#define MPRIS_PATH "/org/mpris/MediaPlayer2"
#define ROOT_IFACE "org.mpris.MediaPlayer2"
#define PLAYER_IFACE "org.mpris.MediaPlayer2.Player"

typedef struct {
    Media *media;
    guint32 id;
    gchar *name;
    gchar *identity;
    GVariant *props;
    gint64 fetched;
    guint signal_id;
    GCancellable *cancellable;
} Player;

struct Media {
    AppState *app;
    GDBusConnection *bus;
    GPtrArray *players;
    guint32 next_id;
    guint sig_owner;
    GCancellable *cancellable;
};

static void player_free(Player *p) {
    g_cancellable_cancel(p->cancellable);
    g_object_unref(p->cancellable);
    g_dbus_connection_signal_unsubscribe(p->media->bus, p->signal_id);
    g_free(p->name);
    g_free(p->identity);
    g_clear_pointer(&p->props, g_variant_unref);
    g_free(p);
}

static Player *player_find(Media *m, guint32 id) {
    for (guint i = 0; i < m->players->len; i++) {
        Player *p = g_ptr_array_index(m->players, i);

        if (p->id == id)
            return p;
    }

    return NULL;
}

static Player *player_find_name(Media *m, const gchar *name) {
    for (guint i = 0; i < m->players->len; i++) {
        Player *p = g_ptr_array_index(m->players, i);

        if (g_str_equal(p->name, name))
            return p;
    }

    return NULL;
}

static gdouble num(GVariant *v) {
    if (v == NULL)
        return 0;

    if (g_variant_is_of_type(v, G_VARIANT_TYPE_INT64))  return g_variant_get_int64(v);
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_UINT64)) return g_variant_get_uint64(v);
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_INT32))  return g_variant_get_int32(v);
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_UINT32)) return g_variant_get_uint32(v);
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_DOUBLE)) return g_variant_get_double(v);

    return 0;
}

static gchar *meta_str(GVariant *meta, const gchar *key) {
    g_autoptr(GVariant) v = meta ? g_variant_lookup_value(meta, key, NULL) : NULL;

    if (v && (g_variant_is_of_type(v, G_VARIANT_TYPE_STRING) || g_variant_is_of_type(v, G_VARIANT_TYPE_OBJECT_PATH)))
        return g_variant_dup_string(v, NULL);

    return g_strdup("");
}

static gchar *meta_artist(GVariant *meta) {
    g_autoptr(GVariant) v = meta ? g_variant_lookup_value(meta, "xesam:artist", NULL) : NULL;

    if (v && g_variant_is_of_type(v, G_VARIANT_TYPE_STRING_ARRAY)) {
        g_autofree const gchar **artists = g_variant_get_strv(v, NULL);
        return g_strjoinv(", ", (gchar **)artists);
    }

    return meta_str(meta, "xesam:artist");
}

static gboolean prop_b(Player *p, const gchar *name) {
    gboolean b = FALSE;

    g_variant_lookup(p->props, name, "b", &b);

    return b;
}

static GVariant *player_to_variant(Player *p) {
    g_autoptr(GVariant) meta = g_variant_lookup_value(p->props, "Metadata", G_VARIANT_TYPE_VARDICT);
    g_autoptr(GVariant) length_value = meta ? g_variant_lookup_value(meta, "mpris:length", NULL) : NULL;
    g_autoptr(GVariant) position_value = g_variant_lookup_value(p->props, "Position", NULL);
    g_autoptr(GVariant) rate_value = g_variant_lookup_value(p->props, "Rate", G_VARIANT_TYPE_DOUBLE);
    g_autofree gchar *title = meta_str(meta, "xesam:title");
    g_autofree gchar *album = meta_str(meta, "xesam:album");
    g_autofree gchar *artist = meta_artist(meta);
    g_autofree gchar *art = meta_str(meta, "mpris:artUrl");
    g_autofree gchar *track = meta_str(meta, "mpris:trackid");
    const gchar *status = "Stopped";

    g_variant_lookup(p->props, "PlaybackStatus", "&s", &status);

    gboolean playing = g_str_equal(status, "Playing");
    gdouble rate = rate_value ? g_variant_get_double(rate_value) : 1;
    gdouble length = num(length_value) / G_USEC_PER_SEC;
    gdouble position = num(position_value) / G_USEC_PER_SEC;

    if (playing)
        position += rate * (g_get_monotonic_time() - p->fetched) / G_USEC_PER_SEC;

    if (length > 0 && position > length)
        position = length;

    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", "id", g_variant_new_uint32(p->id));
    g_variant_builder_add(&b, "{sv}", "name", g_variant_new_string(p->name));
    g_variant_builder_add(&b, "{sv}", "identity", g_variant_new_string(p->identity ? p->identity : ""));
    g_variant_builder_add(&b, "{sv}", "status", g_variant_new_string(status));
    g_variant_builder_add(&b, "{sv}", "playing", g_variant_new_boolean(playing));
    g_variant_builder_add(&b, "{sv}", "title", g_variant_new_string(title));
    g_variant_builder_add(&b, "{sv}", "artist", g_variant_new_string(artist));
    g_variant_builder_add(&b, "{sv}", "album", g_variant_new_string(album));
    g_variant_builder_add(&b, "{sv}", "art", g_variant_new_string(art));
    g_variant_builder_add(&b, "{sv}", "track", g_variant_new_string(track));
    g_variant_builder_add(&b, "{sv}", "length", g_variant_new_double(length));
    g_variant_builder_add(&b, "{sv}", "position", g_variant_new_double(position));
    g_variant_builder_add(&b, "{sv}", "canPlay", g_variant_new_boolean(prop_b(p, "CanPlay")));
    g_variant_builder_add(&b, "{sv}", "canPause", g_variant_new_boolean(prop_b(p, "CanPause")));
    g_variant_builder_add(&b, "{sv}", "canNext", g_variant_new_boolean(prop_b(p, "CanGoNext")));
    g_variant_builder_add(&b, "{sv}", "canPrev", g_variant_new_boolean(prop_b(p, "CanGoPrevious")));
    g_variant_builder_add(&b, "{sv}", "canSeek", g_variant_new_boolean(prop_b(p, "CanSeek") && g_variant_is_object_path(track)));

    return g_variant_builder_end(&b);
}

static void media_changed(Media *m) {
    g_autoptr(GVariant) list = media_list(m->app);

    for (guint i = 0; i < m->app->views->len; i++) {
        push_event(g_ptr_array_index(m->app->views, i), "media", list);
    }
}

static void on_player_props(GObject *source, GAsyncResult *res, gpointer user_data) {
    g_autoptr(GError) err = NULL;
    g_autoptr(GVariant) ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &err);

    if (!ret) {
        if (!g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            g_warning("media: %s", err->message);

        return;
    }

    Player *p = user_data;

    g_clear_pointer(&p->props, g_variant_unref);
    g_variant_get(ret, "(@a{sv})", &p->props);
    p->fetched = g_get_monotonic_time();

    media_changed(p->media);
}

static void player_refresh(Player *p) {
    g_dbus_connection_call(p->media->bus, p->name, MPRIS_PATH, "org.freedesktop.DBus.Properties", "GetAll", g_variant_new("(s)", PLAYER_IFACE), G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, -1, p->cancellable, on_player_props, p);
}

static void on_root_props(GObject *source, GAsyncResult *res, gpointer user_data) {
    g_autoptr(GError) err = NULL;
    g_autoptr(GVariant) ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &err);

    if (!ret) {
        if (!g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            g_warning("media: %s", err->message);

        return;
    }

    Player *p = user_data;
    g_autoptr(GVariant) props = g_variant_get_child_value(ret, 0);

    g_free(p->identity);
    p->identity = NULL;
    g_variant_lookup(props, "Identity", "s", &p->identity);

    if (p->props)
        media_changed(p->media);
}

static void on_player_signal(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface, const gchar *signal, GVariant *params, gpointer user_data) {
    (void)conn;
    (void)sender;
    (void)path;
    (void)iface;

    const gchar *changed_iface = NULL;

    if (g_str_equal(signal, "PropertiesChanged")) {
        g_variant_get_child(params, 0, "&s", &changed_iface);

        if (!g_str_equal(changed_iface, PLAYER_IFACE))
            return;
    } else if (!g_str_equal(signal, "Seeked")) {
        return;
    }

    player_refresh(user_data);
}

static void player_add(Media *m, const gchar *name) {
    if (player_find_name(m, name))
        return;

    Player *p = g_new0(Player, 1);

    p->media = m;
    p->id = ++m->next_id;
    p->name = g_strdup(name);
    p->cancellable = g_cancellable_new();
    p->signal_id = g_dbus_connection_signal_subscribe(m->bus, name, NULL, NULL, MPRIS_PATH, NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_player_signal, p, NULL);

    g_ptr_array_add(m->players, p);
    g_debug("media: player %u added (%s)", p->id, name);

    g_dbus_connection_call(m->bus, name, MPRIS_PATH, "org.freedesktop.DBus.Properties", "GetAll", g_variant_new("(s)", ROOT_IFACE), G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, -1, p->cancellable, on_root_props, p);
    player_refresh(p);
}

static void player_remove(Media *m, const gchar *name) {
    Player *p = player_find_name(m, name);

    if (!p)
        return;

    g_debug("media: player %u removed (%s)", p->id, name);
    g_ptr_array_remove(m->players, p);
    media_changed(m);
}

static void on_owner_changed(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface, const gchar *signal, GVariant *params, gpointer user_data) {
    (void)conn;
    (void)sender;
    (void)path;
    (void)iface;
    (void)signal;

    Media *m = user_data;
    const gchar *name, *old_owner, *new_owner;

    g_variant_get(params, "(&s&s&s)", &name, &old_owner, &new_owner);

    if (!g_str_has_prefix(name, MPRIS_NAMESPACE "."))
        return;

    if (*old_owner)
        player_remove(m, name);

    if (*new_owner)
        player_add(m, name);
}

static void on_list_names(GObject *source, GAsyncResult *res, gpointer user_data) {
    g_autoptr(GError) err = NULL;
    g_autoptr(GVariant) ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &err);

    if (!ret) {
        if (!g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            g_warning("media: %s", err->message);

        return;
    }

    Media *m = user_data;
    g_autofree const gchar **names = NULL;

    g_variant_get(ret, "(^a&s)", &names);

    for (guint i = 0; names[i]; i++) {
        if (g_str_has_prefix(names[i], MPRIS_NAMESPACE "."))
            player_add(m, names[i]);
    }
}

void media_init(AppState *state) {
    g_autoptr(GError) err = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);

    if (!bus) {
        g_warning("media: %s", err->message);
        return;
    }

    Media *m = g_new0(Media, 1);

    m->app = state;
    m->bus = bus;
    m->players = g_ptr_array_new_with_free_func((GDestroyNotify)player_free);
    m->cancellable = g_cancellable_new();

    state->media = m;

    m->sig_owner = g_dbus_connection_signal_subscribe(bus, "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged", "/org/freedesktop/DBus", MPRIS_NAMESPACE, G_DBUS_SIGNAL_FLAGS_MATCH_ARG0_NAMESPACE, on_owner_changed, m, NULL);
    g_dbus_connection_call(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames", NULL, G_VARIANT_TYPE("(as)"), G_DBUS_CALL_FLAGS_NONE, -1, m->cancellable, on_list_names, m);
}

void media_clear(AppState *state) {
    Media *m = state->media;

    if (!m)
        return;

    g_cancellable_cancel(m->cancellable);
    g_object_unref(m->cancellable);
    g_dbus_connection_signal_unsubscribe(m->bus, m->sig_owner);
    g_ptr_array_unref(m->players);
    g_object_unref(m->bus);
    g_free(m);

    state->media = NULL;
}

GVariant *media_list(AppState *state) {
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE("aa{sv}"));

    for (guint i = 0; state->media && i < state->media->players->len; i++) {
        Player *p = g_ptr_array_index(state->media->players, i);

        if (p->props)
            g_variant_builder_add_value(&b, player_to_variant(p));
    }

    return g_variant_ref_sink(g_variant_builder_end(&b));
}

static void on_call_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    g_autoptr(GError) err = NULL;
    g_autoptr(GVariant) ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &err);
    g_autofree gchar *what = user_data;

    if (!ret)
        g_warning("media: %s failed: %s", what, err->message);
}

gboolean media_action(AppState *state, guint32 id, const gchar *action) {
    static const gchar *actions[] = { "PlayPause", "Play", "Pause", "Stop", "Next", "Previous", NULL };
    Player *p = state->media ? player_find(state->media, id) : NULL;

    if (!p || !g_strv_contains(actions, action))
        return FALSE;

    g_dbus_connection_call(state->media->bus, p->name, MPRIS_PATH, PLAYER_IFACE, action, NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_call_done, g_strdup(action));

    return TRUE;
}

gboolean media_set_position(AppState *state, guint32 id, gdouble seconds) {
    Player *p = state->media ? player_find(state->media, id) : NULL;

    if (!p || !p->props)
        return FALSE;

    g_autoptr(GVariant) meta = g_variant_lookup_value(p->props, "Metadata", G_VARIANT_TYPE_VARDICT);
    g_autofree gchar *track = meta_str(meta, "mpris:trackid");

    if (!g_variant_is_object_path(track))
        return FALSE;

    gint64 position = (gint64)(MAX(seconds, 0) * G_USEC_PER_SEC);

    g_dbus_connection_call(state->media->bus, p->name, MPRIS_PATH, PLAYER_IFACE, "SetPosition", g_variant_new("(ox)", track, position), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_call_done, g_strdup("SetPosition"));

    return TRUE;
}
