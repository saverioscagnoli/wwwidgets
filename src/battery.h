#pragma once

#include "state.h"

void battery_init(AppState *state);
GVariant *battery_snapshot(GDBusProxy *p);
