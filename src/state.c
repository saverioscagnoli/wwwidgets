#include <gdk/gdk.h>
#include <glib.h>
#include <jsc/jsc.h>
#include <json-glib/json-glib.h>

#include "state.h"
#include "battery.h"
#include "notifications.h"
#include "webkit/WebKitUserContentManager.h"
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
    "  window.wwwidgets = {"
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
    "    battery: {"
    "      get: () => call('battery.get'),"
    "      onChange: (cb) => on('battery', cb),"
    "    },"
    "  };"
    "})();";

static void reply_variant(WebKitScriptMessageReply *reply, JSCContext *ctx, GVariant *v) {
    g_autofree gchar *json = json_gvariant_serialize_data(v, NULL);
    g_autoptr(JSCValue) result = jsc_value_new_from_json(ctx, json);
    webkit_script_message_reply_return_value(reply, result);
}

void push_event(WebKitWebView *view, const gchar *name, GVariant *detail) {
    g_autofree gchar *s = g_variant_print(detail, TRUE);
    g_debug("dispatching event: %s, detail: %s", name, s);

    g_autofree gchar *ev = g_strconcat("wwwidgets:", name, NULL);
    g_autofree gchar *json = json_gvariant_serialize_data(detail, NULL);

    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", "name", g_variant_new_string(ev));
    g_variant_builder_add(&b, "{sv}", "detail", g_variant_new_string(json));

    webkit_web_view_call_async_javascript_function(view, "window.dispatchEvent(new CustomEvent(name, { detail: JSON.parse(detail) }));", -1, g_variant_builder_end(&b), NULL, NULL, NULL, NULL, NULL);
}

static gboolean on_native_message(WebKitUserContentManager *manager, JSCValue *message, WebKitScriptMessageReply *reply, gpointer user_data) {
    (void)manager;

    AppState *state = user_data;
    JSCContext *ctx = jsc_value_get_context(message);

    g_autoptr(JSCValue) cmd_value = jsc_value_object_get_property(message, "cmd");
    g_autofree gchar *cmd = jsc_value_to_string(cmd_value);

    if (g_str_equal(cmd, "workspace.list")) {
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

static void on_scheme_request(WebKitURISchemeRequest *request, gpointer user_data) {
    AppState *state = user_data;
    g_autoptr(GUri) uri = g_uri_parse(webkit_uri_scheme_request_get_uri(request), G_URI_FLAGS_NONE, NULL);
    const gchar *host = uri ? g_uri_get_host(uri) : NULL;

    if (host && g_str_equal(host, "notifications") && notifications_serve(state, request, g_uri_get_path(uri)))
        return;

    if (host && g_str_has_prefix(host, "widget-") && widget_serve(state, request, host, g_uri_get_path(uri)))
        return;

    g_autoptr(GError) err = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "not found");
    webkit_uri_scheme_request_finish_error(request, err);
}

void state_init(AppState *state) {
    GdkDisplay *display = gdk_display_get_default();

    state->monitors = g_object_ref(gdk_display_get_monitors(display));

    g_signal_connect(state->monitors, "items-changed", G_CALLBACK(on_monitors_changed), state);

    state->content_manager = webkit_user_content_manager_new();
    webkit_user_content_manager_register_script_message_handler_with_reply(state->content_manager, "native", NULL);
    g_signal_connect(state->content_manager, "script-message-with-reply-received::native", G_CALLBACK(on_native_message), state);

    g_autoptr(WebKitUserScript) s = webkit_user_script_new(SHIM, WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, NULL, NULL);
    webkit_user_content_manager_add_script(state->content_manager, s);
    g_debug("shim injected to js");

    state->views = g_ptr_array_new();
    webkit_web_context_register_uri_scheme(webkit_web_context_get_default(), "wwwidgets", on_scheme_request, state, NULL);

    if (state->config->workspaces)
        workspace_init(state);

    if (state->config->notifications)
        notifications_init(state);

    if (state->config->battery)
        battery_init(state);
}

void state_clear(AppState *state) {
    g_clear_object(&state->monitors);
    g_clear_pointer(&state->config, config_free);
    g_clear_object(&state->content_manager);
    g_clear_pointer(&state->views, g_ptr_array_unref);
    g_clear_pointer(&state->workspaces, g_ptr_array_unref);
    notifications_clear(state);
    g_clear_object(&state->upower);
    g_debug("state cleared");
}

void on_monitors_changed(GListModel *list, guint position, guint removed, guint added, gpointer user_data) {
    (void)user_data;
    g_debug("monitors: %u removed, %u added at %u, now %u", removed, added, position, g_list_model_get_n_items(list));
}
