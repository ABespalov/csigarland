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
// A run's own, made again for each, so that what is random comes out the same from the same file:
// a reset leaves the renderer's random numbers where they were.
std::optional<garland::EffectRenderer> s_runner;
constexpr int kMaxLeds = CSG_MAX_LEDS;
static_assert(kMaxLeds == CSIGARLAND_MAX_LEDS, "the firmware's longest chain");
garland::Rgb s_frame[kMaxLeds];
garland::RgbQ8 s_lit[kMaxLeds];

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

extern "C" void csg_run_start() {
    s_runner.emplace(kSeed);
}

// The loaded effect as the run's renderer draws it, before any brightness.
extern "C" void csg_run_render(uint32_t time_ms, uint8_t* rgb, int count) {
    if (!s_runner) {
        csg_run_start();
    }
    if (count > kMaxLeds) {
        count = kMaxLeds;
    }
    s_runner->render(s_effect, time_ms, std::span<garland::Rgb>(s_frame, static_cast<size_t>(count)));
    for (int i = 0; i < count; ++i) {
        rgb[i * 3] = s_frame[i].r;
        rgb[i * 3 + 1] = s_frame[i].g;
        rgb[i * 3 + 2] = s_frame[i].b;
    }
}

extern "C" void csg_render(uint32_t time_ms, int brightness_percent, uint8_t* rgb, int count) {
    if (count > kMaxLeds) {
        count = kMaxLeds;
    }
    const auto leds = static_cast<size_t>(count);
    s_renderer.render(s_effect, time_ms, std::span<garland::Rgb>(s_frame, leds));
    // The brightness as the Lighting task applies it, with its curve. Not its current limit or dither.
    const auto percent = static_cast<uint8_t>(brightness_percent < 0 ? 0 : (brightness_percent > 100 ? 100 : brightness_percent));
    const uint32_t gain = garland::brightness_gain(percent);
    garland::apply_brightness(std::span<const garland::Rgb>(s_frame, leds), gain, std::span<garland::RgbQ8>(s_lit, leds));
    const float share = static_cast<float>(gain) / static_cast<float>(garland::kFullBrightnessGain);
    const float screen_gain = std::pow(share, 1.0F / garland::kBrightnessGamma);
    for (int i = 0; i < count; ++i) {
        rgb[i * 3] = to_screen(s_frame[i].r, s_lit[i].r, screen_gain);
        rgb[i * 3 + 1] = to_screen(s_frame[i].g, s_lit[i].g, screen_gain);
        rgb[i * 3 + 2] = to_screen(s_frame[i].b, s_lit[i].b, screen_gain);
    }
}
