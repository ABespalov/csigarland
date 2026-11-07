/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The last stage before the LEDs: brightness, current limiter, temporal dithering
 * Contributors: None
 */

#include "effects.h"

#include <cmath>

namespace garland {
namespace {

constexpr uint64_t kChannelUnitsPerLed = uint64_t{3} * 255 * 256;
constexpr uint16_t kFullChannelQ8 = 255 * 256;
constexpr uint32_t kMinimumGain = kFullBrightnessGain / 255 + 1;  // A full channel at one step.
constexpr uint32_t kDarkBelowQ8 = 64;
constexpr float kBrightnessGamma = 2.2F;

uint16_t channel_q8(uint8_t value, uint32_t gain) {
    const uint32_t level = (uint32_t{value} * gain) >> 8;
    return static_cast<uint16_t>(level < kDarkBelowQ8 ? 0 : level);
}

uint8_t dither_channel(uint16_t target, uint8_t& residue) {
    const uint32_t sum = uint32_t{residue} + target;
    residue = static_cast<uint8_t>(sum & 0xFF);
    return static_cast<uint8_t>(sum >= kFullChannelQ8 ? 255 : sum >> 8);
}

}  // namespace

uint32_t brightness_gain(uint8_t percent) {
    if (percent == 0) {
        return 0;
    }
    if (percent >= 100) {
        return kFullBrightnessGain;
    }
    const float unit = static_cast<float>(percent - 1) / 99.0F;
    return kMinimumGain + static_cast<uint32_t>(std::lround(std::pow(unit, kBrightnessGamma) *
                                                            static_cast<float>(kFullBrightnessGain - kMinimumGain)));
}

void apply_brightness(std::span<const Rgb> frame, uint32_t gain, std::span<RgbQ8> out) {
    for (size_t i = 0; i < frame.size(); ++i) {
        out[i] = RgbQ8{channel_q8(frame[i].r, gain), channel_q8(frame[i].g, gain), channel_q8(frame[i].b, gain)};
    }
}

uint32_t estimate_current_ma(std::span<const RgbQ8> frame, const CurrentModel& model) {
    uint64_t units = 0;
    for (const RgbQ8& led : frame) {
        units += uint64_t{led.r} + led.g + led.b;
    }
    return static_cast<uint32_t>(units * model.white_ma / kChannelUnitsPerLed) +
           static_cast<uint32_t>(frame.size()) * model.idle_ma;
}

bool FrameDither::step(std::span<const RgbQ8> targets, std::span<Rgb> out) {
    bool changed = !shown_valid_;
    for (size_t i = 0; i < targets.size(); ++i) {
        const Rgb led{dither_channel(targets[i].r, residue_[i][0]), dither_channel(targets[i].g, residue_[i][1]),
                      dither_channel(targets[i].b, residue_[i][2])};
        out[i] = led;
        changed = changed || !(led == shown_[i]);
        shown_[i] = led;
    }
    shown_valid_ = true;
    return changed;
}

void FrameDither::restart() {
    for (size_t i = 0; i < residue_.size(); ++i) {
        for (size_t channel = 0; channel < residue_[i].size(); ++channel) {
            // Multiplying by an odd number visits every value, which spreads neighbours apart.
            residue_[i][channel] = static_cast<uint8_t>((i * 3 + channel) * 89 + 128);
        }
    }
    shown_valid_ = false;
}

uint32_t limit_current(std::span<RgbQ8> frame, const CurrentModel& model) {
    const uint32_t current_ma = estimate_current_ma(frame, model);
    if (current_ma <= model.limit_ma) {
        return current_ma;
    }
    // Idle draw cannot be dimmed away, so only the rest of the budget scales.
    const uint32_t idle_ma = static_cast<uint32_t>(frame.size()) * model.idle_ma;
    const uint32_t lit_budget_ma = model.limit_ma > idle_ma ? model.limit_ma - idle_ma : 0;
    const uint32_t lit_ma = current_ma - idle_ma;
    // A limit below the idle draw of a dark frame: there is nothing to scale.
    if (lit_ma == 0) {
        return current_ma;
    }
    for (RgbQ8& led : frame) {
        led.r = static_cast<uint16_t>(uint32_t{led.r} * lit_budget_ma / lit_ma);
        led.g = static_cast<uint16_t>(uint32_t{led.g} * lit_budget_ma / lit_ma);
        led.b = static_cast<uint16_t>(uint32_t{led.b} * lit_budget_ma / lit_ma);
    }
    return current_ma;
}

}  // namespace garland
