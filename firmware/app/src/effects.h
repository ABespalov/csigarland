/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The last stage before the LEDs: brightness, current limiter, temporal dithering
 * Contributors: None
 *
 * An LED channel has 256 steps, which is too few once a brightness ceiling is applied:
 * a dim level has a handful of steps left, so fades crawl and colours drift. So brightness
 * and the limiter work in 1/256ths of a step, and FrameDither turns that into frames sent
 * about 400 times a second, whose average is the wanted level. Same approach as csiPass's
 * status LED, by the same author.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace garland {

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;

    bool operator==(const Rgb&) const = default;
};

// A colour in 1/256ths of a step: full is 255 * 256.
struct RgbQ8 {
    uint16_t r;
    uint16_t g;
    uint16_t b;

    bool operator==(const RgbQ8&) const = default;
};

constexpr uint32_t kFullBrightnessGain = 65536;

// The owner's percent is perceptual, so each step looks about the same anywhere on the
// scale (gamma 2.2). 1 % puts a full channel on the LED's dimmest step, and 0 is off.
uint32_t brightness_gain(uint8_t percent);

// Sizes must match. Below a quarter of a step a channel is dark: fewer lit frames than that
// read as a twinkle, not a glow.
void apply_brightness(std::span<const Rgb> frame, uint32_t gain, std::span<RgbQ8> out);

// The current the frame would draw from the 5 V rail, on average over its dithering.
uint32_t estimate_current_ma(std::span<const RgbQ8> frame);

// Scales the frame down so it draws no more than limit_ma. Returns the current
// before scaling.
uint32_t limit_current(std::span<RgbQ8> frame, uint32_t limit_ma);

// One first-order sigma-delta per channel of each LED: the target plus what the frames
// before left over, cut to a whole step, the rest carried on. A channel only ever shows
// the two steps either side of its target, and averages exactly to it.
class FrameDither {
public:
    // Fills out and says whether the frame differs from the last one, as a level that needs
    // no dithering then costs no traffic.
    bool step(std::span<const RgbQ8> targets, std::span<Rgb> out);

    // The next step reports a change whatever it is, as the LEDs may not hold the last frame.
    void resend() { shown_valid_ = false; }

    // Each LED starts its cycle at a different point: all of them stepping up on the same
    // frame would flicker where spread over the chain it cannot be seen.
    void restart();

private:
    std::array<std::array<uint8_t, 3>, CSIGARLAND_MAX_LEDS> residue_{};
    std::array<Rgb, CSIGARLAND_MAX_LEDS> shown_{};
    bool shown_valid_ = false;
};

}  // namespace garland
