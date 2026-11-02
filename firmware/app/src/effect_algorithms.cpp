/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The animation algorithms effect files refer to by name
 * Contributors: None
 *
 * Written for this project. Other LED projects inspired what these effects look
 * like, but no code here comes from them.
 */

#include "effect_tools.h"

#include <algorithm>

namespace garland {
namespace {

constexpr uint32_t kFadeCycleMs = 4000;
constexpr uint32_t kChaseStepMs = 100;
constexpr uint32_t kChaseTailQ8 = 3 * 256;
constexpr uint32_t kFireStepMs = 24;
constexpr uint32_t kCandleStepMs = 40;
constexpr uint32_t kSparkleStepMs = 16;
constexpr uint32_t kStripeStepMs = 120;
constexpr int32_t kStripeEdgeQ8 = 96;
constexpr uint32_t kTwinkleMinPeriodMs = 1200;
constexpr uint32_t kTwinklePeriodSpreadMs = 1600;
constexpr uint32_t kBreatheCycleMs = 5000;
constexpr uint32_t kWipeStepMs = 80;
constexpr uint32_t kWipeHoldPerIxMs = 16;
constexpr uint32_t kTheaterStepMs = 180;
constexpr uint32_t kMeteorStepMs = 60;
constexpr uint32_t kScannerStepMs = 70;
constexpr uint32_t kColorloopStepMs = 80;
constexpr uint32_t kDriftStepMs = 30;
constexpr uint32_t kBurstMs = 1000;
constexpr uint32_t kBurstFlashMs = 120;
constexpr uint32_t kBurstRadiusQ8 = 3 * 256;
constexpr uint32_t kBurstMinPeriodMs = 1700;
constexpr uint32_t kBurstPeriodSpreadMs = 1300;
constexpr uint32_t kHeartbeatCycleMs = 1400;
constexpr uint32_t kHeartbeatSecondBeatMs = 300;
constexpr uint32_t kHeartbeatPulseMs = 260;
constexpr uint32_t kWavesCycleMs = 4000;
constexpr uint32_t kSequentialStepMs = 500;
constexpr uint32_t kSloGloCycleMs = 6000;
constexpr uint32_t kChasingStepMs = 200;
constexpr uint32_t kBuildUpStepMs = 400;
constexpr uint32_t kCrossFadeStepMs = 900;
constexpr uint32_t kAllGlowRiseMs = 2500;
constexpr uint32_t kAllGlowDarkMs = 700;
constexpr uint32_t kRandomGroupStepMs = 350;
constexpr uint32_t kSlowFadeCycleMs = 5000;
constexpr uint32_t kFlashStepMs = 500;
constexpr uint32_t kCombinationModeMs = 10000;
constexpr uint32_t kRunningStepMs = 6;
constexpr uint32_t kRandomColorsCycleMs = 4000;
constexpr uint32_t kDissolveCycleMs = 6000;
constexpr uint32_t kDissolveSeedStep = 7919;
constexpr uint32_t kBallMinPeriodMs = 1600;
constexpr uint32_t kBallPeriodStepMs = 430;
constexpr uint32_t kRippleMs = 1400;
constexpr uint32_t kRippleMinPeriodMs = 2600;
constexpr uint32_t kRipplePeriodSpreadMs = 1800;
constexpr uint32_t kRippleRadiusQ8 = 10 * 256;
constexpr uint32_t kRippleWidthQ8 = 384;
constexpr uint32_t kDripFormMs = 800;
constexpr uint32_t kDripFallMs = 900;
constexpr uint32_t kDripMinPeriodMs = 2400;
constexpr uint32_t kDripPeriodSpreadMs = 1600;
constexpr uint32_t kDropTail = 3;
constexpr uint32_t kRainStepMs = 70;
constexpr uint32_t kRainMinPeriodMs = 900;
constexpr uint32_t kRainPeriodSpreadMs = 1500;

void run_solid(const Layer& layer, std::span<Rgb> leds) {
    std::ranges::fill(leds, palette_color(layer.palette, 0));
}

void run_fade(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t position = paced_time(time_ms, layer.sx) % kFadeCycleMs;
    const uint32_t half = kFadeCycleMs / 2;
    const uint32_t rising = position < half ? position : kFadeCycleMs - position;
    const uint32_t level = perceptual(rising * 255 / half);
    std::ranges::fill(leds, scaled(palette_color(layer.palette, 0), level));
}

void run_chase(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    std::ranges::fill(leds, Rgb{0, 0, 0});
    if (leds.empty()) {
        return;
    }
    // The head runs from the first LED to the last and starts over. The tail does
    // not wrap around the ends.
    const uint32_t cycle_ms = kChaseStepMs * static_cast<uint32_t>(leds.size());
    const uint32_t head_q8 =
        static_cast<uint32_t>(uint64_t{paced_time(time_ms, layer.sx) % cycle_ms} * 256 / kChaseStepMs);
    const uint32_t head = head_q8 >> 8;
    const Rgb color = palette_color(layer.palette, 0);
    for (uint32_t i = 0; i <= head && i < leds.size(); ++i) {
        const uint32_t distance_q8 = head_q8 - i * 256;
        if (distance_q8 < kChaseTailQ8) {
            leds[i] = scaled(color, perceptual((kChaseTailQ8 - distance_q8) * 255 / kChaseTailQ8));
        }
    }
}

// a: heat per LED. sx sets how fast heat is lost, ix how often new heat appears.
void run_fire(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds, AlgorithmState& state, Rng& rng) {
    const uint32_t count = static_cast<uint32_t>(leds.size());
    for (uint32_t n = steps_to_take(state, time_ms / kFireStepMs); n > 0; --n) {
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t loss = rng.below(2 + layer.sx / 16u);
            state.a[i] = static_cast<uint8_t>(state.a[i] > loss ? state.a[i] - loss : 0);
        }
        // Heat rises away from the first LEDs.
        for (uint32_t i = count; i-- > 1;) {
            state.a[i] = static_cast<uint8_t>((state.a[i] + 3u * state.a[i - 1]) / 4);
        }
        if (rng.below(256) < layer.ix) {
            const uint32_t spot = rng.below(count / 6 + 1);
            state.a[spot] = static_cast<uint8_t>(std::min<uint32_t>(255, state.a[spot] + 120 + rng.below(96)));
        }
    }
    for (uint32_t i = 0; i < count; ++i) {
        leds[i] = palette_color(layer.palette, state.a[i]);
    }
}

// a: current level, b: level it is heading for. ix is the dimmest it gets, so a
// high value gives a calm flame. sx is how fast it flickers.
void run_candle(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds, AlgorithmState& state, Rng& rng) {
    const uint32_t count = static_cast<uint32_t>(leds.size());
    if (!state.started) {
        state.started = true;
        std::fill_n(state.a.begin(), count, uint8_t{255});
        std::fill_n(state.b.begin(), count, uint8_t{255});
    }
    for (uint32_t n = steps_to_take(state, paced_time(time_ms, layer.sx) / kCandleStepMs); n > 0; --n) {
        const uint32_t range = 256u - layer.ix;
        for (uint32_t i = 0; i < count; ++i) {
            if (rng.below(4) == 0) {
                state.b[i] = static_cast<uint8_t>(layer.ix + rng.below(range));
            }
            const int difference = static_cast<int>(state.b[i]) - state.a[i];
            state.a[i] = static_cast<uint8_t>(state.a[i] + (difference + (difference > 0 ? 3 : 0)) / 4);
        }
    }
    const Rgb color = palette_color(layer.palette, 0);
    for (uint32_t i = 0; i < count; ++i) {
        leds[i] = scaled(color, state.a[i]);
    }
}

// Needs no memory: each LED's own period and each cycle's number decide its turn.
// ix is the share of cycles in which an LED lights.
void run_twinkle(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t now = paced_time(time_ms, layer.sx);
    for (uint32_t i = 0; i < leds.size(); ++i) {
        const uint32_t identity = mix(i + 1);
        const uint32_t period = kTwinkleMinPeriodMs + identity % kTwinklePeriodSpreadMs;
        const uint32_t shifted = now + (identity >> 8) % period;
        const uint32_t seed = mix(identity + shifted / period);
        if ((seed & 0xFF) >= layer.ix) {
            leds[i] = Rgb{0, 0, 0};
            continue;
        }
        const uint32_t position = shifted % period;
        const uint32_t half = period / 2;
        const uint32_t rising = position < half ? position : period - position;
        leds[i] = scaled(palette_color(layer.palette, static_cast<uint8_t>(seed >> 8)),
                         perceptual(rising * 255 / half));
    }
}

// a: sparkle level per LED. col[0] is the sparkle, col[1] the background; a palette
// in the file gives each LED its own sparkle colour. ix is how often they appear,
// sx how fast they fade.
void run_sparkle(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds, AlgorithmState& state, Rng& rng) {
    const uint32_t step = time_ms / kSparkleStepMs;
    const uint32_t count = static_cast<uint32_t>(leds.size());
    if (step != state.last_step) {
        const uint32_t steps = std::min<uint32_t>(step - state.last_step, 8);
        state.last_step = step;
        const uint32_t decay = steps * (1 + layer.sx / 16u);
        for (uint32_t i = 0; i < count; ++i) {
            state.a[i] = static_cast<uint8_t>(state.a[i] > decay ? state.a[i] - decay : 0);
            if (rng.below(4096) < layer.ix) {
                state.a[i] = 255;
            }
        }
    }
    const Rgb background = layer.col_count > 1 ? layer.col[1] : Rgb{0, 0, 0};
    for (uint32_t i = 0; i < count; ++i) {
        const Rgb spark = layer.explicit_palette ? palette_color(layer.palette, static_cast<uint8_t>(mix(i + 1)))
                                                 : palette_color(layer.palette, 0);
        leds[i] = mixed(background, spark, perceptual(state.a[i]));
    }
}

// col[0] and col[1] in bands that travel along the chain, ix setting their width.
void run_stripes(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const Rgb first = layer.col_count > 0 ? layer.col[0] : palette_color(layer.palette, 0);
    const Rgb second = layer.col_count > 1 ? layer.col[1] : Rgb{0, 0, 0};
    const uint32_t band_q8 = (1u + layer.ix / 32u) * 256;
    const uint32_t cycle_q8 = band_q8 * 2;
    const uint32_t travel_q8 =
        static_cast<uint32_t>(uint64_t{paced_time(time_ms, layer.sx)} * 256 / kStripeStepMs);
    for (uint32_t i = 0; i < leds.size(); ++i) {
        const uint32_t position = (i * 256 + cycle_q8 - travel_q8 % cycle_q8) % cycle_q8;
        // A triangle crossing mid-level halfway, clipped to full colour: both bands
        // get equal width, with a short soft edge between them.
        const int32_t distance = static_cast<int32_t>(position < band_q8 ? position : cycle_q8 - position);
        const int32_t amount = 127 + (distance - static_cast<int32_t>(band_q8 / 2)) * 255 / kStripeEdgeQ8;
        leds[i] = mixed(second, first, static_cast<uint32_t>(std::clamp<int32_t>(amount, 0, 255)));
    }
}

// Slow waves at different lengths pick the colour and the brightness, and the
// mismatch between them keeps the pattern from repeating visibly. ix sets wave size.
void run_aurora(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t spread = 2u + layer.ix / 8u;
    for (uint32_t i = 0; i < leds.size(); ++i) {
        const uint32_t hue = (wave(i * spread + now / 40) + wave(i * spread * 2 + now / 97 + 80)) / 2;
        const uint32_t glow = wave(i * (spread + 3) + now / 61 + 170);
        leds[i] = scaled(palette_color(layer.palette, static_cast<uint8_t>(hue)), 40 + perceptual(glow) * 215 / 255);
    }
}

// The palette travels along the chain. ix sets how much of it fits on one LED.
void run_rainbow(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t now = paced_time(time_ms, layer.sx) / 16;
    const uint32_t spread = 1u + layer.ix / 8u;
    for (uint32_t i = 0; i < leds.size(); ++i) {
        leds[i] = palette_color(layer.palette, static_cast<uint8_t>(i * spread + now));
    }
}

// Unlike fade, it follows a sine and does not go dark: ix is the level it sinks to.
void run_breathe(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t position = paced_time(time_ms, layer.sx) % kBreatheCycleMs * 256 / kBreatheCycleMs;
    const uint32_t level = layer.ix + (255u - layer.ix) * perceptual(wave(position)) / 255;
    std::ranges::fill(leds, scaled(palette_color(layer.palette, 0), level));
}

// Each palette stop in turn spreads from the first LED to the last over the one
// before it, then holds for a time set by ix.
void run_wipe(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    if (leds.empty()) {
        return;
    }
    const uint32_t sweep_ms = kWipeStepMs * static_cast<uint32_t>(leds.size());
    const uint32_t pass_ms = sweep_ms + layer.ix * kWipeHoldPerIxMs;
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t pass = now / pass_ms;
    const uint32_t head_q8 = std::min(now % pass_ms, sweep_ms) * 256 / kWipeStepMs;
    const uint32_t colors = layer.palette.count > 1 ? layer.palette.count : 2;
    const Rgb current = wipe_color(layer.palette, pass);
    const Rgb previous = wipe_color(layer.palette, pass + colors - 1);
    for (uint32_t i = 0; i < leds.size(); ++i) {
        const uint32_t led_q8 = i * 256;
        const uint32_t reached = head_q8 > led_q8 ? std::min<uint32_t>(head_q8 - led_q8, 256) : 0;
        leds[i] = mixed(previous, current, reached * 255 / 256);
    }
}

// Every n-th LED lit, the pattern stepping along the chain like marquee bulbs. ix
// sets n, from 2 to 5. col[1] is the background; a palette in the file colours the
// LEDs by their place in the chain.
void run_theater(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t period = 2u + layer.ix / 64u;
    const uint32_t phase = paced_time(time_ms, layer.sx) / kTheaterStepMs % period;
    const Rgb background = layer.col_count > 1 ? layer.col[1] : Rgb{0, 0, 0};
    const uint32_t count = static_cast<uint32_t>(leds.size());
    for (uint32_t i = 0; i < count; ++i) {
        if (i % period != phase) {
            leds[i] = background;
        } else if (layer.explicit_palette) {
            leds[i] = palette_color(layer.palette, static_cast<uint8_t>(i * 255 / count));
        } else {
            leds[i] = palette_color(layer.palette, 0);
        }
    }
}

// A head with a long tail. It runs on past the last LED until the tail has left the
// chain too, then starts over. ix sets the tail length.
void run_meteor(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t tail = 2u + layer.ix / 16u;
    const uint32_t tail_q8 = tail * 256;
    const uint32_t cycle_ms = kMeteorStepMs * (static_cast<uint32_t>(leds.size()) + tail);
    const uint32_t head_q8 = paced_time(time_ms, layer.sx) % cycle_ms * 256 / kMeteorStepMs;
    for (uint32_t i = 0; i < leds.size(); ++i) {
        const uint32_t led_q8 = i * 256;
        const uint32_t distance_q8 = head_q8 >= led_q8 ? head_q8 - led_q8 : tail_q8;
        leds[i] = distance_q8 < tail_q8 ? trail_color(layer.palette, distance_q8 * 255 / tail_q8) : Rgb{0, 0, 0};
    }
}

// A light running to the last LED and back. An LED's brightness depends only on how
// long ago the light last passed it, so the tail follows the turn without memory.
// ix sets the tail length.
void run_scanner(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t count = static_cast<uint32_t>(leds.size());
    if (count < 2) {
        std::ranges::fill(leds, palette_color(layer.palette, 0));
        return;
    }
    const uint32_t cycle_ms = 2 * (count - 1) * kScannerStepMs;
    const uint32_t tail_ms = (2u + layer.ix / 16u) * kScannerStepMs;
    const uint32_t now = paced_time(time_ms, layer.sx) % cycle_ms;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t forward_ms = i * kScannerStepMs;
        const uint32_t back_ms = cycle_ms - forward_ms;
        const uint32_t age_ms =
            std::min((now + cycle_ms - forward_ms) % cycle_ms, (now + cycle_ms - back_ms) % cycle_ms);
        leds[i] = age_ms < tail_ms ? trail_color(layer.palette, age_ms * 255 / tail_ms) : Rgb{0, 0, 0};
    }
}

