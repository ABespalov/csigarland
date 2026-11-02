/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Algorithms where light travels along the chain
 * Contributors: None
 */

#include <algorithm>

#include "effect_tools.h"

namespace garland {
namespace {

constexpr uint32_t kBandsStepMs = 150;
constexpr uint32_t kSawStepMs = 70;
constexpr uint32_t kSweepStepMs = 60;
constexpr uint32_t kSweepHoldPerIxMs = 12;
constexpr uint32_t kScanStepMs = 60;
constexpr uint32_t kAndroidStepMs = 40;
constexpr uint32_t kBandStepMs = 70;
constexpr uint32_t kChaseFlashStepMs = 400;
constexpr uint32_t kChaseFlashOnMs = 50;
constexpr uint32_t kChaseFlashSecondMs = 100;
constexpr uint32_t kStreamStepMs = 90;
constexpr uint32_t kBeamStepMs = 90;
constexpr uint32_t kLoadingStepMs = 60;
constexpr uint32_t kLoadingHoldMs = 300;
constexpr uint32_t kLoadingFadeMinMs = 200;
constexpr uint32_t kLoadingFadePerIxMs = 8;
constexpr uint32_t kTrailStepMs = 16;
constexpr uint32_t kTrailMaxSteps = 8;
constexpr uint32_t kOscillateStepMs = 60;
constexpr uint32_t kOscillateStepSpreadMs = 23;
constexpr uint32_t kOscillateWidth = 3;
constexpr uint32_t kIcuCycleMs = 1600;
constexpr uint32_t kIcuMoveMs = 400;
constexpr uint32_t kIcuBlinkAtMs = 1200;
constexpr uint32_t kIcuBlinkMs = 120;
constexpr uint32_t kTetrixFallMs = 500;
constexpr uint32_t kTetrixClearMs = 1000;

// Palette stops as runs of equal length, repeating along the chain. ix sets the length,
// 1 + ix/32 LEDs, and sx 0 keeps them still. A stop's position does not matter here.
void run_bands(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t run = 1u + layer.ix / 32u;
    const uint32_t cycle = run * layer.palette.count;
    const uint32_t travel = layer.sx == 0 ? 0 : paced_time(time_ms, layer.sx) / kBandsStepMs % cycle;
    for (uint32_t i = 0; i < leds.size(); ++i) {
        leds[i] = layer.palette.stops[(i + cycle - travel) % cycle / run].color;
    }
}

// Heads with a fading trail, one after another with no gap. ix sets the distance between
// them. A palette gives each its own colour; col[1] is the level the trail fades to.
void run_saw(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint64_t length_q8 = (3u + layer.ix / 16u) * 256;
    const uint64_t travel_q8 = uint64_t{paced_time(time_ms, layer.sx)} * 256 / kSawStepMs;
    const uint64_t start_q8 = length_q8 * leds.size();  // Keeps every LED's offset above 0.
    const Rgb background = background_of(layer);
    for (uint32_t i = 0; i < leds.size(); ++i) {
        const uint64_t offset = travel_q8 + start_q8 - i * 256;
        const auto level = static_cast<uint32_t>(255 - offset % length_q8 * 255 / length_q8);
        const Rgb head = layer.explicit_palette
                             ? palette_color(layer.palette, static_cast<uint8_t>(mix(static_cast<uint32_t>(offset / length_q8))))
                             : palette_color(layer.palette, 0);
        leds[i] = mixed(background, head, perceptual(level));
    }
}

Rgb sweep_color(const Palette& palette, uint32_t pass, bool random) {
    return random ? palette_color(palette, static_cast<uint8_t>(mix(pass + 1))) : wipe_color(palette, pass);
}

// A new colour spreads over the old one, then holds for a time set by ix. With bounce it
// comes back from the far end on every other pass.
void run_sweep(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds, bool bounce, bool random) {
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t sweep_ms = kSweepStepMs * count;
    const uint32_t pass_ms = sweep_ms + layer.ix * kSweepHoldPerIxMs;
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t pass = now / pass_ms;
    const uint32_t head_q8 = std::min(now % pass_ms, sweep_ms) * 256 / kSweepStepMs;
    const uint32_t colors = layer.palette.count > 1 ? layer.palette.count : 2;
    const Rgb current = sweep_color(layer.palette, pass, random);
    const Rgb previous = sweep_color(layer.palette, random ? pass - 1 : pass + colors - 1, random);
    const bool backwards = bounce && pass % 2 != 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t led_q8 = (backwards ? count - 1 - i : i) * 256;
        const uint32_t reached = head_q8 > led_q8 ? std::min<uint32_t>(head_q8 - led_q8, 256) : 0;
        leds[i] = mixed(previous, current, reached * 255 / 256);
    }
}

// Dots with no trail running to the last LED and back, evenly apart. ix sets how many.
void run_scan(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    std::ranges::fill(leds, background_of(layer));
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t dots = 1u + layer.ix / 64u;
    const uint32_t cycle_ms = 2 * std::max<uint32_t>(count - 1, 1) * kScanStepMs;
    const uint32_t now = paced_time(time_ms, layer.sx);
    for (uint32_t dot = 0; dot < dots; ++dot) {
        draw_dot(leds, bounce_q8(now + dot * cycle_ms / dots, kScanStepMs, count),
                 layer.palette.stops[dot % layer.palette.count].color);
    }
}

// A segment that stretches ahead, then pulls its tail in, and so creeps around the chain.
// ix sets how long it gets.
void run_android(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t longest = std::max<uint32_t>(count * (32u + layer.ix) / 512u, 2);
    const uint32_t tick = paced_time(time_ms, layer.sx) / kAndroidStepMs;
    const uint32_t round = tick / (2 * longest);
    const uint32_t at = tick % (2 * longest);
    const uint32_t tail = (round * longest + (at < longest ? 0 : at - longest)) % count;
    const uint32_t length = at < longest ? at + 1 : 2 * longest - at;
    const Rgb color = palette_color(layer.palette, 0);
    const Rgb background = background_of(layer);
    for (uint32_t i = 0; i < count; ++i) {
        leds[i] = (i + count - tail) % count < length ? color : background;
    }
}

// A band running around the chain over a background. ix sets its width, 1 + ix/16 LEDs. A
// palette is the background, along the chain. With random, the palette picks the band's
// colour for each lap instead.
void run_band(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds, bool random) {
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t width = 1u + layer.ix / 16u;
    const uint32_t travel = paced_time(time_ms, layer.sx) / kBandStepMs;
    const uint32_t head = travel % count;
    Rgb band = layer.col_count > 0 ? layer.col[0] : palette_color(layer.palette, 0);
    if (random) {
        band = palette_color(layer.palette, static_cast<uint8_t>(mix(travel / count + 1)));
    }
    for (uint32_t i = 0; i < count; ++i) {
        if ((i + count - head) % count < width) {
            leds[i] = band;
        } else if (layer.explicit_palette && !random) {
            leds[i] = palette_color(layer.palette, static_cast<uint8_t>(i * 255 / count));
        } else {
            leds[i] = background_of(layer);
        }
    }
}

// A short pair of LEDs flashes twice, then moves on: to the next place over col[0], or
// with random, to a random place in a random colour over darkness.
void run_chase_flash(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds, bool random) {
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t step = now / kChaseFlashStepMs;
    const uint32_t at = now % kChaseFlashStepMs;
    const uint32_t seed = mix(step + 1);
    std::ranges::fill(leds, random || layer.col_count == 0 ? Rgb{0, 0, 0} : layer.col[0]);
    const bool lit = at < kChaseFlashOnMs || (at >= kChaseFlashSecondMs && at < kChaseFlashSecondMs + kChaseFlashOnMs);
    if (!lit) {
        return;
    }
    const uint32_t place = (random ? seed : step) % count;
    const Rgb flash = random ? palette_color(layer.palette, static_cast<uint8_t>(seed >> 8))
                             : (layer.col_count > 1 ? layer.col[1] : Rgb{255, 255, 255});
    leds[place] = flash;
    leds[(place + 1) % count] = flash;
}

// Blocks of random colours travelling along the chain, ix setting their size. Smooth
// shades each one into the next.
void run_stream(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds, bool smooth) {
    const uint64_t block_q8 = (1u + layer.ix / 32u) * 256;
    const uint64_t travel_q8 = uint64_t{paced_time(time_ms, layer.sx)} * 256 / kStreamStepMs;
    const uint64_t start_q8 = block_q8 * leds.size();
    for (uint32_t i = 0; i < leds.size(); ++i) {
        const uint64_t offset = travel_q8 + start_q8 - i * 256;
        const auto number = static_cast<uint32_t>(offset / block_q8);
        const Rgb color = palette_color(layer.palette, static_cast<uint8_t>(mix(number + 1)));
        if (smooth) {
            const Rgb next = palette_color(layer.palette, static_cast<uint8_t>(mix(number + 2)));
            leds[i] = mixed(color, next, static_cast<uint32_t>(offset % block_q8 * 255 / block_q8));
        } else {
            leds[i] = color;
        }
    }
}

// A soft-edged patch of col[0] on col[1]: going around the chain like a lighthouse's beam,
// or with bounce, to the last LED and back. ix sets its width.
void run_beam(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds, bool bounce) {
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t width_q8 = (2u + layer.ix / 16u) * 256;
    const uint32_t round_q8 = count * 256;
    const uint32_t head_q8 = bounce ? bounce_q8(now, kBeamStepMs, count)
                                    : static_cast<uint32_t>(uint64_t{now} * 256 / kBeamStepMs % round_q8);
    const Rgb color = palette_color(layer.palette, 0);
    const Rgb background = background_of(layer);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t led_q8 = i * 256;
        uint32_t distance_q8 = led_q8 > head_q8 ? led_q8 - head_q8 : head_q8 - led_q8;
        if (!bounce) {
            distance_q8 = std::min(distance_q8, round_q8 - distance_q8);
        }
        const uint32_t amount = distance_q8 < width_q8 ? (width_q8 - distance_q8) * 255 / width_q8 : 0;
        leds[i] = mixed(background, color, perceptual(amount));
    }
}

