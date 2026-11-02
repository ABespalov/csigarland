/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Compose an effect's layers into one frame
 * Contributors: None
 */

#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "effect_algorithms.h"
#include "effect_spec.h"

namespace garland {

constexpr int kRendererMaxLeds = CSIGARLAND_MAX_LEDS;

// Holds its buffers inside, so keep it in static storage.
class EffectRenderer {
public:
    explicit EffectRenderer(uint32_t seed) : rng_(seed) { reset(); }

    // Call when another effect is activated: algorithms start from nothing.
    void reset();

    void render(const EffectSpec& effect, uint32_t time_ms, std::span<Rgb> frame);

private:
    void render_layer(const Layer& layer, AlgorithmState& state, uint32_t time_ms, std::span<Rgb> frame);

    Rng rng_;
    std::array<Rgb, kRendererMaxLeds> virtual_leds_{};
    std::array<std::array<uint8_t, kRendererMaxLeds>, kMaxLayers> memory_a_{};
    std::array<std::array<uint8_t, kRendererMaxLeds>, kMaxLayers> memory_b_{};
    std::array<AlgorithmState, kMaxLayers> states_{};
};

}  // namespace garland