// The whole chain travels through the palette. ix shifts each LED from the one before
// it; 0 keeps them all the same.
void run_colorloop(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t now = paced_time(time_ms, layer.sx) / kColorloopStepMs;
    for (uint32_t i = 0; i < leds.size(); ++i) {
        leds[i] = palette_color(layer.palette, static_cast<uint8_t>(now + i * layer.ix / 64u));
    }
}

// a: place on the palette per LED, b: place it is heading for, the short way round.
// ix is how often an LED picks a new place.
void run_drift(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds, AlgorithmState& state, Rng& rng) {
    const uint32_t count = static_cast<uint32_t>(leds.size());
    if (!state.started) {
        state.started = true;
        for (uint32_t i = 0; i < count; ++i) {
            state.a[i] = static_cast<uint8_t>(rng.next());
            state.b[i] = state.a[i];
        }
    }
    for (uint32_t n = steps_to_take(state, paced_time(time_ms, layer.sx) / kDriftStepMs); n > 0; --n) {
        for (uint32_t i = 0; i < count; ++i) {
            if (rng.below(8192) < layer.ix) {
                state.b[i] = static_cast<uint8_t>(rng.next());
            }
            const auto difference = static_cast<int8_t>(state.b[i] - state.a[i]);
            state.a[i] = static_cast<uint8_t>(state.a[i] + (difference > 0) - (difference < 0));
        }
    }
    for (uint32_t i = 0; i < count; ++i) {
        leds[i] = palette_color(layer.palette, state.a[i]);
    }
}

