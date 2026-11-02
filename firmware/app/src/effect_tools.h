/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: What the animation algorithms share: timing, colour arithmetic, noise, drawing
 * Contributors: None
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <span>

#include "effect_algorithms.h"

namespace garland {

// Each family of algorithms answers for its own names and returns false for the rest.
bool run_motion_algorithm(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds, AlgorithmState& state);
bool run_color_algorithm(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds, AlgorithmState& state, Rng& rng);
bool run_burst_algorithm(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds);

// sx 128 is the file's natural pace, and any other value scales it.
inline uint32_t paced_time(uint32_t time_ms, uint8_t sx) {
    return static_cast<uint32_t>(uint64_t{time_ms} * (uint32_t{sx} + 1) / 129);
}

inline uint32_t mix(uint32_t value) {
    value = (value ^ (value >> 15)) * 0x2C1B3C6Du;
    value = (value ^ (value >> 12)) * 0x297A2D39u;
    return value ^ (value >> 15);
}

// Parabola approximation of a sine wave, full cycle per 256, range 0..255.
inline uint8_t wave(uint32_t position) {
    const uint32_t half = position & 0x7F;
    const uint32_t bump = half * (128 - half) / 32;  // 0..128
    const uint32_t value = (position & 0x80) == 0 ? 127 + std::min<uint32_t>(bump, 128)
                                                  : 127 - std::min<uint32_t>(bump, 127);
    return static_cast<uint8_t>(value);
}

inline Rgb scaled(Rgb color, uint32_t level) {
    return Rgb{static_cast<uint8_t>(color.r * level / 255), static_cast<uint8_t>(color.g * level / 255),
               static_cast<uint8_t>(color.b * level / 255)};
}

inline Rgb mixed(Rgb from, Rgb to, uint32_t amount) {
    const auto channel = [amount](uint8_t a, uint8_t b) {
        return static_cast<uint8_t>((a * (255 - amount) + b * amount) / 255);
    };
    return Rgb{channel(from.r, to.r), channel(from.g, to.g), channel(from.b, to.b)};
}

// level is 0..255. Squaring approximates the eye, so fades look even to the end.
inline uint32_t perceptual(uint32_t level) {
    return level * level / 255;
}

inline Rgb brighter_of(Rgb a, Rgb b) {
    return Rgb{std::max(a.r, b.r), std::max(a.g, b.g), std::max(a.b, b.b)};
}

// col[1] when the file gives one.
inline Rgb background_of(const Layer& layer) {
    return layer.col_count > 1 ? layer.col[1] : Rgb{0, 0, 0};
}

// A dot between two LEDs, shared by how close it is to each, so it moves smoothly.
inline void draw_dot(std::span<Rgb> leds, uint32_t position_q8, Rgb color) {
    const uint32_t led = position_q8 >> 8;
    const uint32_t share = position_q8 & 0xFF;
    if (led < leds.size()) {
        leds[led] = brighter_of(leds[led], scaled(color, 255 - share));
    }
    if (led + 1 < leds.size()) {
        leds[led + 1] = brighter_of(leds[led + 1], scaled(color, share));
    }
}

// position 0 is the head of a trail and 255 its end: the palette runs along the
// trail and the light dies out towards the end.
inline Rgb trail_color(const Palette& palette, uint32_t position) {
    return scaled(palette_color(palette, static_cast<uint8_t>(position)), perceptual(255 - position));
}

// A lone colour takes turns with darkness.
inline Rgb wipe_color(const Palette& palette, uint32_t pass) {
    const uint32_t colors = palette.count > 1 ? palette.count : 2;
    const uint32_t index = pass % colors;
    return index < palette.count ? palette.stops[index].color : Rgb{0, 0, 0};
}

// Where a light running to the last LED and back is now, in 1/256ths of an LED.
inline uint32_t bounce_q8(uint32_t now, uint32_t step_ms, uint32_t count) {
    if (count < 2) {
        return 0;
    }
    const uint32_t span_ms = (count - 1) * step_ms;
    const uint32_t position = now % (2 * span_ms);
    const uint32_t along = position < span_ms ? position : 2 * span_ms - position;
    return static_cast<uint32_t>(uint64_t{along} * 256 / step_ms);
}

// Smooth random values along x, 256 units from one random value to the next, range 0..255.
inline uint8_t noise_line(uint32_t x, uint32_t seed) {
    const uint32_t cell = x >> 8;
    const uint32_t part = x & 0xFF;
    const uint32_t eased = part * part * (3 * 256 - 2 * part) / 65536;  // 0..255
    const uint32_t from = mix(cell + seed) & 0xFF;
    const uint32_t to = mix(cell + 1 + seed) & 0xFF;
    return static_cast<uint8_t>((from * (256 - eased) + to * eased) / 256);
}

// The same over two directions: along the chain and over time.
inline uint8_t noise_field(uint32_t x, uint32_t y) {
    const uint32_t row = y >> 8;
    const uint32_t part = y & 0xFF;
    const uint32_t from = noise_line(x, row * 7919);
    const uint32_t to = noise_line(x, (row + 1) * 7919);
    return static_cast<uint8_t>((from * (256 - part) + to * part) / 256);
}

}  // namespace garland
