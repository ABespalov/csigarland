/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The Lab build's measurements on the board: why it started, temperatures, memory and current
 * Contributors: None
 */

#pragma once

#include <cstdint>

#include "garland_state.h"
#include "lab_logic.h"

namespace garland {

// Keeps the last frame's current by the model, and the highest. One store from the Lighting task, never a lock.
void lab_store_current(uint32_t model_ma);

// Called by the Lighting task for each frame it renders. Nothing outside the Lab build.
inline void lab_note_current(uint32_t model_ma) {
    if constexpr (kLabBuild) {
        lab_store_current(model_ma);
    }
}

// At startup, after tls_pool_start and before anything uses TLS: puts a count behind mbedtls_calloc and
// mbedtls_free, which passes every ask on to the pool. Does nothing outside the Lab build.
void lab_count_tls();

// Last at startup, after the clock module: logs why the board started, a brownout among the reasons, and
// starts the health and TLS lines, once a minute. Does nothing outside the Lab build.
void lab_start(State& state);

}  // namespace garland
