#pragma once

#include <gio/gio.h>
#include <webkit/webkit.h>
#include <glib.h>

#include "config.h"

struct ext_workspace_manager_v1;

typedef struct Tray Tray;

typedef struct {
    Config *config;
    GListModel *monitors;
    WebKitUserContentManager *content_manager;
    GPtrArray *views;
    GDBusProxy *upower;
    struct ext_workspace_manager_v1 *ws_manager;
    GPtrArray *workspaces;
    GDBusConnection *notif_bus;
    guint notif_owner;
    guint32 notif_next_id;
    GPtrArray *notifications;
    Tray *tray;

} AppState;

void push_event(WebKitWebView *view, const gchar *name, GVariant *detail);
void state_init(AppState *state);
void state_clear(AppState *state);

void on_monitors_changed(GListModel *list, guint position, guint removed, guint added, gpointer user_data);