// Needs no memory: each burst's number decides its place and colour. A burst starts
// as a white flash at one LED, opens out and fades. ix sets how many run at once.
void run_fireworks(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    std::ranges::fill(leds, Rgb{0, 0, 0});
    const uint32_t count = static_cast<uint32_t>(leds.size());
    if (count == 0) {
        return;
    }
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t slots = 1u + layer.ix / 48u;
    for (uint32_t slot = 0; slot < slots; ++slot) {
        const uint32_t identity = mix(slot + 1);
        const uint32_t period = kBurstMinPeriodMs + identity % kBurstPeriodSpreadMs;
        const uint32_t shifted = now + (identity >> 8) % period;
        const uint32_t age_ms = shifted % period;
        if (age_ms >= kBurstMs) {
            continue;
        }
        const uint32_t seed = mix(identity + shifted / period);
        const uint32_t center = seed % count;
        const uint32_t whiteness = age_ms < kBurstFlashMs ? (kBurstFlashMs - age_ms) * 255 / kBurstFlashMs : 0;
        const Rgb color = mixed(palette_color(layer.palette, static_cast<uint8_t>(seed >> 8)), Rgb{255, 255, 255}, whiteness);
        const uint32_t remaining = perceptual(255 - age_ms * 255 / kBurstMs);
        const uint32_t radius_q8 = kBurstRadiusQ8 * (255 - remaining) / 255;
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t distance_q8 = (i > center ? i - center : center - i) * 256;
            if (distance_q8 > radius_q8) {
                continue;
            }
            const Rgb light = scaled(color, remaining * (255 - distance_q8 * 200 / kBurstRadiusQ8) / 255);
            leds[i] = Rgb{std::max(leds[i].r, light.r), std::max(leds[i].g, light.g), std::max(leds[i].b, light.b)};
        }
    }
}

