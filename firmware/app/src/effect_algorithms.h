/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The animation algorithms effect files refer to by name
 * Contributors: None
 */

#pragma once

#include <cstdint>
#include <span>

#include "effect_spec.h"

namespace garland {

// Small and fast; randomness here only has to look natural.
class Rng {
public:
    explicit Rng(uint32_t seed) : state_(seed != 0 ? seed : 1) {}

    uint32_t next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }

    // Uniform enough in 0..limit-1 for animation.
    uint32_t below(uint32_t limit) { return limit == 0 ? 0 : next() % limit; }

private:
    uint32_t state_;
};

// What an algorithm keeps between frames.
struct AlgorithmState {
    std::span<uint8_t> a;  // At least one byte per LED, zero after an effect change.
    std::span<uint8_t> b;
    uint32_t last_step = 0;
    bool started = false;
};

// How many of an algorithm's steps have come since its last frame, which it then takes one by one,
// so that a fast SPEED or a late frame moves it on as far as the time that passed. Capped, so a long
// stall costs a frame no more than this.
constexpr uint32_t kMaxCatchUpSteps = 8;

inline uint32_t steps_to_take(AlgorithmState& state, uint32_t step) {
    const uint32_t steps = step - state.last_step < kMaxCatchUpSteps ? step - state.last_step : kMaxCatchUpSteps;
    state.last_step = step;
    return steps;
}

Rgb palette_color(const Palette& palette, uint8_t position);

// Draws one frame into leds. time_ms already includes the SPEED button and starts
// from 0 when the effect is activated.
void run_algorithm(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds, AlgorithmState& state, Rng& rng);

}  // namespace garland
