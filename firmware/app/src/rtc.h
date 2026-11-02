/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The optional DS3231 clock module on the I2C pads
 * Contributors: None
 */

#pragma once

#include <cstdint>

namespace garland {

// Looks for the part once, at startup. Without one the pads are released and every call
// below returns false.
bool rtc_start();

bool rtc_present();

// False when the part did not answer or holds no time at all. trusted says whether what
// it holds can be believed, see rtc_is_trusted.
bool rtc_read(int64_t& epoch, bool& trusted);

// Sets the time, and clears the part's word that it had lost it.
bool rtc_write(int64_t epoch);

}  // namespace garland