uint32_t heartbeat_pulse(uint32_t age_ms) {
    return age_ms < kHeartbeatPulseMs ? perceptual(255 - age_ms * 255 / kHeartbeatPulseMs) : 0;
}

// A strong beat and a weaker one, then a rest. ix is the level between beats.
void run_heartbeat(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t position = paced_time(time_ms, layer.sx) % kHeartbeatCycleMs;
    const uint32_t first = heartbeat_pulse(position);
    const uint32_t second =
        position >= kHeartbeatSecondBeatMs ? heartbeat_pulse(position - kHeartbeatSecondBeatMs) * 3 / 4 : 0;
    const uint32_t level = std::max({uint32_t{layer.ix}, first, second});
    std::ranges::fill(leds, scaled(palette_color(layer.palette, 0), level));
}

// The modes of the classic 8-function garland controller. Its bulbs are wired in groups
// that it drives together, and each bulb has its own fixed colour.

// Rises from 0 to 255 and back over the cycle, evenly to the eye.
uint32_t glow_level(uint32_t time_ms, uint32_t cycle_ms) {
    const uint32_t position = time_ms % cycle_ms;
    const uint32_t half = cycle_ms / 2;
    return perceptual((position < half ? position : cycle_ms - position) * 255 / half);
}

uint32_t classic_level(Algorithm mode, uint32_t group, uint32_t groups, uint32_t now) {
    switch (mode) {
        case Algorithm::Waves:
            // Each group rises and falls in its own turn.
            return glow_level(now + group * kWavesCycleMs / groups, kWavesCycleMs);
        case Algorithm::Sequential: {
            // On one group after another, then off in the same order.
            const uint32_t step = now / kSequentialStepMs % (groups * 2);
            return (step < groups ? group <= step : group > step - groups) ? 255 : 0;
        }
        case Algorithm::SloGlo:
            return glow_level(now, kSloGloCycleMs);
        case Algorithm::Chasing:
            return group == now / kChasingStepMs % groups ? 255 : 0;
        case Algorithm::BuildUp:
            // On one group after another, staying on, then all off at once.
            return group < now / kBuildUpStepMs % (groups + 1) ? 255 : 0;
        case Algorithm::CrossFade: {
            // Each group hands over to the next, so one is always on its way in as another leaves.
            const uint32_t place = now % (kCrossFadeStepMs * groups);
            const uint32_t peak = group * kCrossFadeStepMs;
            const uint32_t ahead = (place + kCrossFadeStepMs * groups - peak) % (kCrossFadeStepMs * groups);
            const uint32_t distance = std::min(ahead, kCrossFadeStepMs * groups - ahead);
            return distance < kCrossFadeStepMs ? perceptual(255 - distance * 255 / kCrossFadeStepMs) : 0;
        }
        case Algorithm::AllGlow: {
            // Together up and down, with a moment of dark before the next breath.
            const uint32_t position = now % (kAllGlowRiseMs * 2 + kAllGlowDarkMs);
            return position < kAllGlowRiseMs * 2 ? glow_level(position, kAllGlowRiseMs * 2) : 0;
        }
        case Algorithm::RandomGroup: {
            // Another group each step, worked out from the step alone: even steps choose freely, and
            // an odd one takes what neither neighbour has. Two groups can only take turns.
            const uint32_t step = now / kRandomGroupStepMs;
            if (groups == 2) {
                return group == step % 2 ? 255 : 0;
            }
            uint32_t chosen = mix(step) % groups;
            if (step % 2 == 1) {
                const uint32_t before = mix(step - 1) % groups;
                const uint32_t after = mix(step + 1) % groups;
                while (chosen == before || chosen == after) {
                    chosen = (chosen + 1) % groups;
                }
            }
            return group == chosen ? 255 : 0;
        }
        case Algorithm::SlowFade: {
            // One half fades out as the other fades in, so the garland never goes dark.
            const uint32_t level = glow_level(now, kSlowFadeCycleMs);
            return group % 2 == 0 ? level : 255 - level;
        }
        case Algorithm::Flash:
            return group % 2 == now / kFlashStepMs % 2 ? 255 : 0;
        default:
            return 255;
    }
}

