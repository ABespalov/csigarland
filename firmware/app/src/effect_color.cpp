/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Algorithms that change colour and brightness in place
 * Contributors: None
 */

#include <algorithm>

#include "effect_tools.h"

namespace garland {
namespace {

constexpr uint32_t kTrafficCycleMs = 6000;
constexpr uint32_t kTrafficStopMs = 2400;
constexpr uint32_t kTrafficReadyMs = 3000;
constexpr uint32_t kTrafficGoMs = 5400;
constexpr uint32_t kEyesMinPeriodMs = 2600;
constexpr uint32_t kEyesPeriodSpreadMs = 2400;
constexpr uint32_t kEyesOpenMs = 200;
constexpr uint32_t kEyesCloseMs = 400;
constexpr uint32_t kEyesBlinkMs = 120;
constexpr uint32_t kEyesGap = 2;
constexpr uint32_t kTwinkleUpMinPeriodMs = 1400;
constexpr uint32_t kTwinkleUpSpreadMs = 1600;
constexpr uint32_t kDynamicStepMs = 250;
constexpr uint32_t kIcicleMinPeriodMs = 1800;
constexpr uint32_t kIcicleSpreadMs = 1800;
constexpr uint32_t kIcicleTailLeds = 3;
constexpr uint32_t kTvSceneMs = 1700;
constexpr uint32_t kTvFlickerMs = 110;

// Lamps in threes, repeating along the chain: col[0] stop, col[1] ready, col[2] go. With
// ix from 128 up, stop goes straight to go.
void run_traffic(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t at = paced_time(time_ms, layer.sx) % kTrafficCycleMs;
    const bool ready_with_stop = layer.ix < 128 && at >= kTrafficStopMs && at < kTrafficReadyMs;
    const bool stop = at < kTrafficStopMs || (layer.ix >= 128 && at < kTrafficReadyMs) || ready_with_stop;
    const bool ready = ready_with_stop || at >= kTrafficGoMs;
    const bool go = at >= kTrafficReadyMs && at < kTrafficGoMs;
    const bool lit[] = {stop, ready, go};
    for (uint32_t i = 0; i < leds.size(); ++i) {
        leds[i] = lit[i % 3] ? layer.col[i % 3] : Rgb{0, 0, 0};
    }
}

// Pairs of eyes opening at random places, blinking once and closing. Needs no memory. ix
// sets how many pairs there can be.
void run_eyes(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    std::ranges::fill(leds, Rgb{0, 0, 0});
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t pairs = 1u + layer.ix / 64u;
    for (uint32_t pair = 0; pair < pairs; ++pair) {
        const uint32_t identity = mix(pair + 1);
        const uint32_t period = kEyesMinPeriodMs + identity % kEyesPeriodSpreadMs;
        const uint32_t shifted = now + (identity >> 8) % period;
        const uint32_t age_ms = shifted % period;
        const uint32_t open_ms = period * 3 / 5;
        if (age_ms >= open_ms) {
            continue;
        }
        const uint32_t seed = mix(identity + shifted / period);
        const uint32_t blink_at = open_ms / 3 + (seed >> 8) % (open_ms / 3);
        if (age_ms >= blink_at && age_ms < blink_at + kEyesBlinkMs) {
            continue;
        }
        uint32_t level = 255;
        if (age_ms < kEyesOpenMs) {
            level = perceptual(age_ms * 255 / kEyesOpenMs);
        } else if (age_ms + kEyesCloseMs > open_ms) {
            level = perceptual((open_ms - age_ms) * 255 / kEyesCloseMs);
        }
        const Rgb color = scaled(palette_color(layer.palette, static_cast<uint8_t>(seed >> 16)), level);
        const uint32_t place = seed % (count > kEyesGap ? count - kEyesGap : 1);
        leds[place] = brighter_of(leds[place], color);
        if (place + kEyesGap < count) {
            leds[place + kEyesGap] = brighter_of(leds[place + kEyesGap], color);
        }
    }
}

// Patches of col[0] on col[1], at a distance set by ix. With breathing they swell and shrink.
void run_spots(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds, bool breathing) {
    const uint32_t distance = 3u + layer.ix / 16u;
    const uint32_t threshold = breathing ? wave(paced_time(time_ms, layer.sx) / 16) * 230u / 255u : 128u;
    const Rgb color = palette_color(layer.palette, 0);
    const Rgb background = background_of(layer);
    for (uint32_t i = 0; i < leds.size(); ++i) {
        const uint32_t height = wave(i * 256 / distance + 64);
        const uint32_t level = height > threshold ? (height - threshold) * 255 / (255 - threshold) : 0;
        leds[i] = mixed(background, color, perceptual(level));
    }
}

// Every LED stays lit and dips at its own rhythm. ix is the level it dips to.
void run_shimmer(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t now = paced_time(time_ms, layer.sx);
    for (uint32_t i = 0; i < leds.size(); ++i) {
        const uint32_t identity = mix(i + 1);
        const uint32_t height = wave(now / (8 + identity % 16) + (identity >> 8));
        const Rgb color = layer.explicit_palette ? palette_color(layer.palette, static_cast<uint8_t>(identity >> 16))
                                                 : palette_color(layer.palette, 0);
        leds[i] = scaled(color, layer.ix + (255u - layer.ix) * perceptual(height) / 255);
    }
}

// Like twinkle, but an LED rises slowly and goes out quickly. ix is the share of LEDs lit.
void run_twinkle_up(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t now = paced_time(time_ms, layer.sx);
    for (uint32_t i = 0; i < leds.size(); ++i) {
        const uint32_t identity = mix(i + 1);
        const uint32_t period = kTwinkleUpMinPeriodMs + identity % kTwinkleUpSpreadMs;
        const uint32_t shifted = now + (identity >> 8) % period;
        const uint32_t seed = mix(identity + shifted / period);
        if ((seed & 0xFF) >= layer.ix) {
            leds[i] = Rgb{0, 0, 0};
            continue;
        }
        const uint32_t age_ms = shifted % period;
        const uint32_t rise_ms = period * 4 / 5;
        const uint32_t level = age_ms < rise_ms ? age_ms * 255 / rise_ms : (period - age_ms) * 255 / (period - rise_ms);
        leds[i] = scaled(palette_color(layer.palette, static_cast<uint8_t>(seed >> 8)), perceptual(level));
    }
}

// The chain as a row of icicles, ix setting their length, 4 to 11 LEDs: from each in its own
// time a drop runs down, gathering speed, over col[1] if there is one.
void run_icicle(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t length = 4u + layer.ix / 32u;
    const Rgb background = background_of(layer);
    const Rgb color = palette_color(layer.palette, 0);
    for (uint32_t i = 0; i < leds.size(); ++i) {
        const uint32_t identity = mix(i / length + 1);
        const uint32_t period = kIcicleMinPeriodMs + identity % kIcicleSpreadMs;
        const uint32_t age_ms = (now + (identity >> 8) % period) % period;
        const uint32_t fall_ms = period * 2 / 5;
        const uint64_t head_q8 = uint64_t{length} * 256 * age_ms * age_ms / (uint64_t{fall_ms} * fall_ms);
        const uint64_t led_q8 = uint64_t{i % length} * 256;
        const uint64_t tail_q8 = uint64_t{kIcicleTailLeds} * 256;
        const uint32_t level = led_q8 <= head_q8 && head_q8 - led_q8 < tail_q8
                                   ? 255 - static_cast<uint32_t>((head_q8 - led_q8) * 255 / tail_q8)
                                   : 0;
        leds[i] = mixed(background, color, perceptual(level));
    }
}

// Old bubble lamps: each in its own colour, with a slow swell and a quicker, uneven stir of
// rising bubbles, never going dark.
void run_bubble(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t now = paced_time(time_ms, layer.sx);
    for (uint32_t i = 0; i < leds.size(); ++i) {
        const uint32_t identity = mix(i + 1);
        const uint32_t swell = wave(now / (24 + identity % 24) + (identity >> 8));
        const uint32_t stir = wave(now / (3 + (identity >> 16) % 4) + (identity >> 4));
        const uint32_t level = 90u + swell * 65u / 255u + stir * 100u / 255u;
        leds[i] = scaled(layer.palette.stops[i % layer.palette.count].color, level);
    }
}

// a: place on the palette per LED. At every step some LEDs jump to a new one, ix setting
// the share.
void run_dynamic(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds, AlgorithmState& state, Rng& rng) {
    const auto count = static_cast<uint32_t>(leds.size());
    if (!state.started) {
        state.started = true;
        for (uint32_t i = 0; i < count; ++i) {
            state.a[i] = static_cast<uint8_t>(rng.next());
        }
    }
    for (uint32_t n = steps_to_take(state, paced_time(time_ms, layer.sx) / kDynamicStepMs); n > 0; --n) {
        for (uint32_t i = 0; i < count; ++i) {
            if (rng.below(256) < layer.ix) {
                state.a[i] = static_cast<uint8_t>(rng.next());
            }
        }
    }
    for (uint32_t i = 0; i < count; ++i) {
        leds[i] = palette_color(layer.palette, state.a[i]);
    }
}

// Colour and brightness in waves whose lengths themselves change slowly.
void run_pride(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t hue_spread = 3u + wave(now / 128) / 32u;
    const uint32_t glow_spread = 5u + wave(now / 97) / 48u;
    for (uint32_t i = 0; i < leds.size(); ++i) {
        const uint32_t glow = 64u + wave(i * glow_spread + now / 8) * 191u / 255u;
        leds[i] = scaled(palette_color(layer.palette, static_cast<uint8_t>(i * hue_spread + now / 64)), glow);
    }
}

// Pulses along the chain to a steady beat. ix sets the delay from each LED to the next.
void run_bpm(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t delay = 1u + layer.ix / 32u;
    for (uint32_t i = 0; i < leds.size(); ++i) {
        const uint32_t glow = 64u + wave(now / 4 + 256 * leds.size() - i * delay) * 191u / 255u;
        leds[i] = scaled(palette_color(layer.palette, static_cast<uint8_t>(i * 2 + now / 16)), glow);
    }
}

// Noise picks a place on the palette for each LED. ix sets how fine the grain is.
void run_noise(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t grain = 8u + layer.ix;
    for (uint32_t i = 0; i < leds.size(); ++i) {
        leds[i] = palette_color(layer.palette, noise_field(i * grain, now / 8));
    }
}

// The palette stays along the chain and noise sets the brightness.
void run_noise_glow(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t grain = 8u + layer.ix;
    for (uint32_t i = 0; i < leds.size(); ++i) {
        const Rgb color = palette_color(layer.palette, static_cast<uint8_t>(i * 2 + now / 64));
        leds[i] = scaled(color, perceptual(noise_field(i * grain, now / 8)));
    }
}

// Two waves running against each other pick the colour, a third the brightness.
void run_plasma(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t spread = 2u + layer.ix / 16u;
    for (uint32_t i = 0; i < leds.size(); ++i) {
        const uint32_t hue = (uint32_t{wave(i * spread + now / 16)} + wave(i * spread * 3 / 2 + 512 - now / 23 % 256)) / 2;
        const uint32_t glow = 96u + wave(i * spread / 2 + now / 31) * 159u / 255u;
        leds[i] = scaled(palette_color(layer.palette, static_cast<uint8_t>(hue)), glow);
    }
}

// Brightness where two waves meet. With noise the meeting points also wander.
void run_phased(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds, bool noisy) {
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t spread = 3u + layer.ix / 16u;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t drift = noisy ? noise_line(i * 32 + now / 16, 31) : 0;
        const uint32_t level = uint32_t{wave(i * spread + now / 16 + drift)} * wave(i * spread * 2 + 1024 - now / 24 % 256) / 255;
        leds[i] = scaled(palette_color(layer.palette, static_cast<uint8_t>(i * 255 / count)), level);
    }
}

// A wave of brightness one way, the palette the other. ix sets the wave's length.
void run_sine(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t now = paced_time(time_ms, layer.sx) / 16;
    const uint32_t spread = 2u + layer.ix / 8u;
    for (uint32_t i = 0; i < leds.size(); ++i) {
        const Rgb color = palette_color(layer.palette, static_cast<uint8_t>(i * spread - now));
        leds[i] = scaled(color, perceptual(wave(i * spread + now)));
    }
}

// The palette running out from zones and back in, ix setting how many zones.
void run_flow(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t now = paced_time(time_ms, layer.sx) / 32;
    const uint32_t zones = 1u + layer.ix / 32u;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t place = i * zones * 512 / count & 0x1FF;
        const uint32_t height = place < 256 ? place : 511 - place;
        leds[i] = palette_color(layer.palette, static_cast<uint8_t>(height / 2 + now));
    }
}

