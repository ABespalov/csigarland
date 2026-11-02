/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Detect a garland on the data pin while the LED rail is off
 * Contributors: None
 */

#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "garland_state.h"

namespace garland {

struct ProbeResult {
    ProbeStatus status;
    uint16_t led_estimate;  // 0 when unknown.
};

struct PresenceReading {
    ProbeStatus status;
    uint32_t mv;
};

// Whether anything is on the data pin, in under a millisecond and with the rail on or
// off, so a garland plugged in or removed at any time is noticed. Gives no LED count.
// The pin must be free of the LED output, and is left driven low.
PresenceReading probe_presence(gpio_num_t data_pin);

// WS2812B has no read-back, so this reads the input protection diode instead.
// The rail must already be off, and the pin is left driven low. Blocks up to
// about 0.7 s: the settle time for a just-released rail and the measurement.
ProbeResult probe_garland(gpio_num_t data_pin);

}  // namespace garland