// A bar in the palette's colours fills the chain, then fades out over a time set by ix.
void run_loading(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t fill_ms = count * kLoadingStepMs;
    const uint32_t lit_ms = fill_ms + kLoadingHoldMs;
    const uint32_t fade_ms = kLoadingFadeMinMs + layer.ix * kLoadingFadePerIxMs;
    const uint32_t at = paced_time(time_ms, layer.sx) % (lit_ms + fade_ms);
    const uint32_t level = at < lit_ms ? 255 : perceptual(255 - (at - lit_ms) * 255 / fade_ms);
    const uint32_t head_q8 = std::min(at, fill_ms) * 256 / kLoadingStepMs;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t led_q8 = i * 256;
        const uint32_t reached = head_q8 > led_q8 ? std::min<uint32_t>(head_q8 - led_q8, 256) * 255 / 256 : 0;
        const Rgb color = palette_color(layer.palette, static_cast<uint8_t>(i * 255 / count));
        leds[i] = scaled(color, reached * level / 255);
    }
}

// a: the light an LED has left, b: its place on the palette. Called once per trail step.
void fade_trail(AlgorithmState& state, uint32_t count, uint32_t steps, uint32_t per_step) {
    const uint32_t loss = std::min(steps, kTrailMaxSteps) * per_step;
    for (uint32_t i = 0; i < count; ++i) {
        state.a[i] = static_cast<uint8_t>(state.a[i] > loss ? state.a[i] - loss : 0);
    }
}

