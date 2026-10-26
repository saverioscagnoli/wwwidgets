#include <gdk/gdk.h>
#include <gtk4-layer-shell.h>
#include <glib.h>
#include <jsc/jsc.h>
#include <json-glib/json-glib.h>

#include "state.h"
#include "store.h"
#include "audio.h"
#include "battery.h"
#include "bluetooth.h"
#include "clipboard.h"
#include "image.h"
#include "media.h"
#include "network.h"
#include "process.h"
#include "reload.h"
#include "notifications.h"
#include "toplevel.h"
#include "tray.h"
#include "workspace.h"

static const char *SHIM =
    "(() => {"
    "  const call = (cmd, args = {}) =>"
    "    window.webkit.messageHandlers.native.postMessage({ cmd, ...args });"
    "  const on = (name, cb) => {"
    "    const h = (e) => cb(e.detail);"
    "    window.addEventListener(`wwwidgets:${name}`, h);"
    "    return () => window.removeEventListener(`wwwidgets:${name}`, h);"
    "  };"
    "  const monitor = new URLSearchParams(location.search).get('monitor') || '';"
    "  const win = `${location.host}|${monitor}`;"
    "  window.wwwidgets = {"
    "    monitor,"
    "    window: {"
    "      setInputRegion: (rects) => call('window.setInputRegion', { win, rects: rects == null ? 'null' : JSON.stringify(rects) }),"
    "      setKeyboard: (mode) => call('window.setKeyboard', { win, mode }),"
    "    },"
    "    state: {"
    "      get: (key) => call('state.get', { key }),"
    "      set: (key, value) => call('state.set', { key, value: JSON.stringify(value ?? null) }),"
    "      toggle: (key) => call('state.toggle', { key }),"
    "      onChange: (key, cb) => on('state', (d) => d.key === key && cb(d.value)),"
    "    },"
    "    workspace: {"
    "      list: () => call('workspace.list'),"
    "      get: () => call('workspace.get'),"
    "      set: (id) => call('workspace.set', { id }),"
    "      onChange: (cb) => on('workspace', cb),"
    "    },"
    "    notifications: {"
    "      list: () => call('notifications.list'),"
    "      close: (id) => call('notifications.close', { id }),"
    "      invoke: (id, key) => call('notifications.invoke', { id, key }),"
    "      onChange: (cb) => on('notifications', cb),"
    "    },"
    "    tray: {"
    "      list: () => call('tray.list'),"
    "      activate: (id) => call('tray.activate', { id }),"
    "      secondaryActivate: (id) => call('tray.secondaryActivate', { id }),"
    "      contextMenu: (id) => call('tray.contextMenu', { id }),"
    "      scroll: (id, delta, orientation = 'vertical') => call('tray.scroll', { id, delta, orientation }),"
    "      menu: (id) => call('tray.menu', { id }),"
    "      menuClick: (id, item) => call('tray.menuClick', { id, item }),"
    "      onChange: (cb) => on('tray', cb),"
    "    },"
    "    audio: {"
    "      get: () => call('audio.get'),"
    "      setVolume: (device, volume) => call('audio.setVolume', { device, volume }),"
    "      setMute: (device, muted) => call('audio.setMute', { device, muted }),"
    "      setDefault: (kind, name) => call('audio.setDefault', { kind, name }),"
    "      setDeviceVolume: (kind, name, volume) => call('audio.setDeviceVolume', { kind, name, volume }),"
    "      setDeviceMute: (kind, name, muted) => call('audio.setDeviceMute', { kind, name, muted }),"
    "      setStreamVolume: (kind, index, volume) => call('audio.setStreamVolume', { kind, index, volume }),"
    "      setStreamMute: (kind, index, muted) => call('audio.setStreamMute', { kind, index, muted }),"
    "      moveStream: (kind, index, device) => call('audio.moveStream', { kind, index, device }),"
    "      onChange: (cb) => on('audio', cb),"
    "    },"
    "    bluetooth: {"
    "      get: () => call('bluetooth.get'),"
    "      setPowered: (powered) => call('bluetooth.setPowered', { powered }),"
    "      scan: (on = true) => call('bluetooth.scan', { on }),"
    "      connect: (path) => call('bluetooth.connect', { path }),"
    "      disconnect: (path) => call('bluetooth.disconnect', { path }),"
    "      onChange: (cb) => on('bluetooth', cb),"
    "      pair: (path) => call('bluetooth.pair', { path }),"
    "      reply: (value) => call('bluetooth.reply', { accept: true, value: String(value ?? '') }),"
    "      reject: () => call('bluetooth.reply', { accept: false, value: '' }),"
    "      onRequest: (cb) => on('bluetooth.request', cb),"
    "    },"
    "    network: {"
    "      get: () => call('network.get'),"
    "      setWifi: (enabled) => call('network.setWifi', { enabled }),"
    "      scan: () => call('network.scan'),"
    "      connect: (path, password) => call('network.connect', { path, password: password ?? null }),"
    "      disconnect: () => call('network.disconnect'),"
    "      onChange: (cb) => on('network', cb),"
    "    },"
    "    battery: {"
    "      get: () => call('battery.get'),"
    "      onChange: (cb) => on('battery', cb),"
    "    },"
    "    toplevels: {"
    "      list: () => call('toplevels.list'),"
    "      activate: (id) => call('toplevels.action', { id, action: 'activate' }),"
    "      close: (id) => call('toplevels.action', { id, action: 'close' }),"
    "      setMinimized: (id, on) => call('toplevels.action', { id, action: on ? 'minimize' : 'unminimize' }),"
    "      onChange: (cb) => on('toplevels', cb),"
    "    },"
    "    clipboard: {"
    "      history: () => call('clipboard.history'),"
    "      set: (text) => call('clipboard.set', { text: String(text) }),"
    "      copy: (id) => call('clipboard.entry', { id, action: 'copy' }),"
    "      remove: (id) => call('clipboard.entry', { id, action: 'remove' }),"
    "      clear: () => call('clipboard.clear'),"
    "      onChange: (cb) => on('clipboard', cb),"
    "    },"
    "    media: {"
    "      list: () => call('media.list'),"
    "      playPause: (id) => call('media.action', { id, action: 'PlayPause' }),"
    "      play: (id) => call('media.action', { id, action: 'Play' }),"
    "      pause: (id) => call('media.action', { id, action: 'Pause' }),"
    "      stop: (id) => call('media.action', { id, action: 'Stop' }),"
    "      next: (id) => call('media.action', { id, action: 'Next' }),"
    "      previous: (id) => call('media.action', { id, action: 'Previous' }),"
    "      setPosition: (id, position) => call('media.setPosition', { id, position }),"
    "      onChange: (cb) => on('media', cb),"
    "    },"
    "    exec: (command, args = []) => call('exec', { argv: [command, ...args] }),"
    "    spawn: async (command, args = [], { onStdout, onStderr, onExit } = {}) => {"
    "      const id = await call('spawn', { argv: [command, ...args] });"
    "      const off = on('process', (d) => {"
    "        if (d.id !== id) return;"
    "        if (d.type === 'stdout') onStdout?.(d.data);"
    "        else if (d.type === 'stderr') onStderr?.(d.data);"
    "        else { off(); onExit?.(d.data); }"
    "      });"
    "      return {"
    "        id,"
    "        write: (data) => call('process.write', { id, data: String(data) }),"
    "        kill: (signal = 15) => call('process.kill', { id, signal }),"
    "      };"
    "    },"
    "  };"
    "})();";

