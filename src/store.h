#pragma once

#include <glib.h>
#include <gtk/gtk.h>

#include "state.h"

void store_init(AppState *state);
void store_clear(AppState *state);

const gchar *store_get(AppState *state, const gchar *key);
gboolean store_set(AppState *state, const gchar *key, const gchar *json);
void store_apply_visibility(AppState *state, GtkWindow *window);
gint store_command(AppState *state, GApplicationCommandLine *cmdline, gint argc, gchar **argv);
