/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: An effect file in memory, and the parser that fills it
 * Contributors: None
 */

#pragma once

#include <array>
#include <cstdint>

#include "effects.h"

namespace garland {

constexpr int kMaxLayers = 4;
constexpr int kMaxGradientStops = 16;
constexpr int kMaxLayerColors = 3;
constexpr int kFigurineCount = 8;
constexpr int kMaxEffectIdLength = 32;
constexpr int kMaxEffectNameLength = 32;

enum class Algorithm : uint8_t {
    Solid,
    Fade,
    Chase,
    Fire,
    Candle,
    Twinkle,
    Sparkle,
    Stripes,
    Aurora,
    Rainbow,
    Breathe,
    Wipe,
    Theater,
    Meteor,
    Scanner,
    Colorloop,
    Drift,
    Fireworks,
    Heartbeat,
    Steady,
    Waves,
    Sequential,
    SloGlo,
    Chasing,
    BuildUp,
    CrossFade,
    AllGlow,
    RandomGroup,
    SlowFade,
    Flash,
    Combination,
    Running,
    RandomColors,
    Dissolve,
    BouncingBalls,
    Juggle,
    Ripple,
    Sunrise,
    Drip,
    Rain,
    Bands,
    Saw,
    Sweep,
    SweepRandom,
    WipeRandom,
    Scan,
    Android,
    Band,
    BandRandom,
    ChaseFlash,
    ChaseFlashRandom,
    Stream,
    StreamSmooth,
    Beam,
    Gradient,
    Loading,
    Sinelon,
    PerlinMove,
    Oscillate,
    Icu,
    Tetrix,
    Percent,
    Flock,
    Washing,
    TrafficLight,
    Eyes,
    Spots,
    SpotsFade,
    Shimmer,
    TwinkleUp,
    Icicle,
    Bubble,
    Dynamic,
    Pride,
    Bpm,
    Noise,
    NoiseGlow,
    Plasma,
    Phased,
    PhasedNoise,
    Sine,
    Flow,
    FlowStripe,
    Shadows,
    Tv,
    Starburst,
    Rocket,
    Rolling,
    Popcorn,
    Blink,
    StrobeBurst,
    Alternate,
    Lightning,
    Hourglass,
    Sparkler,
    WaterWave,
    CometCollision,
    FigureParade,
};

enum class Blend : uint8_t { Replace, Add, Max };

enum class ZoneKind : uint8_t { All, Figurines, Every };

struct GradientStop {
    uint8_t position;
    Rgb color;
};

struct Palette {
    std::array<GradientStop, kMaxGradientStops> stops{};
    uint8_t count = 0;
};

struct Layer {
    Algorithm algorithm = Algorithm::Solid;
    uint8_t sx = 128;
    uint8_t ix = 128;
    std::array<Rgb, kMaxLayerColors> col{};
    uint8_t col_count = 0;
    Palette palette;           // Always usable: built from col when the file gives none.
    bool explicit_palette = false;
    uint16_t start = 0;
    uint16_t stop = 0;         // 0 means the end of the chain.
    uint8_t grp = 1;
    uint8_t spc = 0;
    int16_t of = 0;
    bool rev = false;
    bool mi = false;
    uint8_t bri = 255;
    ZoneKind zone = ZoneKind::All;
    uint8_t figurine_mask = 0;
    uint8_t every = 1;
    uint8_t offset = 0;
    Blend blend = Blend::Replace;
};

struct EffectSpec {
    char id[kMaxEffectIdLength + 1] = {};
    char name[kMaxEffectNameLength + 1] = {};
    std::array<Layer, kMaxLayers> layers{};
    uint8_t layer_count = 0;
};

// Fills out from one effect file. A file is accepted whole or not at all: on
// failure returns false and points error at a static description. Unknown fields
// are ignored, so a newer file can load on older firmware.
bool parse_effect(const char* json, const char* id, EffectSpec& out, const char*& error);

}  // namespace garland