static void reply_variant(WebKitScriptMessageReply *reply, JSCContext *ctx, GVariant *v) {
    g_autofree gchar *json = json_gvariant_serialize_data(v, NULL);
    g_autoptr(JSCValue) result = jsc_value_new_from_json(ctx, json);
    webkit_script_message_reply_return_value(reply, result);
}

typedef struct {
    WebKitScriptMessageReply *reply;
    JSCContext *ctx;
} PendingReply;

static void on_tray_menu(GVariant *menu, const gchar *error, gpointer user_data) {
    PendingReply *p = user_data;

    if (menu)
        reply_variant(p->reply, p->ctx, menu);
    else
        webkit_script_message_reply_return_error_message(p->reply, error);

    webkit_script_message_reply_unref(p->reply);
    g_object_unref(p->ctx);
    g_free(p);
}

static void on_pending_reply(const gchar *error, gpointer user_data) {
    PendingReply *p = user_data;

    if (error) {
        webkit_script_message_reply_return_error_message(p->reply, error);
    } else {
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(p->ctx, TRUE);
        webkit_script_message_reply_return_value(p->reply, ok);
    }

    webkit_script_message_reply_unref(p->reply);
    g_object_unref(p->ctx);
    g_free(p);
}

void push_event_json(WebKitWebView *view, const gchar *name, const gchar *json) {
    g_debug("dispatching event: %s, detail: %s", name, json);

    g_autofree gchar *ev = g_strconcat("wwwidgets:", name, NULL);

    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", "name", g_variant_new_string(ev));
    g_variant_builder_add(&b, "{sv}", "detail", g_variant_new_string(json));

    webkit_web_view_call_async_javascript_function(view, "window.dispatchEvent(new CustomEvent(name, { detail: JSON.parse(detail) }));", -1, g_variant_builder_end(&b), NULL, NULL, NULL, NULL, NULL);
}