// A bulb's own colour: the palette's stops repeat along the chain. ix sets how many
// groups the bulbs are wired in, from 2 to 5.
void run_classic(const Layer& layer, Algorithm mode, uint32_t now, std::span<Rgb> leds) {
    const uint32_t groups = 2u + layer.ix / 64u;
    for (uint32_t i = 0; i < leds.size(); ++i) {
        leds[i] = scaled(layer.palette.stops[i % layer.palette.count].color, classic_level(mode, i % groups, groups, now));
    }
}

// What the controller's first button position does: all the moving modes in turn.
void run_combination(const Layer& layer, uint32_t now, std::span<Rgb> leds) {
    constexpr Algorithm kModes[] = {Algorithm::Waves,    Algorithm::Sequential, Algorithm::SloGlo,
                                    Algorithm::Chasing,  Algorithm::SlowFade,   Algorithm::Flash};
    constexpr uint32_t kModeCount = sizeof(kModes) / sizeof(kModes[0]);
    run_classic(layer, kModes[now / kCombinationModeMs % kModeCount], now % kCombinationModeMs, leds);
}

// A falling drop's head with its tail behind it. Parts off the chain are left out.
void draw_drop(std::span<Rgb> leds, const Palette& palette, uint32_t head_q8) {
    const uint32_t tail_q8 = kDropTail * 256;
    for (uint32_t i = 0; i < leds.size(); ++i) {
        const uint32_t led_q8 = i * 256;
        if (led_q8 <= head_q8 && head_q8 - led_q8 < tail_q8) {
            leds[i] = brighter_of(leds[i], trail_color(palette, (head_q8 - led_q8) * 255 / tail_q8));
        }
    }
}

