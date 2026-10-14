#pragma once

#include "state.h"

typedef void (*ProcessExecCallback)(GVariant *result, const gchar *error, gpointer user_data);

void process_init(AppState *state);
void process_clear(AppState *state);

void process_exec(AppState *state, const gchar *const *argv, ProcessExecCallback callback, gpointer user_data);
guint32 process_spawn(AppState *state, const gchar *const *argv, GError **error);
gboolean process_write(AppState *state, guint32 id, const gchar *data);
gboolean process_kill(AppState *state, guint32 id, gint signal);