void push_event(WebKitWebView *view, const gchar *name, GVariant *detail) {
    g_autofree gchar *json = json_gvariant_serialize_data(detail, NULL);
    push_event_json(view, name, json);
}

static GtkWindow *find_window(AppState *state, const gchar *win) {
    for (guint i = 0; i < state->windows->len; i++) {
        GtkWindow *window = g_ptr_array_index(state->windows, i);

        if (g_strcmp0(g_object_get_data(G_OBJECT(window), "win"), win) == 0)
            return window;
    }

    return NULL;
}

static GStrv jsc_to_strv(JSCValue *array) {
    if (!jsc_value_is_array(array))
        return NULL;

    g_autoptr(JSCValue) len_value = jsc_value_object_get_property(array, "length");
    gint32 len = jsc_value_to_int32(len_value);
    GStrv strv = g_new0(gchar *, len + 1);

    for (gint32 i = 0; i < len; i++) {
        g_autoptr(JSCValue) item = jsc_value_object_get_property_at_index(array, i);
        strv[i] = jsc_value_to_string(item);
    }

    return strv;
}

gboolean window_set_input_region(GtkWindow *window, const gchar *json) {
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
    g_autoptr(JsonNode) node = json_from_string(json, NULL);

    if (surface == NULL || node == NULL)
        return FALSE;

    if (JSON_NODE_HOLDS_NULL(node)) {
        gdk_surface_set_input_region(surface, NULL);
        return TRUE;
    }

    if (!JSON_NODE_HOLDS_ARRAY(node))
        return FALSE;

    JsonArray *array = json_node_get_array(node);
    cairo_region_t *region = cairo_region_create();

    for (guint i = 0; i < json_array_get_length(array); i++) {
        JsonObject *r = json_array_get_object_element(array, i);

        if (r == NULL) {
            cairo_region_destroy(region);
            return FALSE;
        }

        cairo_rectangle_int_t rect = {
            (int)json_object_get_double_member_with_default(r, "x", 0),
            (int)json_object_get_double_member_with_default(r, "y", 0),
            (int)json_object_get_double_member_with_default(r, "width", 0),
            (int)json_object_get_double_member_with_default(r, "height", 0),
        };

        cairo_region_union_rectangle(region, &rect);
    }

    gdk_surface_set_input_region(surface, region);
    cairo_region_destroy(region);
    return TRUE;
}