// Waves of light travelling along the chain, over col[1] if there is one. ix sets how many
// fit. A palette colours them by their place in the chain.
void run_running(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t travel = paced_time(time_ms, layer.sx) / kRunningStepMs;
    const uint32_t spread = 2u + layer.ix / 8u;
    const uint32_t count = static_cast<uint32_t>(leds.size());
    const Rgb background = background_of(layer);
    for (uint32_t i = 0; i < count; ++i) {
        const Rgb color = palette_color(layer.palette, layer.explicit_palette ? static_cast<uint8_t>(i * 255 / count) : uint8_t{0});
        leds[i] = mixed(background, color, perceptual(wave(i * spread - travel)));
    }
}

// The whole chain fades to a new random place on the palette, then holds it.
void run_random_colors(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t number = now / kRandomColorsCycleMs;
    const uint32_t amount = std::min<uint32_t>(now % kRandomColorsCycleMs * 255 / (kRandomColorsCycleMs / 2), 255);
    const Rgb from = palette_color(layer.palette, static_cast<uint8_t>(mix(number + 1)));
    const Rgb to = palette_color(layer.palette, static_cast<uint8_t>(mix(number + 2)));
    std::ranges::fill(leds, mixed(from, to, amount));
}

// LEDs turn on one by one at random until all are lit, then off the same way. Each LED's
// moment comes from the round's number, so this needs no memory.
void run_dissolve(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t half = kDissolveCycleMs / 2;
    const uint32_t round = now / kDissolveCycleMs;
    const uint32_t position = now % kDissolveCycleMs;
    for (uint32_t i = 0; i < leds.size(); ++i) {
        const uint32_t moment = mix(i + 1 + round * kDissolveSeedStep) % half;
        const bool lit = position < half ? position >= moment : position - half < moment;
        const Rgb color = layer.explicit_palette ? palette_color(layer.palette, static_cast<uint8_t>(mix(i + 1)))
                                                 : palette_color(layer.palette, 0);
        leds[i] = lit ? color : Rgb{0, 0, 0};
    }
}

// Balls rising from the first LED and falling back, each at its own pace. ix sets how many.
void run_bouncing_balls(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    std::ranges::fill(leds, Rgb{0, 0, 0});
    if (leds.empty()) {
        return;
    }
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t balls = 1u + layer.ix / 64u;
    const uint32_t span_q8 = (static_cast<uint32_t>(leds.size()) - 1) * 256;
    for (uint32_t ball = 0; ball < balls; ++ball) {
        const uint64_t period = kBallMinPeriodMs + ball * kBallPeriodStepMs;
        const uint64_t position = now % period;
        const auto height_q8 = static_cast<uint32_t>(span_q8 * 4 * position * (period - position) / (period * period));
        draw_dot(leds, height_q8, layer.palette.stops[ball % layer.palette.count].color);
    }
}