// Stripes that keep getting narrower and wider as they move.
void run_flow_stripe(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t spread = 2u + wave(now / 64) / 24u;
    for (uint32_t i = 0; i < leds.size(); ++i) {
        leds[i] = palette_color(layer.palette, static_cast<uint8_t>(i * spread + now / 16));
    }
}

// Soft shadows drifting over the palette. ix sets how many.
void run_shadows(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t shadows = 1u + layer.ix / 48u;
    constexpr uint32_t kWidthQ8 = 4 * 256;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t led_q8 = i * 256;
        uint32_t level = 255;
        for (uint32_t shadow = 0; shadow < shadows; ++shadow) {
            const uint32_t place_q8 = (count - 1) * 256 / 255 * wave(now / (18 + shadow * 5) + shadow * 67);
            const uint32_t distance_q8 = led_q8 > place_q8 ? led_q8 - place_q8 : place_q8 - led_q8;
            if (distance_q8 < kWidthQ8) {
                level = level * (64 + 191 * distance_q8 / kWidthQ8) / 255;
            }
        }
        leds[i] = scaled(palette_color(layer.palette, static_cast<uint8_t>(i * 2 + now / 64)), level);
    }
}

// Light from a television in a dark room: scenes cut from colour to colour and flicker. ix
// sets how deep the flicker goes.
void run_tv(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t seed = mix(now / kTvSceneMs + 1);
    const uint32_t flicker = mix(now / kTvFlickerMs + seed) % (1u + layer.ix * 2u / 3u);
    const uint32_t level = 255 - flicker;
    // One scene in eight is a dark one.
    const uint32_t scene = (seed & 7) == 0 ? level / 3 : level;
    std::ranges::fill(leds, scaled(palette_color(layer.palette, static_cast<uint8_t>(seed >> 8)), scene));
}

}  // namespace