static gboolean on_native_message(WebKitUserContentManager *manager, JSCValue *message, WebKitScriptMessageReply *reply, gpointer user_data) {
    (void)manager;

    AppState *state = user_data;
    JSCContext *ctx = jsc_value_get_context(message);

    g_autoptr(JSCValue) cmd_value = jsc_value_object_get_property(message, "cmd");
    g_autofree gchar *cmd = jsc_value_to_string(cmd_value);

    if (g_str_equal(cmd, "state.get")) {
       g_autoptr(JSCValue) key_value = jsc_value_object_get_property(message, "key");
       g_autofree gchar *key = jsc_value_to_string(key_value);
       const gchar *json = store_get(state, key);
       g_autoptr(JSCValue) result = jsc_value_new_from_json(ctx, json ? json : "null");

       webkit_script_message_reply_return_value(reply, result);
    } else if (g_str_equal(cmd, "state.set")) {
        g_autoptr(JSCValue) key_value = jsc_value_object_get_property(message, "key");
        g_autoptr(JSCValue) value_value = jsc_value_object_get_property(message, "value");
        g_autofree gchar *key = jsc_value_to_string(key_value);
        g_autofree gchar *value = jsc_value_to_string(value_value);
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);

        if (store_set(state, key, value))
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "value is not serializable");
    } else if (g_str_equal(cmd, "state.toggle")) {
        g_autoptr(JSCValue) key_value = jsc_value_object_get_property(message, "key");
        g_autofree gchar *key = jsc_value_to_string(key_value);
        gboolean on = g_strcmp0(store_get(state, key), "true") != 0;
        g_autoptr(JSCValue) result = jsc_value_new_boolean(ctx, on);

        store_set(state, key, on ? "true" : "false");
        webkit_script_message_reply_return_value(reply, result);
    } else if (g_str_equal(cmd, "workspace.list")) {
        g_autoptr(GVariant) list = workspace_list(state);
        reply_variant(reply, ctx, list);
    } else if (g_str_equal(cmd, "workspace.get")) {
        g_autoptr(GVariant) ws = workspace_active(state);
        g_autoptr(JSCValue) null = jsc_value_new_null(ctx);

        if (ws)
            reply_variant(reply, ctx, ws);
        else
            webkit_script_message_reply_return_value(reply, null);
    } else if (g_str_equal(cmd, "workspace.set")) {
      g_autoptr(JSCValue) id_value = jsc_value_object_get_property(message, "id");
      g_autofree gchar *id = jsc_value_to_string(id_value);
      g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);

      if (workspace_activate(state, id))
          webkit_script_message_reply_return_value(reply, ok);
      else
          webkit_script_message_reply_return_error_message(reply, "no such workspace or not activable");
    } else if (g_str_equal(cmd, "notifications.list")) {
        g_autoptr(GVariant) list = notifications_list(state);
        reply_variant(reply, ctx, list);
    } else if (g_str_equal(cmd, "notifications.close")) {
        g_autoptr(JSCValue) id_value = jsc_value_object_get_property(message, "id");
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);

        if (notifications_close(state, (guint32)jsc_value_to_int32(id_value)))
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "no such notification");
    } else if (g_str_equal(cmd, "notifications.invoke")) {
        g_autoptr(JSCValue) id_value = jsc_value_object_get_property(message, "id");
        g_autoptr(JSCValue) key_value = jsc_value_object_get_property(message, "key");
        g_autofree gchar *key = jsc_value_to_string(key_value);
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);

        if (notifications_invoke(state, (guint32)jsc_value_to_int32(id_value), key))
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "no such notification or action");

    } else if (g_str_equal(cmd, "battery.get")) {
        if (!state->upower)
            return webkit_script_message_reply_return_error_message(reply, "no battery"), TRUE;

        g_autoptr(GVariant) snapshot = battery_snapshot(state->upower);
        reply_variant(reply, ctx, snapshot);
    } else if (g_str_equal(cmd, "tray.list")) {
        g_autoptr(GVariant) list = tray_list(state);
        reply_variant(reply, ctx, list);
    } else if (g_str_equal(cmd, "tray.activate") || g_str_equal(cmd, "tray.secondaryActivate") || g_str_equal(cmd, "tray.contextMenu")) {
        const gchar *method = g_str_equal(cmd, "tray.activate") ? "Activate" : g_str_equal(cmd, "tray.secondaryActivate") ? "SecondaryActivate" : "ContextMenu";
        g_autoptr(JSCValue) id_value = jsc_value_object_get_property(message, "id");
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);

        if (tray_activate(state, (guint32)jsc_value_to_int32(id_value), method))
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "no such tray item");
    } else if (g_str_equal(cmd, "tray.scroll")) {
        g_autoptr(JSCValue) id_value = jsc_value_object_get_property(message, "id");
        g_autoptr(JSCValue) delta_value = jsc_value_object_get_property(message, "delta");
        g_autoptr(JSCValue) orientation_value = jsc_value_object_get_property(message, "orientation");
        g_autofree gchar *orientation = jsc_value_to_string(orientation_value);
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);

        if (tray_scroll(state, (guint32)jsc_value_to_int32(id_value), jsc_value_to_int32(delta_value), orientation))
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "no such tray item");
    } else if (g_str_equal(cmd, "tray.menu")) {
        g_autoptr(JSCValue) id_value = jsc_value_object_get_property(message, "id");
        PendingReply *p = g_new0(PendingReply, 1);

        p->reply = webkit_script_message_reply_ref(reply);
        p->ctx = g_object_ref(ctx);

        if (!tray_menu(state, (guint32)jsc_value_to_int32(id_value), on_tray_menu, p))
            on_tray_menu(NULL, "no such tray item or it has no menu", p);
    } else if (g_str_equal(cmd, "tray.menuClick")) {
        g_autoptr(JSCValue) id_value = jsc_value_object_get_property(message, "id");
        g_autoptr(JSCValue) item_value = jsc_value_object_get_property(message, "item");
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);

        if (tray_menu_click(state, (guint32)jsc_value_to_int32(id_value), jsc_value_to_int32(item_value)))
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "no such tray item or it has no menu");
    } else if (g_str_equal(cmd, "audio.get")) {
        if (!state->audio)
            return webkit_script_message_reply_return_error_message(reply, "audio is disabled"), TRUE;

        g_autoptr(GVariant) snapshot = audio_snapshot(state);
        reply_variant(reply, ctx, snapshot);
    } else if (g_str_equal(cmd, "audio.setVolume")) {
        g_autoptr(JSCValue) device_value = jsc_value_object_get_property(message, "device");
        g_autoptr(JSCValue) volume_value = jsc_value_object_get_property(message, "volume");
        g_autofree gchar *device = jsc_value_to_string(device_value);
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);

        if (audio_set_volume(state, device, jsc_value_to_double(volume_value)))
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "expected sink or source");
    } else if (g_str_equal(cmd, "audio.setMute")) {
        g_autoptr(JSCValue) device_value = jsc_value_object_get_property(message, "device");
        g_autoptr(JSCValue) muted_value = jsc_value_object_get_property(message, "muted");
        g_autofree gchar *device = jsc_value_to_string(device_value);
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);

        if (audio_set_mute(state, device, jsc_value_to_boolean(muted_value)))
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "expected sink or source");
    } else if (g_str_equal(cmd, "audio.setDefault") || g_str_equal(cmd, "audio.setDeviceVolume") || g_str_equal(cmd, "audio.setDeviceMute")) {
        g_autoptr(JSCValue) kind_value = jsc_value_object_get_property(message, "kind");
        g_autoptr(JSCValue) name_value = jsc_value_object_get_property(message, "name");
        g_autofree gchar *kind = jsc_value_to_string(kind_value);
        g_autofree gchar *name = jsc_value_is_string(name_value) ? jsc_value_to_string(name_value) : NULL;
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);
        gboolean done;

        if (g_str_equal(cmd, "audio.setDefault")) {
            done = audio_set_default(state, kind, name);
        } else if (g_str_equal(cmd, "audio.setDeviceVolume")) {
            g_autoptr(JSCValue) volume_value = jsc_value_object_get_property(message, "volume");
            done = audio_set_device_volume(state, kind, name, jsc_value_to_double(volume_value));
        } else {
            g_autoptr(JSCValue) muted_value = jsc_value_object_get_property(message, "muted");
            done = audio_set_device_mute(state, kind, name, jsc_value_to_boolean(muted_value));
        }

        if (done)
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "no such device");
    } else if (g_str_equal(cmd, "audio.setStreamVolume") || g_str_equal(cmd, "audio.setStreamMute") || g_str_equal(cmd, "audio.moveStream")) {
        g_autoptr(JSCValue) kind_value = jsc_value_object_get_property(message, "kind");
        g_autoptr(JSCValue) index_value = jsc_value_object_get_property(message, "index");
        g_autofree gchar *kind = jsc_value_to_string(kind_value);
        guint32 index = (guint32)jsc_value_to_double(index_value);
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);
        gboolean done;

        if (g_str_equal(cmd, "audio.setStreamVolume")) {
            g_autoptr(JSCValue) volume_value = jsc_value_object_get_property(message, "volume");
            done = audio_set_stream_volume(state, kind, index, jsc_value_to_double(volume_value));
        } else if (g_str_equal(cmd, "audio.setStreamMute")) {
            g_autoptr(JSCValue) muted_value = jsc_value_object_get_property(message, "muted");
            done = audio_set_stream_mute(state, kind, index, jsc_value_to_boolean(muted_value));
        } else {
            g_autoptr(JSCValue) device_value = jsc_value_object_get_property(message, "device");
            g_autofree gchar *device = jsc_value_to_string(device_value);
            done = audio_move_stream(state, kind, index, device);
        }

        if (done)
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "no such stream or device");
    } else if (g_str_equal(cmd, "bluetooth.get")) {
        if (!state->bluez)
            return webkit_script_message_reply_return_error_message(reply, "bluetooth is disabled"), TRUE;

        g_autoptr(GVariant) snapshot = bluetooth_snapshot(state);
        reply_variant(reply, ctx, snapshot);
    } else if (g_str_equal(cmd, "bluetooth.setPowered") || g_str_equal(cmd, "bluetooth.scan")) {
        gboolean powered = g_str_equal(cmd, "bluetooth.setPowered");
        g_autoptr(JSCValue) on_value = jsc_value_object_get_property(message, powered ? "powered" : "on");
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);
        gboolean on = jsc_value_to_boolean(on_value);

        if (state->bluez && (powered ? bluetooth_set_powered(state, on) : bluetooth_scan(state, on)))
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "no bluetooth adapter");
    } else if (g_str_equal(cmd, "bluetooth.connect") || g_str_equal(cmd, "bluetooth.disconnect")) {
        g_autoptr(JSCValue) path_value = jsc_value_object_get_property(message, "path");
        g_autofree gchar *path = jsc_value_to_string(path_value);
        PendingReply *p = g_new0(PendingReply, 1);

        p->reply = webkit_script_message_reply_ref(reply);
        p->ctx = g_object_ref(ctx);

        if (!state->bluez || !bluetooth_connect(state, path, g_str_equal(cmd, "bluetooth.connect"), on_pending_reply, p))
            on_pending_reply("no such device", p);
    } else if (g_str_equal(cmd, "bluetooth.pair")) {
        g_autoptr(JSCValue) path_value = jsc_value_object_get_property(message, "path");
        g_autofree gchar *path = jsc_value_to_string(path_value);
        PendingReply *p = g_new0(PendingReply, 1);

        p->reply = webkit_script_message_reply_ref(reply);
        p->ctx = g_object_ref(ctx);

        if (!state->bluez || !bluetooth_pair(state, path, on_pending_reply, p))
            on_pending_reply("no such device", p);
    } else if (g_str_equal(cmd, "bluetooth.reply")) {
        g_autoptr(JSCValue) accept_value = jsc_value_object_get_property(message, "accept");
        g_autoptr(JSCValue) value_value = jsc_value_object_get_property(message, "value");
        g_autofree gchar *value = jsc_value_to_string(value_value);
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);

        if (bluetooth_reply(state, jsc_value_to_boolean(accept_value), value))
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "no pending request");
    } else if (g_str_equal(cmd, "network.get")) {
        if (!state->network)
            return webkit_script_message_reply_return_error_message(reply, "network is disabled"), TRUE;

        g_autoptr(GVariant) snapshot = network_snapshot(state);
        reply_variant(reply, ctx, snapshot);
    } else if (g_str_equal(cmd, "network.setWifi")) {
        g_autoptr(JSCValue) enabled_value = jsc_value_object_get_property(message, "enabled");
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);

        if (state->network && network_set_wifi(state, jsc_value_to_boolean(enabled_value)))
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "network is disabled");
    } else if (g_str_equal(cmd, "network.scan") || g_str_equal(cmd, "network.disconnect")) {
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);

        if (state->network && (g_str_equal(cmd, "network.scan") ? network_scan(state) : network_disconnect(state)))
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "no wifi device");
    } else if (g_str_equal(cmd, "network.connect")) {
        g_autoptr(JSCValue) path_value = jsc_value_object_get_property(message, "path");
        g_autoptr(JSCValue) password_value = jsc_value_object_get_property(message, "password");
        g_autofree gchar *path = jsc_value_to_string(path_value);
        g_autofree gchar *password = jsc_value_is_string(password_value) ? jsc_value_to_string(password_value) : NULL;
        PendingReply *p = g_new0(PendingReply, 1);

        p->reply = webkit_script_message_reply_ref(reply);
        p->ctx = g_object_ref(ctx);

        if (!state->network || !network_connect(state, path, password, on_pending_reply, p))
            on_pending_reply("no wifi device or access point", p);
    } else if (g_str_equal(cmd, "exec")) {
        g_autoptr(JSCValue) argv_value = jsc_value_object_get_property(message, "argv");
        g_auto(GStrv) argv = jsc_to_strv(argv_value);
        PendingReply *p = g_new0(PendingReply, 1);

        p->reply = webkit_script_message_reply_ref(reply);
        p->ctx = g_object_ref(ctx);

        if (!state->processes || !argv || !argv[0])
            on_tray_menu(NULL, "exec is disabled or no command given", p);
        else
            process_exec(state, (const gchar *const *)argv, on_tray_menu, p);
    } else if (g_str_equal(cmd, "spawn")) {
        g_autoptr(JSCValue) argv_value = jsc_value_object_get_property(message, "argv");
        g_auto(GStrv) argv = jsc_to_strv(argv_value);
        g_autoptr(GError) err = NULL;
        guint32 id = state->processes && argv && argv[0] ? process_spawn(state, (const gchar *const *)argv, &err) : 0;

        if (id) {
            g_autoptr(JSCValue) result = jsc_value_new_number(ctx, id);
            webkit_script_message_reply_return_value(reply, result);
        } else {
            webkit_script_message_reply_return_error_message(reply, err ? err->message : "exec is disabled or no command given");
        }
    } else if (g_str_equal(cmd, "process.write")) {
        g_autoptr(JSCValue) id_value = jsc_value_object_get_property(message, "id");
        g_autoptr(JSCValue) data_value = jsc_value_object_get_property(message, "data");
        g_autofree gchar *data = jsc_value_to_string(data_value);
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);

        if (state->processes && process_write(state, (guint32)jsc_value_to_int32(id_value), data))
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "no such process");
    } else if (g_str_equal(cmd, "process.kill")) {
        g_autoptr(JSCValue) id_value = jsc_value_object_get_property(message, "id");
        g_autoptr(JSCValue) signal_value = jsc_value_object_get_property(message, "signal");
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);

        if (state->processes && process_kill(state, (guint32)jsc_value_to_int32(id_value), jsc_value_to_int32(signal_value)))
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "no such process");
    } else if (g_str_equal(cmd, "toplevels.list")) {
        g_autoptr(GVariant) list = toplevel_list(state);
        reply_variant(reply, ctx, list);
    } else if (g_str_equal(cmd, "toplevels.action")) {
        g_autoptr(JSCValue) id_value = jsc_value_object_get_property(message, "id");
        g_autoptr(JSCValue) action_value = jsc_value_object_get_property(message, "action");
        g_autofree gchar *action = jsc_value_to_string(action_value);
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);

        if (toplevel_action(state, (guint32)jsc_value_to_int32(id_value), action))
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "no such window or action");
    } else if (g_str_equal(cmd, "media.list")) {
        g_autoptr(GVariant) list = media_list(state);
        reply_variant(reply, ctx, list);
    } else if (g_str_equal(cmd, "media.action")) {
        g_autoptr(JSCValue) id_value = jsc_value_object_get_property(message, "id");
        g_autoptr(JSCValue) action_value = jsc_value_object_get_property(message, "action");
        g_autofree gchar *action = jsc_value_to_string(action_value);
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);

        if (media_action(state, (guint32)jsc_value_to_double(id_value), action))
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "no such player or action");
    } else if (g_str_equal(cmd, "media.setPosition")) {
        g_autoptr(JSCValue) id_value = jsc_value_object_get_property(message, "id");
        g_autoptr(JSCValue) position_value = jsc_value_object_get_property(message, "position");
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);

        if (media_set_position(state, (guint32)jsc_value_to_double(id_value), jsc_value_to_double(position_value)))
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "no such player or track is not seekable");
    } else if (g_str_equal(cmd, "clipboard.history")) {
        g_autoptr(GVariant) list = clipboard_history(state);
        reply_variant(reply, ctx, list);
    } else if (g_str_equal(cmd, "clipboard.set")) {
        g_autoptr(JSCValue) text_value = jsc_value_object_get_property(message, "text");
        g_autofree gchar *text = jsc_value_to_string(text_value);
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);

        if (clipboard_set(state, text))
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "clipboard is unavailable");
    } else if (g_str_equal(cmd, "clipboard.entry")) {
        g_autoptr(JSCValue) id_value = jsc_value_object_get_property(message, "id");
        g_autoptr(JSCValue) action_value = jsc_value_object_get_property(message, "action");
        g_autofree gchar *action = jsc_value_to_string(action_value);
        guint32 id = (guint32)jsc_value_to_double(id_value);
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);
        gboolean done = g_str_equal(action, "copy") ? clipboard_copy(state, id) : clipboard_remove(state, id);

        if (done)
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "no such entry");
    } else if (g_str_equal(cmd, "clipboard.clear")) {
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);

        if (clipboard_wipe(state))
            webkit_script_message_reply_return_value(reply, ok);
        else
            webkit_script_message_reply_return_error_message(reply, "clipboard is disabled");
    } else if (g_str_equal(cmd, "window.setInputRegion") || g_str_equal(cmd, "window.setKeyboard")) {
        g_autoptr(JSCValue) win_value = jsc_value_object_get_property(message, "win");
        g_autofree gchar *win = jsc_value_to_string(win_value);
        GtkWindow *window = find_window(state, win);
        g_autoptr(JSCValue) ok = jsc_value_new_boolean(ctx, TRUE);

        if (window == NULL)
            webkit_script_message_reply_return_error_message(reply, "no such window");
        else if (g_str_equal(cmd, "window.setInputRegion")) {
            g_autoptr(JSCValue) rects_value = jsc_value_object_get_property(message, "rects");
            g_autofree gchar *rects = jsc_value_to_string(rects_value);

            if (window_set_input_region(window, rects))
                webkit_script_message_reply_return_value(reply, ok);
            else
                webkit_script_message_reply_return_error_message(reply, "expected null or a list of {x, y, width, height}");
        } else {
            g_autoptr(JSCValue) mode_value = jsc_value_object_get_property(message, "mode");
            g_autofree gchar *mode = jsc_value_to_string(mode_value);

            if (g_str_equal(mode, "none"))           gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
            else if (g_str_equal(mode, "on-demand")) gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
            else if (g_str_equal(mode, "exclusive")) gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
            else {
                webkit_script_message_reply_return_error_message(reply, "expected none, on-demand or exclusive");
                return TRUE;
            }

            webkit_script_message_reply_return_value(reply, ok);
        }
    } else {
        webkit_script_message_reply_return_error_message(reply, "unknown command");
    }

    return TRUE;
}

