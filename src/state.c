#include <gdk/gdk.h>
#include <glib.h>
#include <jsc/jsc.h>
#include <json-glib/json-glib.h>

#include "state.h"
#include "battery.h"

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
    "    battery: {"
    "      get: () => call('battery.get'),"
    "      onChange: (cb) => on('battery', cb),"
    "    },"
    "  };"
    "})();";

void push_event(WebKitWebView *view, const gchar *name, GVariant *detail) {
    g_autofree gchar *ev = g_strconcat("wwwidgets:", name, NULL);

    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", "name", g_variant_new_string(ev));
    g_variant_builder_add(&b, "{sv}", "detail", detail);

    webkit_web_view_call_async_javascript_function(view, "window.dispatchEvent(new CustomEvent(name, { detail }));", -1, g_variant_builder_end(&b), NULL, NULL, NULL, NULL, NULL);
}

static gboolean on_native_message(WebKitUserContentManager *manager, JSCValue *message, WebKitScriptMessageReply *reply, gpointer user_data) {
    (void)manager;

    AppState *state = user_data;
    JSCContext *ctx = jsc_value_get_context(message);

    g_autoptr(JSCValue) cmd_value = jsc_value_object_get_property(message, "cmd");
    g_autofree gchar *cmd = jsc_value_to_string(cmd_value);

    if (g_str_equal(cmd, "battery.get")) {
        if (!state->upower)
            return webkit_script_message_reply_return_error_message(reply, "no battery"), TRUE;

        g_autoptr(GVariant) snap = battery_snapshot(state->upower);
        g_autofree gchar *json = json_gvariant_serialize_data(snap, NULL);
        g_autoptr(JSCValue) result = jsc_value_new_from_json(ctx, json);
        webkit_script_message_reply_return_value(reply, result);
    }

    return TRUE;
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
    battery_init(state);
}

void state_clear(AppState *state) {
    g_clear_object(&state->monitors);
    g_clear_pointer(&state->config, config_free);
    g_clear_object(&state->content_manager);
    g_clear_pointer(&state->views, g_ptr_array_unref);
    g_clear_object(&state->upower);
    g_debug("state cleared");
}

void on_monitors_changed(GListModel *list, guint position, guint removed, guint added, gpointer user_data) {
    (void)user_data;
    g_debug("monitors: %u removed, %u added at %u, now %u", removed, added, position, g_list_model_get_n_items(list));
}