bool run_color_algorithm(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds, AlgorithmState& state, Rng& rng) {
    switch (layer.algorithm) {
        case Algorithm::TrafficLight:
            run_traffic(layer, time_ms, leds);
            return true;
        case Algorithm::Eyes:
            run_eyes(layer, time_ms, leds);
            return true;
        case Algorithm::Spots:
            run_spots(layer, time_ms, leds, false);
            return true;
        case Algorithm::SpotsFade:
            run_spots(layer, time_ms, leds, true);
            return true;
        case Algorithm::Shimmer:
            run_shimmer(layer, time_ms, leds);
            return true;
        case Algorithm::TwinkleUp:
            run_twinkle_up(layer, time_ms, leds);
            return true;
        case Algorithm::Icicle:
            run_icicle(layer, time_ms, leds);
            return true;
        case Algorithm::Bubble:
            run_bubble(layer, time_ms, leds);
            return true;
        case Algorithm::Dynamic:
            run_dynamic(layer, time_ms, leds, state, rng);
            return true;
        case Algorithm::Pride:
            run_pride(layer, time_ms, leds);
            return true;
        case Algorithm::Bpm:
            run_bpm(layer, time_ms, leds);
            return true;
        case Algorithm::Noise:
            run_noise(layer, time_ms, leds);
            return true;
        case Algorithm::NoiseGlow:
            run_noise_glow(layer, time_ms, leds);
            return true;
        case Algorithm::Plasma:
            run_plasma(layer, time_ms, leds);
            return true;
        case Algorithm::Phased:
            run_phased(layer, time_ms, leds, false);
            return true;
        case Algorithm::PhasedNoise:
            run_phased(layer, time_ms, leds, true);
            return true;
        case Algorithm::Sine:
            run_sine(layer, time_ms, leds);
            return true;
        case Algorithm::Flow:
            run_flow(layer, time_ms, leds);
            return true;
        case Algorithm::FlowStripe:
            run_flow_stripe(layer, time_ms, leds);
            return true;
        case Algorithm::Shadows:
            run_shadows(layer, time_ms, leds);
            return true;
        case Algorithm::Tv:
            run_tv(layer, time_ms, leds);
            return true;
        default:
            return false;
    }
}

}  // namespace garland