// Dots swinging along the chain at different paces, crossing each other. ix sets how many.
void run_juggle(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    std::ranges::fill(leds, Rgb{0, 0, 0});
    if (leds.empty()) {
        return;
    }
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t dots = 1u + layer.ix / 43u;
    const uint32_t span_q8 = (static_cast<uint32_t>(leds.size()) - 1) * 256;
    for (uint32_t dot = 0; dot < dots; ++dot) {
        const uint32_t swing = wave(now / (10 + dot * 4) + dot * 40);
        draw_dot(leds, span_q8 / 255 * swing, palette_color(layer.palette, static_cast<uint8_t>(dot * 255 / dots)));
    }
}

// Rings spreading from random LEDs and fading. Needs no memory, as with fireworks. ix sets
// how many run at once.
void run_ripple(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    std::ranges::fill(leds, Rgb{0, 0, 0});
    const uint32_t count = static_cast<uint32_t>(leds.size());
    if (count == 0) {
        return;
    }
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t slots = 1u + layer.ix / 64u;
    for (uint32_t slot = 0; slot < slots; ++slot) {
        const uint32_t identity = mix(slot + 1);
        const uint32_t period = kRippleMinPeriodMs + identity % kRipplePeriodSpreadMs;
        const uint32_t shifted = now + (identity >> 8) % period;
        const uint32_t age_ms = shifted % period;
        if (age_ms >= kRippleMs) {
            continue;
        }
        const uint32_t seed = mix(identity + shifted / period);
        const uint32_t center = seed % count;
        const Rgb color = palette_color(layer.palette, static_cast<uint8_t>(seed >> 8));
        const uint32_t radius_q8 = age_ms * kRippleRadiusQ8 / kRippleMs;
        const uint32_t remaining = perceptual(255 - age_ms * 255 / kRippleMs);
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t distance_q8 = (i > center ? i - center : center - i) * 256;
            const uint32_t off_ring = distance_q8 > radius_q8 ? distance_q8 - radius_q8 : radius_q8 - distance_q8;
            if (off_ring < kRippleWidthQ8) {
                leds[i] = brighter_of(leds[i], scaled(color, remaining * (kRippleWidthQ8 - off_ring) / kRippleWidthQ8));
            }
        }
    }
}

// Climbs through the palette once and stays at its end, until the effect starts again.
// ix sets how long it takes: ix + 1 seconds at the natural pace.
void run_sunrise(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint64_t duration_ms = (uint64_t{layer.ix} + 1) * 1000;
    const uint64_t position = uint64_t{paced_time(time_ms, layer.sx)} * 255 / duration_ms;
    std::ranges::fill(leds, palette_color(layer.palette, static_cast<uint8_t>(std::min<uint64_t>(position, 255))));
}

// A drop swells on the first LED, then falls faster and faster along the chain. ix sets how
// many fall independently.
void run_drip(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    std::ranges::fill(leds, Rgb{0, 0, 0});
    if (leds.empty()) {
        return;
    }
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t drops = 1u + layer.ix / 64u;
    const uint64_t fall_q8 = (static_cast<uint32_t>(leds.size()) + kDropTail) * 256;
    for (uint32_t drop = 0; drop < drops; ++drop) {
        const uint32_t identity = mix(drop + 1);
        const uint32_t period = kDripMinPeriodMs + identity % kDripPeriodSpreadMs;
        const uint32_t age_ms = (now + (identity >> 8) % period) % period;
        if (age_ms < kDripFormMs) {
            leds[0] = brighter_of(leds[0], scaled(palette_color(layer.palette, 0), perceptual(age_ms * 255 / kDripFormMs)));
            continue;
        }
        const uint64_t falling_ms = age_ms - kDripFormMs;
        if (falling_ms < kDripFallMs) {
            draw_drop(leds, layer.palette, static_cast<uint32_t>(fall_q8 * falling_ms * falling_ms / (kDripFallMs * kDripFallMs)));
        }
    }
}

// Drops starting at random LEDs and running on past the end of the chain or their time,
// whichever comes first. ix sets how many.
void run_rain(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    std::ranges::fill(leds, Rgb{0, 0, 0});
    const uint32_t count = static_cast<uint32_t>(leds.size());
    if (count == 0) {
        return;
    }
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t drops = 2u + layer.ix / 32u;
    for (uint32_t drop = 0; drop < drops; ++drop) {
        const uint32_t identity = mix(drop + 1);
        const uint32_t period = kRainMinPeriodMs + identity % kRainPeriodSpreadMs;
        const uint32_t shifted = now + (identity >> 8) % period;
        const uint32_t start = mix(identity + shifted / period) % count;
        draw_drop(leds, layer.palette, start * 256 + shifted % period * 256 / kRainStepMs);
    }
}

}  // namespace