static gboolean widget_serve(AppState *state, WebKitURISchemeRequest *request, const gchar *host, const gchar *path) {
    gchar *end;
    guint64 index = g_ascii_strtoull(host + strlen("widget-"), &end, 10);

    if (*end != '\0' || end == host + strlen("widget-") || index >= state->config->widgets->len)
        return FALSE;

    WidgetConfig *w = g_ptr_array_index(state->config->widgets, index);

    if (w->path == NULL)
        return FALSE;

    g_autofree gchar *dir = g_path_get_dirname(w->path);
    g_autoptr(GFile) root = g_file_new_for_path(dir);

    while (*path == '/')
        path++;

    g_autofree gchar *entry = *path ? NULL : g_path_get_basename(w->path);
    g_autoptr(GFile) file = g_file_resolve_relative_path(root, entry ? entry : path);

    if (!g_file_has_prefix(file, root))
        return FALSE;

    gchar *contents;
    gsize len;

    if (!g_file_load_contents(file, NULL, &contents, &len, NULL, NULL))
        return FALSE;

    g_autoptr(GBytes) bytes = g_bytes_new_take(contents, len);
    g_autofree gchar *name = g_file_get_basename(file);
    g_autofree gchar *type = g_content_type_guess(name, (const guchar *)contents, len, NULL);
    g_autofree gchar *mime = g_content_type_get_mime_type(type);
    g_autoptr(GInputStream) stream = g_memory_input_stream_new_from_bytes(bytes);

    webkit_uri_scheme_request_finish(request, stream, len, mime);
    return TRUE;
}

