/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Turn a data-pin voltage reading into a presence verdict
 * Contributors: None
 */

#pragma once

#include <cstdint>

namespace garland {

// An open pin rises through the pull-up within microseconds, and on this board
// measured 2.66 V, not the full supply. An LED strip on a live 5 V rail measured a
// steady 1.90-1.94 V, its data input loading the pull-up like a divider, and the
// owner's garland of 32 on its gated rail, powered or not, 1.99-2.02 V. The threshold
// sits between those and an open pin.
constexpr uint32_t kProbePresentBelowMv = 2300;
constexpr uint32_t kProbePullupMv = 3300;

constexpr bool probe_says_present(uint32_t pin_mv) {
    return pin_mv < kProbePresentBelowMv;
}

}  // namespace garland
