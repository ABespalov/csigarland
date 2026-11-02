/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Wire the three panel buttons to the garland state
 * Contributors: None
 */

#pragma once

#include "garland_state.h"

namespace garland {

// False if any button could not be set up. The others still work.
bool start_buttons(State& state);

}  // namespace garland
