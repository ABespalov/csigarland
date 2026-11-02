/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Wi-Fi access point and the web portal served over it
 * Contributors: None
 */

#pragma once

#include "garland_state.h"

namespace garland {

// A portal failure is logged and leaves the garland working without it.
bool start_portal(State& state);

}  // namespace garland