Rgb palette_color(const Palette& palette, uint8_t position) {
    if (palette.count == 0) {
        return Rgb{0, 0, 0};
    }
    if (position <= palette.stops[0].position) {
        return palette.stops[0].color;
    }
    for (int i = 1; i < palette.count; ++i) {
        const GradientStop& right = palette.stops[i];
        if (position <= right.position) {
            const GradientStop& left = palette.stops[i - 1];
            const uint32_t amount = static_cast<uint32_t>(position - left.position) * 255 /
                                    static_cast<uint32_t>(right.position - left.position);
            return mixed(left.color, right.color, amount);
        }
    }
    return palette.stops[palette.count - 1].color;
}

void run_algorithm(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds, AlgorithmState& state, Rng& rng) {
    // Many algorithms divide by the count: no LEDs, nothing to draw.
    if (leds.empty()) {
        return;
    }
    switch (layer.algorithm) {
        case Algorithm::Solid:
            run_solid(layer, leds);
            break;
        case Algorithm::Fade:
            run_fade(layer, time_ms, leds);
            break;
        case Algorithm::Chase:
            run_chase(layer, time_ms, leds);
            break;
        case Algorithm::Fire:
            run_fire(layer, time_ms, leds, state, rng);
            break;
        case Algorithm::Candle:
            run_candle(layer, time_ms, leds, state, rng);
            break;
        case Algorithm::Twinkle:
            run_twinkle(layer, time_ms, leds);
            break;
        case Algorithm::Sparkle:
            run_sparkle(layer, time_ms, leds, state, rng);
            break;
        case Algorithm::Stripes:
            run_stripes(layer, time_ms, leds);
            break;
        case Algorithm::Aurora:
            run_aurora(layer, time_ms, leds);
            break;
        case Algorithm::Rainbow:
            run_rainbow(layer, time_ms, leds);
            break;
        case Algorithm::Breathe:
            run_breathe(layer, time_ms, leds);
            break;
        case Algorithm::Wipe:
            run_wipe(layer, time_ms, leds);
            break;
        case Algorithm::Theater:
            run_theater(layer, time_ms, leds);
            break;
        case Algorithm::Meteor:
            run_meteor(layer, time_ms, leds);
            break;
        case Algorithm::Scanner:
            run_scanner(layer, time_ms, leds);
            break;
        case Algorithm::Colorloop:
            run_colorloop(layer, time_ms, leds);
            break;
        case Algorithm::Drift:
            run_drift(layer, time_ms, leds, state, rng);
            break;
        case Algorithm::Fireworks:
            run_fireworks(layer, time_ms, leds);
            break;
        case Algorithm::Heartbeat:
            run_heartbeat(layer, time_ms, leds);
            break;
        case Algorithm::Steady:
        case Algorithm::Waves:
        case Algorithm::Sequential:
        case Algorithm::SloGlo:
        case Algorithm::Chasing:
        case Algorithm::BuildUp:
        case Algorithm::CrossFade:
        case Algorithm::AllGlow:
        case Algorithm::RandomGroup:
        case Algorithm::SlowFade:
        case Algorithm::Flash:
            run_classic(layer, layer.algorithm, paced_time(time_ms, layer.sx), leds);
            break;
        case Algorithm::Combination:
            run_combination(layer, paced_time(time_ms, layer.sx), leds);
            break;
        case Algorithm::Running:
            run_running(layer, time_ms, leds);
            break;
        case Algorithm::RandomColors:
            run_random_colors(layer, time_ms, leds);
            break;
        case Algorithm::Dissolve:
            run_dissolve(layer, time_ms, leds);
            break;
        case Algorithm::BouncingBalls:
            run_bouncing_balls(layer, time_ms, leds);
            break;
        case Algorithm::Juggle:
            run_juggle(layer, time_ms, leds);
            break;
        case Algorithm::Ripple:
            run_ripple(layer, time_ms, leds);
            break;
        case Algorithm::Sunrise:
            run_sunrise(layer, time_ms, leds);
            break;
        case Algorithm::Drip:
            run_drip(layer, time_ms, leds);
            break;
        case Algorithm::Rain:
            run_rain(layer, time_ms, leds);
            break;
        default:
            if (!run_motion_algorithm(layer, time_ms, leds, state) && !run_color_algorithm(layer, time_ms, leds, state, rng)) {
                run_burst_algorithm(layer, time_ms, leds);
            }
            break;
    }
}

}  // namespace garland
