/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Turn data-pin voltage readings into a presence verdict and LED estimate
 * Contributors: None
 */

#pragma once

#include <algorithm>
#include <cstdint>

namespace garland {

// An open pin rises through the pull-up within microseconds, and on this board
// measured 2.66 V, not the full supply. A garland on an unpowered rail holds it about
// one diode drop above the rail. An LED strip on a live 5 V rail measured a steady
// 1.90-1.94 V: its data input loads the pull-up like a divider. The threshold sits
// halfway between that and an open pin.
constexpr uint32_t kProbePresentBelowMv = 2300;
constexpr uint32_t kProbePullupMv = 3300;

// Nominal values, both with wide tolerance. They are the calibration points for
// a real garland; the probe logs its raw readings for that.
constexpr uint32_t kProbePullupKohm = 45;
constexpr uint32_t kProbeCapPerLedNf = 100;

constexpr bool probe_says_present(uint32_t start_mv) {
    return start_mv < kProbePresentBelowMv;
}

// The pull-up charges the rail capacitance, one decoupling capacitor per LED
// board, through the input protection diode: C = I * dt / dV. Returns 0 when the
// readings cannot give a number.
constexpr uint16_t probe_estimate_leds(uint32_t start_mv, uint32_t end_mv, uint32_t elapsed_us) {
    if (end_mv <= start_mv || elapsed_us == 0) {
        return 0;
    }
    const uint32_t middle_mv = (start_mv + end_mv) / 2;
    if (middle_mv >= kProbePullupMv) {
        return 0;
    }
    // mV / kOhm = uA, and uA * us / mV = nF.
    const uint32_t current_ua = (kProbePullupMv - middle_mv) / kProbePullupKohm;
    const uint32_t capacitance_nf =
        static_cast<uint32_t>(uint64_t{current_ua} * elapsed_us / (end_mv - start_mv));
    const uint32_t leds = (capacitance_nf + kProbeCapPerLedNf / 2) / kProbeCapPerLedNf;
    return static_cast<uint16_t>(std::clamp<uint32_t>(leds, 1, CSIGARLAND_MAX_LEDS));
}

}  // namespace garland
