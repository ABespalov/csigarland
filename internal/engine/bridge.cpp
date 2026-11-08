/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Builds the firmware's effect engine into the utility, and lets Go call it
 * Contributors: None
 *
 * The firmware's own sources, included rather than copied, as cgo builds only what is in its folder.
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <optional>
#include <span>

#include "bridge.h"

#include "../../firmware/app/src/effect_algorithms.cpp"
#include "../../firmware/app/src/effect_burst.cpp"
#include "../../firmware/app/src/effect_color.cpp"
#include "../../firmware/app/src/effect_motion.cpp"
#include "../../firmware/app/src/effect_parser.cpp"
#include "../../firmware/app/src/effect_renderer.cpp"
#include "../../firmware/app/src/effects.cpp"

namespace {

garland::EffectSpec s_effect;
// A fixed seed: what is random in an effect need not be the same on the garland.
constexpr uint32_t kSeed = 0x47415244;
garland::EffectRenderer s_renderer(kSeed);
constexpr int kMaxLeds = CSG_MAX_LEDS;
static_assert(kMaxLeds == CSIGARLAND_MAX_LEDS, "the firmware's longest chain");
garland::Rgb s_frame[kMaxLeds];
garland::RgbQ8 s_lit[kMaxLeds];
// The garland's power settings, which the current of every preview frame here is worked out by.
garland::PowerSettings s_power;

// A run's whole state, so that one goes on while the preview, or the other run, draws.
struct Run {
    garland::EffectSpec effect{};
    // Made again for each run, so that what is random comes out the same from the same file: a reset
    // leaves the renderer's random numbers where they were.
    std::optional<garland::EffectRenderer> renderer;
    garland::Rgb frame[kMaxLeds]{};
    garland::RgbQ8 lit[kMaxLeds]{};
    garland::PowerSettings power;
};
Run s_runs[CSG_RUNS];

Run* run_of(int run) {
    return run >= 0 && run < CSG_RUNS ? &s_runs[run] : nullptr;
}

int clamp_count(int count) {
    return count < 0 ? 0 : (count > kMaxLeds ? kMaxLeds : count);
}

uint8_t clamp_percent(int percent) {
    return static_cast<uint8_t>(percent < 0 ? 0 : (percent > 100 ? 100 : percent));
}

bool power_is_valid(int supply_ma, int white_ma, int idle_ma, int margin_percent) {
    return garland::supply_ma_is_valid(supply_ma) && garland::led_white_ma_is_valid(white_ma) &&
           garland::led_idle_ma_is_valid(idle_ma) && garland::margin_percent_is_valid(margin_percent);
}

garland::PowerSettings power_of(int supply_ma, int white_ma, int idle_ma, int margin_percent) {
    return garland::PowerSettings{static_cast<uint16_t>(supply_ma), static_cast<uint8_t>(white_ma),
                                  static_cast<uint8_t>(idle_ma), static_cast<uint8_t>(margin_percent)};
}

// The garland's curve turns a percent into a share of light, for an eye; a screen's own gamma does the
// same to a pixel's value. So a channel's colour is scaled by that share taken back through the curve,
// and an LED at 15% looks dimmed to about 15% rather than dark. One the garland sends dark stays dark.
uint8_t to_screen(uint8_t value, uint16_t level_q8, float screen_gain) {
    if (level_q8 == 0) {
        return 0;
    }
    const long scaled = std::lround(static_cast<float>(value) * screen_gain);
    return static_cast<uint8_t>(scaled > 255 ? 255 : scaled);
}

}  // namespace

static bool parse(const char* json, garland::EffectSpec& parsed, char* error, int error_bytes) {
    const char* problem = nullptr;
    if (garland::parse_effect(json, "utility", parsed, problem)) {
        return true;
    }
    std::snprintf(error, static_cast<size_t>(error_bytes), "%s", problem != nullptr ? problem : "not an effect");
    return false;
}

extern "C" int csg_check(const char* json, char* error, int error_bytes) {
    garland::EffectSpec parsed{};
    return parse(json, parsed, error, error_bytes) ? 1 : 0;
}

extern "C" int csg_load(const char* json, char* error, int error_bytes) {
    garland::EffectSpec parsed{};
    if (!parse(json, parsed, error, error_bytes)) {
        return 0;
    }
    s_effect = parsed;
    return 1;
}

// The parser's own lists, for the utility to offer nothing it would refuse.
extern "C" int csg_algorithm_count() {
    return static_cast<int>(std::size(garland::kAlgorithms));
}

extern "C" const char* csg_algorithm_name(int index) {
    return index >= 0 && index < csg_algorithm_count() ? garland::kAlgorithms[index].name : "";
}

extern "C" int csg_figurine_count() {
    return garland::kFigurineCount;
}

extern "C" const char* csg_figurine_name(int index) {
    return index >= 0 && index < garland::kFigurineCount ? garland::kFigurines[index] : "";
}

extern "C" void csg_reset() {
    s_renderer.reset();
}