void paint_trail(const Layer& layer, std::span<Rgb> leds, const AlgorithmState& state) {
    for (uint32_t i = 0; i < leds.size(); ++i) {
        leds[i] = scaled(palette_color(layer.palette, state.b[i]), perceptual(state.a[i]));
    }
}

// A dot swinging along the chain by a sine, leaving a trail. ix sets the trail's length. A
// palette colours it as time goes by.
void run_sinelon(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds, AlgorithmState& state) {
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t step = now / kTrailStepMs;
    if (step != state.last_step) {
        fade_trail(state, count, step - state.last_step, 1u + (255u - layer.ix) / 16u);
        state.last_step = step;
    }
    // The stretch since the last step, so a fast dot on a long chain leaves no holes.
    const uint32_t here = wave(step) * (count - 1) / 255;
    const uint32_t before = wave(step - 1) * (count - 1) / 255;
    const auto hue = static_cast<uint8_t>(layer.explicit_palette ? now / 32 : 0);
    for (uint32_t i = std::min(here, before); i <= std::max(here, before); ++i) {
        state.a[i] = 255;
        state.b[i] = hue;
    }
    paint_trail(layer, leds, state);
}

// Dots wandering where noise takes them, leaving a short trail. ix sets how many.
void run_perlin_move(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds, AlgorithmState& state) {
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t step = now / kTrailStepMs;
    if (step != state.last_step) {
        fade_trail(state, count, step - state.last_step, 6);
        state.last_step = step;
    }
    const uint32_t dots = 1u + layer.ix / 64u;
    for (uint32_t dot = 0; dot < dots; ++dot) {
        const uint32_t place = noise_line(now / 8, (dot + 1) * 1021) * (count - 1) / 255;
        state.a[place] = 255;
        state.b[place] = static_cast<uint8_t>(dot * 255 / dots);
    }
    paint_trail(layer, leds, state);
}