// wwwidgets://icon/<name> for a themed icon, wwwidgets://icon//abs/path or wwwidgets://icon/file:///... for a file
static gboolean icon_serve(WebKitURISchemeRequest *request, const gchar *path) {
    if (*path == '/')
        path++;

    Image img = { 0 };

    if (!image_load_icon(&img, path, NULL))
        return FALSE;

    image_serve(&img, request);
    image_clear(&img);

    return TRUE;
}

static void on_scheme_request(WebKitURISchemeRequest *request, gpointer user_data) {
    AppState *state = user_data;
    g_autoptr(GUri) uri = g_uri_parse(webkit_uri_scheme_request_get_uri(request), G_URI_FLAGS_NONE, NULL);
    const gchar *host = uri ? g_uri_get_host(uri) : NULL;

    if (host && g_str_equal(host, "icon") && icon_serve(request, g_uri_get_path(uri)))
        return;

    if (host && g_str_equal(host, "notifications") && notifications_serve(state, request, g_uri_get_path(uri)))
        return;

    if (host && g_str_equal(host, "clipboard") && clipboard_serve(state, request, g_uri_get_path(uri)))
        return;

    if (host && g_str_equal(host, "tray") && tray_serve(state, request, g_uri_get_path(uri)))
        return;

    if (host && g_str_has_prefix(host, "widget-") && widget_serve(state, request, host, g_uri_get_path(uri)))
        return;

    g_autoptr(GError) err = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "not found");
    webkit_uri_scheme_request_finish_error(request, err);
}

