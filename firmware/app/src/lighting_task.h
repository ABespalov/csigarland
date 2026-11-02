/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: High-priority task that owns the LED rail, the probe and the frames
 * Contributors: None
 */

#pragma once

#include "garland_state.h"

namespace garland {

// False if the task could not be started.
bool start_lighting_task(State& state);

}  // namespace garland
