#pragma once

#include "ui_types.h"

struct UiInputContext {
  bool configMode = false;
  bool sensorDetailsAvailable = false;
};

// Converts a raw touch coordinate into a semantic action. Both supported
// portrait displays use the same full-width horizontal hit regions.
UiAction resolveTouchAction(UiPage page, int x, int y,
                            const UiInputContext &context);
