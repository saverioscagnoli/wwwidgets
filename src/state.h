#pragma once

#include <gio/gio.h>
#include <webkit/webkit.h>
#include <glib.h>

#include "config.h"

typedef struct {
    Config *config;
    GListModel *monitors;
    WebKitUserContentManager *content_manager;
} AppState;

void state_init(AppState *state);
void state_clear(AppState *state);

void on_monitors_changed(GListModel *list, guint position, guint removed, guint added, gpointer user_data);