extern "C" int csg_run_load(int run, const char* json, char* error, int error_bytes) {
    Run* const r = run_of(run);
    if (r == nullptr) {
        std::snprintf(error, static_cast<size_t>(error_bytes), "%s", "no such run");
        return 0;
    }
    garland::EffectSpec parsed{};
    if (!parse(json, parsed, error, error_bytes)) {
        return 0;
    }
    r->effect = parsed;
    r->renderer.emplace(kSeed);
    return 1;
}

// The run's effect as its renderer draws it, before any brightness.
extern "C" void csg_run_render(int run, uint32_t time_ms, uint8_t* rgb, int count) {
    Run* const r = run_of(run);
    if (r == nullptr || !r->renderer) {
        return;
    }
    count = clamp_count(count);
    r->renderer->render(r->effect, time_ms, std::span<garland::Rgb>(r->frame, static_cast<size_t>(count)));
    for (int i = 0; i < count; ++i) {
        rgb[i * 3] = r->frame[i].r;
        rgb[i * 3 + 1] = r->frame[i].g;
        rgb[i * 3 + 2] = r->frame[i].b;
    }
}

extern "C" int csg_run_power(int run, int supply_ma, int white_ma, int idle_ma, int margin_percent) {
    Run* const r = run_of(run);
    if (r == nullptr || !power_is_valid(supply_ma, white_ma, idle_ma, margin_percent)) {
        return 0;
    }
    r->power = power_of(supply_ma, white_ma, idle_ma, margin_percent);
    return 1;
}

extern "C" uint32_t csg_render(uint32_t time_ms, int brightness_percent, uint8_t* rgb, int count) {
    if (count > kMaxLeds) {
        count = kMaxLeds;
    }
    const auto leds = static_cast<size_t>(count);
    s_renderer.render(s_effect, time_ms, std::span<garland::Rgb>(s_frame, leds));
    // The brightness as the Lighting task applies it, with its curve. Not its current limit or dither.
    const uint32_t gain = garland::brightness_gain(clamp_percent(brightness_percent));
    garland::apply_brightness(std::span<const garland::Rgb>(s_frame, leds), gain, std::span<garland::RgbQ8>(s_lit, leds));
    const float share = static_cast<float>(gain) / static_cast<float>(garland::kFullBrightnessGain);
    const float screen_gain = std::pow(share, 1.0F / garland::kBrightnessGamma);
    for (int i = 0; i < count; ++i) {
        rgb[i * 3] = to_screen(s_frame[i].r, s_lit[i].r, screen_gain);
        rgb[i * 3 + 1] = to_screen(s_frame[i].g, s_lit[i].g, screen_gain);
        rgb[i * 3 + 2] = to_screen(s_frame[i].b, s_lit[i].b, screen_gain);
    }
    return garland::estimate_current_ma(std::span<const garland::RgbQ8>(s_lit, leds), garland::current_model(s_power));
}

// The firmware's own model of what a frame of the run draws at a brightness, before its limit holds it back.
extern "C" uint32_t csg_run_current(int run, uint32_t time_ms, int brightness_percent, int count) {
    Run* const r = run_of(run);
    if (r == nullptr || !r->renderer) {
        return 0;
    }
    const auto leds = static_cast<size_t>(clamp_count(count));
    r->renderer->render(r->effect, time_ms, std::span<garland::Rgb>(r->frame, leds));
    garland::apply_brightness(std::span<const garland::Rgb>(r->frame, leds), garland::brightness_gain(clamp_percent(brightness_percent)),
                              std::span<garland::RgbQ8>(r->lit, leds));
    return garland::estimate_current_ma(std::span<const garland::RgbQ8>(r->lit, leds), garland::current_model(r->power));
}

// The firmware's gain for a brightness, of kFullBrightnessGain: what a current at 100% is scaled by.
extern "C" uint32_t csg_brightness_gain(int brightness_percent) {
    return garland::brightness_gain(clamp_percent(brightness_percent));
}

// What a chain of count LEDs draws all dark: the part of the current no brightness scales.
extern "C" uint32_t csg_idle_current(int count) {
    if (count > kMaxLeds) {
        count = kMaxLeds;
    }
    std::fill(s_lit, s_lit + count, garland::RgbQ8{});
    return garland::estimate_current_ma(std::span<const garland::RgbQ8>(s_lit, static_cast<size_t>(count)), garland::current_model(s_power));
}

extern "C" void csg_default_power(int* supply_ma, int* white_ma, int* idle_ma, int* margin_percent) {
    const garland::PowerSettings defaults;
    *supply_ma = defaults.supply_ma;
    *white_ma = defaults.white_ma;
    *idle_ma = defaults.idle_ma;
    *margin_percent = defaults.margin_percent;
}

extern "C" int csg_set_power(int supply_ma, int white_ma, int idle_ma, int margin_percent) {
    if (!power_is_valid(supply_ma, white_ma, idle_ma, margin_percent)) {
        return 0;
    }
    s_power = power_of(supply_ma, white_ma, idle_ma, margin_percent);
    return 1;
}

extern "C" uint32_t csg_power_limit() {
    return garland::power_limit_ma(s_power);
}
