#pragma once

#include <gio/gio.h>
#include <gtk/gtk.h>
#include <webkit/webkit.h>
#include <glib.h>

#include "config.h"

struct ext_workspace_manager_v1;

typedef struct Tray Tray;
typedef struct Audio Audio;
typedef struct BluetoothAgent BluetoothAgent;
typedef struct Network Network;

typedef struct {
    Config *config;
    GListModel *monitors;
    WebKitUserContentManager *content_manager;
    GPtrArray *views;
    GPtrArray *windows;
    GHashTable *store;
    struct ext_workspace_manager_v1 *ws_manager;
    GPtrArray *workspaces;
    GDBusConnection *notif_bus;
    guint notif_owner;
    guint32 notif_next_id;
    GPtrArray *notifications;
    Tray *tray;
    Audio *audio;
    GDBusObjectManager *bluez;
    BluetoothAgent *bt_agent;
    GDBusProxy *upower;
    Network *network;
} AppState;

void push_event(WebKitWebView *view, const gchar *name, GVariant *detail);
void push_event_json(WebKitWebView *view, const gchar *name, const gchar *json);
gboolean window_set_input_region(GtkWindow *window, const gchar *json);
void state_init(AppState *state);
void state_clear(AppState *state);

void on_monitors_changed(GListModel *list, guint position, guint removed, guint added, gpointer user_data);
