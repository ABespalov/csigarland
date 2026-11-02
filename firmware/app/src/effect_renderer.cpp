/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Compose an effect's layers into one frame
 * Contributors: None
 */

#include "effect_renderer.h"

#include <algorithm>

namespace garland {
namespace {

bool in_zone(const Layer& layer, uint32_t led) {
    switch (layer.zone) {
        case ZoneKind::Figurines:
            return (layer.figurine_mask & (1u << (led % kFigurineCount))) != 0;
        case ZoneKind::Every:
            return led % layer.every == layer.offset;
        case ZoneKind::All:
            return true;
    }
    return true;
}

uint8_t saturated_add(uint8_t a, uint8_t b) {
    return static_cast<uint8_t>(std::min<uint32_t>(uint32_t{a} + b, 255));
}

void blend_into(Rgb& target, Rgb color, Blend blend) {
    switch (blend) {
        case Blend::Replace:
            target = color;
            break;
        case Blend::Add:
            target = Rgb{saturated_add(target.r, color.r), saturated_add(target.g, color.g),
                         saturated_add(target.b, color.b)};
            break;
        case Blend::Max:
            target = Rgb{std::max(target.r, color.r), std::max(target.g, color.g), std::max(target.b, color.b)};
            break;
    }
}

}  // namespace

void EffectRenderer::reset() {
    for (int i = 0; i < kMaxLayers; ++i) {
        memory_a_[i].fill(0);
        memory_b_[i].fill(0);
        states_[i] = AlgorithmState{memory_a_[i], memory_b_[i], 0, false};
    }
}

void EffectRenderer::render(const EffectSpec& effect, uint32_t time_ms, std::span<Rgb> frame) {
    std::ranges::fill(frame, Rgb{0, 0, 0});
    for (int i = 0; i < effect.layer_count; ++i) {
        render_layer(effect.layers[i], states_[i], time_ms, frame);
    }
}

void EffectRenderer::render_layer(const Layer& layer, AlgorithmState& state, uint32_t time_ms, std::span<Rgb> frame) {
    const uint32_t count = static_cast<uint32_t>(frame.size());
    const uint32_t start = std::min<uint32_t>(layer.start, count);
    const uint32_t stop = layer.stop == 0 ? count : std::min<uint32_t>(layer.stop, count);
    if (start >= stop) {
        return;
    }
    const uint32_t length = stop - start;
    const uint32_t unit = uint32_t{layer.grp} + layer.spc;
    // A mirrored layer draws half the range and reflects it onto the rest.
    const uint32_t drawn = layer.mi ? (length + 1) / 2 : length;
    const uint32_t virtual_count = std::min<uint32_t>((drawn + unit - 1) / unit, virtual_leds_.size());

    const std::span<Rgb> leds(virtual_leds_.data(), virtual_count);
    run_algorithm(layer, time_ms, leds, state, rng_);

    const int32_t rotation = layer.of % static_cast<int32_t>(virtual_count);
    for (uint32_t v = 0; v < virtual_count; ++v) {
        const uint32_t rotated = static_cast<uint32_t>(
            (static_cast<int32_t>(v) + rotation + static_cast<int32_t>(virtual_count)) %
            static_cast<int32_t>(virtual_count));
        const uint32_t ordered = layer.rev ? virtual_count - 1 - rotated : rotated;
        Rgb color = leds[v];
        if (layer.bri != 255) {
            color = Rgb{static_cast<uint8_t>(color.r * layer.bri / 255), static_cast<uint8_t>(color.g * layer.bri / 255),
                        static_cast<uint8_t>(color.b * layer.bri / 255)};
        }
        for (uint32_t k = 0; k < layer.grp; ++k) {
            const uint32_t offset = ordered * unit + k;
            if (offset >= drawn) {
                break;
            }
            const uint32_t led = start + offset;
            if (in_zone(layer, led)) {
                blend_into(frame[led], color, layer.blend);
            }
            if (layer.mi) {
                const uint32_t reflected = stop - 1 - offset;
                if (reflected != led && in_zone(layer, reflected)) {
                    blend_into(frame[reflected], color, layer.blend);
                }
            }
        }
    }
}

}  // namespace garland
