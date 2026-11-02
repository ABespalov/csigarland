/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: ST7789V2 status screen drawn with LVGL
 * Contributors: None
 */

#pragma once

#include "garland_state.h"

namespace garland {

// A display failure is logged and leaves the garland working without a screen.
// False if the driver or LVGL could not be set up. A panel that is simply not there goes unnoticed,
// as nothing reads back from it.
bool start_display(State& state);

}  // namespace garland
