#include <gdk/gdk.h>

#include "state.h"
#include "glib-object.h"

void state_init(AppState *state) {
    GdkDisplay *display = gdk_display_get_default();
    state->monitors = g_object_ref(gdk_display_get_monitors(display));
    g_signal_connect(state->monitors, "items-changed", G_CALLBACK(on_monitors_changed), state);
}

void state_clear(AppState *state) {
    g_clear_object(&state->monitors);
    g_clear_pointer(&state->config, config_free);
}

void on_monitors_changed(GListModel *list, guint position, guint removed, guint added, gpointer user_data) {
    (void)user_data;
    g_debug("monitors: %u removed, %u added at %u, now %u", removed, added, position, g_list_model_get_n_items(list));
}
