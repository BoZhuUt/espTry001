#pragma once

#include "ui_types.h"

struct UiInputContext {
  bool configMode = false;
  bool sensorDetailsAvailable = false;
};

// Converts a raw touch coordinate into a semantic action. It intentionally
// reproduces the original 1.47-inch hit regions and their evaluation order.
UiAction resolveTouchAction(UiPage page, int x, int y,
                            const UiInputContext &context);
