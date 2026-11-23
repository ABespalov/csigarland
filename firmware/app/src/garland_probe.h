/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Tell whether a garland is on the data pin
 * Contributors: None
 */

#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "garland_state.h"

namespace garland {

struct PresenceReading {
    ProbeStatus status;
    uint32_t mv;
};

// Whether anything is on the data pin, in under a millisecond and with the rail on or
// off, so a garland plugged in or removed at any time is noticed. Gives no LED count.
// The pin must be free of the LED output, and is left driven low.
PresenceReading probe_presence(gpio_num_t data_pin);

}  // namespace garland