void state_init(AppState *state) {
    GdkDisplay *display = gdk_display_get_default();

    state->monitors = g_object_ref(gdk_display_get_monitors(display));
    state->monitor_windows = g_hash_table_new_full(g_direct_hash, g_direct_equal, g_object_unref, (GDestroyNotify)g_ptr_array_unref);

    state->content_manager = webkit_user_content_manager_new();
    webkit_user_content_manager_register_script_message_handler_with_reply(state->content_manager, "native", NULL);
    g_signal_connect(state->content_manager, "script-message-with-reply-received::native", G_CALLBACK(on_native_message), state);

    g_autoptr(WebKitUserScript) s = webkit_user_script_new(SHIM, WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, NULL, NULL);
    webkit_user_content_manager_add_script(state->content_manager, s);
    g_debug("shim injected to js");

    state->views = g_ptr_array_new();
    state->windows = g_ptr_array_new();
    store_init(state);
    webkit_web_context_register_uri_scheme(webkit_web_context_get_default(), "wwwidgets", on_scheme_request, state, NULL);

    if (state->config->workspaces)
        workspace_init(state);

    if (state->config->notifications)
        notifications_init(state);

    if (state->config->tray)
        tray_init(state);

    if (state->config->audio)
        audio_init(state);

    if (state->config->bluetooth)
        bluetooth_init(state);

    if (state->config->network)
        network_init(state);

    if (state->config->battery)
        battery_init(state);

    if (state->config->exec)
        process_init(state);

    if (state->config->toplevels)
        toplevels_init(state);

    if (state->config->clipboard)
        clipboard_init(state);

    if (state->config->media)
        media_init(state);
}

void state_clear(AppState *state) {
    reload_clear(state);
    g_clear_pointer(&state->config_path, g_free);
    g_clear_handle_id(&state->monitors_sync_id, g_source_remove);
    g_clear_pointer(&state->monitor_windows, g_hash_table_unref);
    g_clear_object(&state->monitors);
    g_clear_pointer(&state->config, config_free);
    g_clear_object(&state->content_manager);
    g_clear_pointer(&state->views, g_ptr_array_unref);
    g_clear_pointer(&state->windows, g_ptr_array_unref);
    g_clear_pointer(&state->workspaces, g_ptr_array_unref);
    notifications_clear(state);
    tray_clear(state);
    audio_clear(state);
    bluetooth_clear(state);
    network_clear(state);
    store_clear(state);
    g_clear_object(&state->upower);
    process_clear(state);
    g_clear_pointer(&state->toplevels, g_ptr_array_unref);
    clipboard_clear(state);
    media_clear(state);
    g_debug("state cleared");
}