// Three short segments running to the end and back at different paces; where they cross
// their colours add up.
void run_oscillate(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    std::ranges::fill(leds, Rgb{0, 0, 0});
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t now = paced_time(time_ms, layer.sx);
    for (uint32_t segment = 0; segment < 3; ++segment) {
        const Rgb color = layer.palette.stops[segment % layer.palette.count].color;
        const uint32_t start = bounce_q8(now, kOscillateStepMs + segment * kOscillateStepSpreadMs, count) >> 8;
        for (uint32_t i = start; i < std::min(start + kOscillateWidth, count); ++i) {
            leds[i] = Rgb{static_cast<uint8_t>(std::min<uint32_t>(uint32_t{leds[i].r} + color.r, 255)),
                          static_cast<uint8_t>(std::min<uint32_t>(uint32_t{leds[i].g} + color.g, 255)),
                          static_cast<uint8_t>(std::min<uint32_t>(uint32_t{leds[i].b} + color.b, 255))};
        }
    }
}

// A pair of eyes that looks at a random place, blinks, and looks somewhere else.
void run_icu(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    std::ranges::fill(leds, background_of(layer));
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t number = now / kIcuCycleMs;
    const uint32_t at = now % kIcuCycleMs;
    if (at >= kIcuBlinkAtMs && at < kIcuBlinkAtMs + kIcuBlinkMs) {
        return;
    }
    const uint32_t gap = std::max<uint32_t>(count / 4, 2);
    const uint32_t room = count > gap ? count - gap : 1;
    const uint32_t to_q8 = mix(number + 1) % room * 256;
    const uint32_t from_q8 = mix(number) % room * 256;
    const uint32_t moved = std::min(at, kIcuMoveMs);
    const uint32_t place_q8 = (from_q8 * (kIcuMoveMs - moved) + to_q8 * moved) / kIcuMoveMs;
    const Rgb color = palette_color(layer.palette, 0);
    draw_dot(leds, place_q8, color);
    draw_dot(leds, place_q8 + gap * 256, color);
}

// Blocks falling from the far end and stacking from the first LED, until the chain is full
// and fades out. ix sets their size.
void run_tetrix(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    std::ranges::fill(leds, Rgb{0, 0, 0});
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t width = std::min<uint32_t>(1u + layer.ix / 64u, count);
    const uint32_t blocks = count / width;
    const uint32_t falling_ms = blocks * kTetrixFallMs;
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t round = now / (falling_ms + kTetrixClearMs);
    const uint32_t at = now % (falling_ms + kTetrixClearMs);
    const uint32_t landed = std::min(at / kTetrixFallMs, blocks);
    const uint32_t level = at < falling_ms ? 255 : perceptual(255 - (at - falling_ms) * 255 / kTetrixClearMs);
    const auto block_color = [&](uint32_t block) {
        return scaled(palette_color(layer.palette, static_cast<uint8_t>(mix(round * blocks + block))), level);
    };
    for (uint32_t i = 0; i < landed * width; ++i) {
        leds[i] = block_color(i / width);
    }
    if (landed < blocks) {
        const uint64_t top_q8 = uint64_t{count} * 256;
        const uint64_t drop_q8 = top_q8 - landed * width * 256;
        const uint64_t part = at % kTetrixFallMs;
        const auto place_q8 = static_cast<uint32_t>(top_q8 - drop_q8 * part * part / (kTetrixFallMs * kTetrixFallMs));
        for (uint32_t k = 0; k < width; ++k) {
            draw_dot(leds, place_q8 + k * 256, block_color(landed));
        }
    }
}

