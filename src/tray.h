#pragma once

#include <gtk/gtk.h>

#include "state.h"

typedef void (*TrayMenuCallback)(GVariant *menu, const gchar *error, gpointer user_data);

void tray_init(AppState *state);
void tray_clear(AppState *state);

GVariant *tray_list(AppState *state);
gboolean tray_activate(AppState *state, guint32 id, const gchar *method);
gboolean tray_scroll(AppState *state, guint32 id, gint32 delta, const gchar *orientation);
gboolean tray_menu(AppState *state, guint32 id, TrayMenuCallback callback, gpointer user_data);
gboolean tray_menu_click(AppState *state, guint32 id, gint32 item);
gboolean tray_serve(AppState *state, WebKitURISchemeRequest *request, const gchar *path);
