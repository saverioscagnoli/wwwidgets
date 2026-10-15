#pragma once

#include "state.h"

void reload_init(AppState *state, void (*respawn)(AppState *state));
void reload_clear(AppState *state);
