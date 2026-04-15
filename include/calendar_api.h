#pragma once
#include "display.h"
#include "config.h"

// Returns true on success, populates ev. On failure returns false.
bool calendar_fetch(const AppConfig &cfg, CalEvent &ev);