// Still: the share of the chain set by ix in col[0], the rest in col[1].
void run_percent(const Layer& layer, std::span<Rgb> leds) {
    const uint32_t filled_q8 = layer.ix * static_cast<uint32_t>(leds.size()) * 256 / 255;
    const Rgb color = palette_color(layer.palette, 0);
    const Rgb background = background_of(layer);
    for (uint32_t i = 0; i < leds.size(); ++i) {
        const uint32_t led_q8 = i * 256;
        const uint32_t reached = filled_q8 > led_q8 ? std::min<uint32_t>(filled_q8 - led_q8, 256) : 0;
        leds[i] = mixed(background, color, reached * 255 / 256);
    }
}

// Dots following each other along a sine, like birds. ix sets how many.
void run_flock(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    std::ranges::fill(leds, Rgb{0, 0, 0});
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t dots = 2u + layer.ix / 32u;
    const uint32_t span_q8 = (static_cast<uint32_t>(leds.size()) - 1) * 256;
    for (uint32_t dot = 0; dot < dots; ++dot) {
        const uint32_t swing = wave(now / 12 + dot * 16);
        draw_dot(leds, span_q8 / 255 * swing, palette_color(layer.palette, static_cast<uint8_t>(dot * 255 / dots)));
    }
}

// The palette's pattern turns one way and back, like a drum. ix sets how much fits on an LED.
void run_washing(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t turn = uint32_t{wave(paced_time(time_ms, layer.sx) / 16)} * 2;
    const uint32_t spread = 2u + layer.ix / 16u;
    for (uint32_t i = 0; i < leds.size(); ++i) {
        leds[i] = palette_color(layer.palette, static_cast<uint8_t>(i * spread + turn));
    }
}

}  // namespace

bool run_motion_algorithm(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds, AlgorithmState& state) {
    switch (layer.algorithm) {
        case Algorithm::Bands:
            run_bands(layer, time_ms, leds);
            return true;
        case Algorithm::Saw:
            run_saw(layer, time_ms, leds);
            return true;
        case Algorithm::Sweep:
            run_sweep(layer, time_ms, leds, true, false);
            return true;
        case Algorithm::SweepRandom:
            run_sweep(layer, time_ms, leds, true, true);
            return true;
        case Algorithm::WipeRandom:
            run_sweep(layer, time_ms, leds, false, true);
            return true;
        case Algorithm::Scan:
            run_scan(layer, time_ms, leds);
            return true;
        case Algorithm::Android:
            run_android(layer, time_ms, leds);
            return true;
        case Algorithm::Band:
            run_band(layer, time_ms, leds, false);
            return true;
        case Algorithm::BandRandom:
            run_band(layer, time_ms, leds, true);
            return true;
        case Algorithm::ChaseFlash:
            run_chase_flash(layer, time_ms, leds, false);
            return true;
        case Algorithm::ChaseFlashRandom:
            run_chase_flash(layer, time_ms, leds, true);
            return true;
        case Algorithm::Stream:
            run_stream(layer, time_ms, leds, false);
            return true;
        case Algorithm::StreamSmooth:
            run_stream(layer, time_ms, leds, true);
            return true;
        case Algorithm::Beam:
            run_beam(layer, time_ms, leds, false);
            return true;
        case Algorithm::Gradient:
            run_beam(layer, time_ms, leds, true);
            return true;
        case Algorithm::Loading:
            run_loading(layer, time_ms, leds);
            return true;
        case Algorithm::Sinelon:
            run_sinelon(layer, time_ms, leds, state);
            return true;
        case Algorithm::PerlinMove:
            run_perlin_move(layer, time_ms, leds, state);
            return true;
        case Algorithm::Oscillate:
            run_oscillate(layer, time_ms, leds);
            return true;
        case Algorithm::Icu:
            run_icu(layer, time_ms, leds);
            return true;
        case Algorithm::Tetrix:
            run_tetrix(layer, time_ms, leds);
            return true;
        case Algorithm::Percent:
            run_percent(layer, leds);
            return true;
        case Algorithm::Flock:
            run_flock(layer, time_ms, leds);
            return true;
        case Algorithm::Washing:
            run_washing(layer, time_ms, leds);
            return true;
        default:
            return false;
    }
}

}  // namespace garland
