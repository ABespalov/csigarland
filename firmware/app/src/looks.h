/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The LED count, favourites and screen settings on the state, read again while they could not be
 * Contributors: None
 */

#pragma once

#include <cstdint>

#include "esp_err.h"
#include "garland_state.h"

namespace garland {

// At the start, after the uploaded effects, which favourites may name, and before the first favourite is
// chosen: each read onto the state, tried three times, with favourites the catalog's defaults when none
// were saved. One that still cannot be read keeps its default meanwhile, and nothing is saved over it.
void looks_load(State& state);

// For the scheduler's task, every look round: tries again, at most every few seconds, while any is unread.
void looks_retry_if_unread(State& state, uint32_t now_ms);

// Before a change to the favourites or the screen settings, which is worked out from the state and saves
// every part of them: read onto the state first if they could not be. False while they still cannot be, and
// nothing is to be saved, as it would write the defaults over what is there.
bool looks_favorites_read(State& state);
bool looks_display_read(State& state);

// Saves the LED count, and puts it on the state, under the lock a late reading takes, so none comes after
// it with the value it replaced.
esp_err_t looks_save_led_count(State& state, uint16_t setting);

}  // namespace garland
