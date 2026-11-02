/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Algorithms of bursts, thrown objects, flashes and small shows
 * Contributors: None
 *
 * None of these needs memory: each event's number decides its place, colour and size.
 */

#include <algorithm>
#include <array>
#include <cstdlib>

#include "effect_tools.h"

namespace garland {
namespace {

constexpr uint32_t kStarburstMs = 1100;
constexpr uint32_t kStarburstFlashMs = 100;
constexpr uint32_t kStarburstMinPeriodMs = 1600;
constexpr uint32_t kStarburstPeriodSpreadMs = 1400;
constexpr uint32_t kStarburstFragments = 3;
constexpr uint32_t kRocketRiseMs = 700;
constexpr uint32_t kRocketSparksMs = 1300;
constexpr uint32_t kRocketMinPeriodMs = 3200;
constexpr uint32_t kRocketPeriodSpreadMs = 1800;
constexpr uint32_t kRocketSparks = 4;
constexpr int64_t kRocketSparkSpeedQ8 = 3 * 256;  // Per second, for the slowest spark.
constexpr int64_t kRocketGravityQ8 = 12 * 256;    // Per second, each second.
constexpr uint32_t kRollingMinStepMs = 35;
constexpr uint32_t kRollingStepSpreadMs = 50;
constexpr uint32_t kPopcornMinPeriodMs = 1400;
constexpr uint32_t kPopcornPeriodSpreadMs = 1800;
constexpr uint32_t kPopcornMinFlightMs = 900;
constexpr uint32_t kPopcornFlightSpreadMs = 400;
constexpr uint32_t kBlinkCycleMs = 1000;
constexpr uint32_t kStrobeFlashMs = 90;
constexpr uint32_t kStrobeOnMs = 40;
constexpr uint32_t kStrobePauseMs = 800;
constexpr uint32_t kAlternateStepMs = 110;
constexpr uint32_t kAlternateSteps = 8;
constexpr uint32_t kLightningMinPeriodMs = 2000;
constexpr uint32_t kLightningPeriodSpreadMs = 4000;
constexpr uint32_t kLightningFlashMs = 120;
constexpr uint32_t kLightningOnMs = 50;
constexpr uint32_t kGrainGapMs = 260;
constexpr uint32_t kGrainFallMs = 700;
constexpr uint32_t kHourglassHoldMs = 900;
constexpr uint32_t kSparklerStepMs = 350;
constexpr uint32_t kSparkMinLifeMs = 300;
constexpr uint32_t kSparkLifeSpreadMs = 500;
constexpr uint32_t kWaterBaseLevel = 80;
constexpr uint32_t kWaterDropMinPeriodMs = 2600;
constexpr uint32_t kWaterDropPeriodSpreadMs = 1800;
constexpr uint32_t kWaterDropLifeMs = 2400;
constexpr int32_t kWaterWidthQ8 = 3 * 256;
constexpr int32_t kWaterHeight = 110;
constexpr size_t kMaxWaterDrops = 4;  // 1 + 255 / 64.
constexpr uint32_t kCollisionStepMs = 70;
constexpr uint32_t kCollisionBurstMs = 1100;
constexpr uint32_t kCollisionFlashMs = 120;
constexpr uint32_t kCollisionRestMs = 400;
constexpr uint32_t kCometTail = 5;
constexpr uint32_t kParadeStepMs = 900;

// Fragments flying out both ways from a random LED, slowing and fading. ix sets how many
// bursts run at once.
void run_starburst(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    std::ranges::fill(leds, Rgb{0, 0, 0});
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t bursts = 1u + layer.ix / 64u;
    for (uint32_t burst = 0; burst < bursts; ++burst) {
        const uint32_t identity = mix(burst + 1);
        const uint32_t period = kStarburstMinPeriodMs + identity % kStarburstPeriodSpreadMs;
        const uint32_t shifted = now + (identity >> 8) % period;
        const uint32_t age_ms = shifted % period;
        if (age_ms >= kStarburstMs) {
            continue;
        }
        const uint32_t seed = mix(identity + shifted / period);
        const uint32_t center_q8 = seed % count * 256;
        const uint32_t whiteness = age_ms < kStarburstFlashMs ? (kStarburstFlashMs - age_ms) * 255 / kStarburstFlashMs : 0;
        const Rgb color = scaled(mixed(palette_color(layer.palette, static_cast<uint8_t>(seed >> 8)), Rgb{255, 255, 255}, whiteness),
                                 perceptual(255 - age_ms * 255 / kStarburstMs));
        const uint32_t flown_ms = age_ms * (2 * kStarburstMs - age_ms) / (2 * kStarburstMs);
        for (uint32_t fragment = 1; fragment <= kStarburstFragments; ++fragment) {
            const uint32_t distance_q8 = flown_ms * fragment * 32 / 25;  // 5 LEDs a second per fragment.
            draw_dot(leds, center_q8 + distance_q8, color);
            if (distance_q8 <= center_q8) {
                draw_dot(leds, center_q8 - distance_q8, color);
            }
        }
    }
}

// A rocket climbs from the first LED and bursts into sparks that gravity pulls back. ix
// sets how many fly independently.
void run_rocket(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    std::ranges::fill(leds, Rgb{0, 0, 0});
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t rockets = 1u + layer.ix / 128u;
    const uint64_t span_q8 = (uint64_t{count} - 1) * 256;
    for (uint32_t rocket = 0; rocket < rockets; ++rocket) {
        const uint32_t identity = mix(rocket + 1);
        const uint32_t period = kRocketMinPeriodMs + identity % kRocketPeriodSpreadMs;
        const uint32_t shifted = now + (identity >> 8) % period;
        const uint64_t age_ms = shifted % period;
        const uint32_t seed = mix(identity + shifted / period);
        const uint64_t apex_q8 = span_q8 * (50 + seed % 45) / 100;
        if (age_ms < kRocketRiseMs) {
            const uint64_t climbed = apex_q8 * age_ms * (2 * kRocketRiseMs - age_ms) / (kRocketRiseMs * kRocketRiseMs);
            draw_dot(leds, static_cast<uint32_t>(climbed), Rgb{255, 220, 160});
            continue;
        }
        const int64_t flight_ms = static_cast<int64_t>(age_ms) - kRocketRiseMs;
        if (flight_ms >= kRocketSparksMs) {
            continue;
        }
        const Rgb color = scaled(palette_color(layer.palette, static_cast<uint8_t>(seed >> 8)),
                                 perceptual(255 - static_cast<uint32_t>(flight_ms) * 255 / kRocketSparksMs));
        for (uint32_t spark = 0; spark < kRocketSparks; ++spark) {
            const int64_t speed_q8 = kRocketSparkSpeedQ8 * (spark + 1) * (spark % 2 == 0 ? 1 : -1);
            const int64_t place_q8 = static_cast<int64_t>(apex_q8) + speed_q8 * flight_ms / 1000 -
                                     kRocketGravityQ8 * flight_ms * flight_ms / 2000000;
            if (place_q8 >= 0) {
                draw_dot(leds, static_cast<uint32_t>(place_q8), color);
            }
        }
    }
}

// Balls rolling evenly between the ends, each at its own pace. ix sets how many.
void run_rolling(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    std::ranges::fill(leds, Rgb{0, 0, 0});
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t balls = 1u + layer.ix / 64u;
    for (uint32_t ball = 0; ball < balls; ++ball) {
        const uint32_t identity = mix(ball + 1);
        const uint32_t step_ms = kRollingMinStepMs + identity % kRollingStepSpreadMs;
        draw_dot(leds, bounce_q8(now + (identity >> 8) % 8192, step_ms, count), layer.palette.stops[ball % layer.palette.count].color);
    }
}

// Kernels thrown from the first LED to a random height at random moments. ix sets how many.
void run_popcorn(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    std::ranges::fill(leds, Rgb{0, 0, 0});
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t kernels = 2u + layer.ix / 32u;
    const uint64_t span_q8 = (uint64_t{count} - 1) * 256;
    for (uint32_t kernel = 0; kernel < kernels; ++kernel) {
        const uint32_t identity = mix(kernel + 1);
        const uint32_t period = kPopcornMinPeriodMs + identity % kPopcornPeriodSpreadMs;
        const uint64_t flight_ms = kPopcornMinFlightMs + (identity >> 8) % kPopcornFlightSpreadMs;
        const uint32_t shifted = now + (identity >> 16) % period;
        const uint64_t age_ms = shifted % period;
        if (age_ms >= flight_ms) {
            continue;
        }
        const uint32_t seed = mix(identity + shifted / period);
        const uint64_t top_q8 = span_q8 * (30 + seed % 71) / 100;
        const uint64_t height_q8 = top_q8 * 4 * age_ms * (flight_ms - age_ms) / (flight_ms * flight_ms);
        draw_dot(leds, static_cast<uint32_t>(height_q8), palette_color(layer.palette, static_cast<uint8_t>(seed >> 8)));
    }
}

// The whole chain on and off once a second, ix setting the share of it spent on. A palette
// gives each flash its own colour; col[1] is shown between flashes.
void run_blink(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t on_ms = layer.ix * kBlinkCycleMs / 255;
    if (now % kBlinkCycleMs >= on_ms) {
        std::ranges::fill(leds, background_of(layer));
        return;
    }
    const Rgb color = layer.explicit_palette
                          ? palette_color(layer.palette, static_cast<uint8_t>(mix(now / kBlinkCycleMs + 1)))
                          : palette_color(layer.palette, 0);
    std::ranges::fill(leds, color);
}

// A rattle of quick flashes, then a pause. ix sets how many in a rattle.
void run_strobe_burst(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t rattle_ms = (3u + layer.ix / 32u) * kStrobeFlashMs;
    const uint32_t at = paced_time(time_ms, layer.sx) % (rattle_ms + kStrobePauseMs);
    const bool lit = at < rattle_ms && at % kStrobeFlashMs < kStrobeOnMs;
    std::ranges::fill(leds, lit ? palette_color(layer.palette, 0) : background_of(layer));
}

// Sections of the chain in col[0] and col[1] flash twice each, in turn. ix sets how many
// sections, 2 at 0-63, then two more per step.
void run_alternate(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t sections = 2 * (1u + layer.ix / 64u);
    const uint32_t step = paced_time(time_ms, layer.sx) / kAlternateStepMs % kAlternateSteps;
    const uint32_t side = step / (kAlternateSteps / 2);
    const bool lit = step % 2 == 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t section = i * sections / count % 2;
        leds[i] = lit && section == side ? layer.col[section] : Rgb{0, 0, 0};
    }
}

// Strikes over random stretches of the chain: one to four flashes, the first the strongest.
// ix sets how many storms run at once.
void run_lightning(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    std::ranges::fill(leds, Rgb{0, 0, 0});
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t storms = 1u + layer.ix / 64u;
    for (uint32_t storm = 0; storm < storms; ++storm) {
        const uint32_t identity = mix(storm + 1);
        const uint32_t period = kLightningMinPeriodMs + identity % kLightningPeriodSpreadMs;
        const uint32_t shifted = now + (identity >> 8) % period;
        const uint32_t age_ms = shifted % period;
        const uint32_t seed = mix(identity + shifted / period);
        const uint32_t flash = age_ms / kLightningFlashMs;
        if (flash >= 1 + seed % 4 || age_ms % kLightningFlashMs >= kLightningOnMs) {
            continue;
        }
        const uint32_t level = flash == 0 ? 255 : 110 + mix(seed + flash) % 90;
        const Rgb color = scaled(palette_color(layer.palette, 0), level);
        const uint32_t first = mix(seed + 11) % count;
        const uint32_t length = std::max<uint32_t>(count / 4, 1 + mix(seed + 12) % count);
        for (uint32_t i = first; i < std::min(first + length, count); ++i) {
            leds[i] = brighter_of(leds[i], color);
        }
    }
}

// Grains leave the upper pile one by one, fall faster and faster and stack at the first LED. Then
// the glass turns over, which puts the stack exactly where the upper pile starts. ix sets the
// share of the chain that is sand.
void run_hourglass(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    std::ranges::fill(leds, Rgb{0, 0, 0});
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t grains = std::clamp<uint32_t>(count * (uint32_t{layer.ix} + 1) / 256, 1, count);
    const uint32_t cycle_ms = grains * kGrainGapMs + kGrainFallMs + kHourglassHoldMs;
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t at = now % cycle_ms;
    const bool turned = now / cycle_ms % 2 == 1;
    for (uint32_t grain = 0; grain < grains; ++grain) {
        const uint64_t from_q8 = uint64_t{count - grains + grain} * 256;
        const uint64_t to_q8 = uint64_t{grain} * 256;
        const uint64_t left_at = uint64_t{grain} * kGrainGapMs;
        uint64_t place_q8 = at <= left_at ? from_q8 : to_q8;
        if (at > left_at && at < left_at + kGrainFallMs) {
            const uint64_t age = at - left_at;
            place_q8 = from_q8 - (from_q8 - to_q8) * age * age / (uint64_t{kGrainFallMs} * kGrainFallMs);
        }
        const uint64_t shown_q8 = turned ? uint64_t{count - 1} * 256 - place_q8 : place_q8;
        const uint32_t along = grains > 1 ? grain * 255 / (grains - 1) : 0;
        draw_dot(leds, static_cast<uint32_t>(shown_q8), palette_color(layer.palette, static_cast<uint8_t>(along)));
    }
}

// A bright head walks the chain, and each spark flies off from where it was at the spark's birth.
// ix sets how many sparks are in the air.
void run_sparkler(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    std::ranges::fill(leds, Rgb{0, 0, 0});
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t sparks = 4u + layer.ix / 16u;
    for (uint32_t spark = 0; spark < sparks; ++spark) {
        const uint32_t identity = mix(spark + 1);
        const uint32_t life_ms = kSparkMinLifeMs + identity % kSparkLifeSpreadMs;
        const uint32_t shifted = now + (identity >> 8) % life_ms;
        const uint32_t age_ms = shifted % life_ms;
        if (now < age_ms) {
            continue;
        }
        const uint32_t seed = mix(identity + shifted / life_ms);
        const uint32_t origin_q8 = bounce_q8(now - age_ms, kSparklerStepMs, count);
        const uint32_t distance_q8 = (3 + seed % 7) * 256 * age_ms / 1000;  // 3 to 9 LEDs a second.
        const bool ahead = (seed >> 8) % 2 == 0;
        if (!ahead && distance_q8 > origin_q8) {
            continue;
        }
        const Rgb color = scaled(palette_color(layer.palette, static_cast<uint8_t>(seed >> 16)), perceptual(255 - age_ms * 255 / life_ms));
        draw_dot(leds, ahead ? origin_q8 + distance_q8 : origin_q8 - distance_q8, color);
    }
    draw_dot(leds, bounce_q8(now, kSparklerStepMs, count), Rgb{255, 235, 190});
}

// Drops fall on still water, and the rings spread, pass through each other and come back from the
// ends, sinking away. The height of the water picks a place on the palette. ix sets how many drops.
void run_water_wave(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const auto count = static_cast<int32_t>(leds.size());
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t drops = 1u + layer.ix / 64u;
    const int32_t span_q8 = (count - 1) * 256;
    std::array<int32_t, kMaxWaterDrops> center_q8{};
    std::array<int32_t, kMaxWaterDrops> front_q8{};
    std::array<int32_t, kMaxWaterDrops> height{};
    for (uint32_t drop = 0; drop < drops; ++drop) {
        const uint32_t identity = mix(drop + 1);
        const uint32_t period = kWaterDropMinPeriodMs + identity % kWaterDropPeriodSpreadMs;
        const uint32_t shifted = now + (identity >> 8) % period;
        const uint32_t age_ms = shifted % period;
        center_q8[drop] = static_cast<int32_t>(mix(identity + shifted / period) % static_cast<uint32_t>(count)) * 256;
        front_q8[drop] = static_cast<int32_t>(age_ms * 9 * 256 / 1000);  // 9 LEDs a second.
        height[drop] = age_ms < kWaterDropLifeMs ? kWaterHeight * static_cast<int32_t>(kWaterDropLifeMs - age_ms) / static_cast<int32_t>(kWaterDropLifeMs) : 0;
    }
    for (int32_t led = 0; led < count; ++led) {
        int32_t level = kWaterBaseLevel;
        for (uint32_t drop = 0; drop < drops; ++drop) {
            // Each end sends the ring back as if a second drop had fallen beyond it.
            for (const int32_t source_q8 : {center_q8[drop], -center_q8[drop], 2 * span_q8 - center_q8[drop]}) {
                const int32_t off_q8 = std::abs(std::abs(led * 256 - source_q8) - front_q8[drop]);
                if (off_q8 < kWaterWidthQ8) {
                    const int32_t crest = static_cast<int32_t>(wave(static_cast<uint32_t>(64 + off_q8 * 256 / kWaterWidthQ8))) - 127;
                    level += height[drop] * crest / 127 * (kWaterWidthQ8 - off_q8) / kWaterWidthQ8;
                }
            }
        }
        leds[static_cast<size_t>(led)] = palette_color(layer.palette, static_cast<uint8_t>(std::clamp<int32_t>(level, 0, 255)));
    }
}

// Two comets, in the first two colours, run in from the ends, meet somewhere near the middle and
// burst. ix sets how many fragments fly each way.
void run_comet_collision(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    std::ranges::fill(leds, Rgb{0, 0, 0});
    const auto count = static_cast<uint32_t>(leds.size());
    const uint32_t approach_ms = count * kCollisionStepMs / 2 + kCollisionStepMs;
    const uint32_t cycle_ms = approach_ms + kCollisionBurstMs + kCollisionRestMs;
    const uint32_t now = paced_time(time_ms, layer.sx);
    const uint32_t at = now % cycle_ms;
    const uint64_t span_q8 = uint64_t{count - 1} * 256;
    const uint32_t seed = mix(now / cycle_ms + 1);
    const uint64_t meet_q8 = span_q8 * (35 + seed % 31) / 100;
    const Rgb left_color = layer.palette.stops[0].color;
    const Rgb right_color = layer.palette.stops[layer.palette.count > 1 ? 1 : 0].color;
    if (at < approach_ms) {
        const uint64_t left_q8 = meet_q8 * at / approach_ms;
        const uint64_t right_q8 = span_q8 - (span_q8 - meet_q8) * at / approach_ms;
        for (uint32_t back = 0; back < kCometTail; ++back) {
            const uint32_t fade = perceptual(255 - back * 255 / kCometTail);
            if (left_q8 >= uint64_t{back} * 256) {
                draw_dot(leds, static_cast<uint32_t>(left_q8 - uint64_t{back} * 256), scaled(left_color, fade));
            }
            draw_dot(leds, static_cast<uint32_t>(right_q8 + uint64_t{back} * 256), scaled(right_color, fade));
        }
        return;
    }
    const uint32_t age_ms = at - approach_ms;
    if (age_ms >= kCollisionBurstMs) {
        return;
    }
    const uint32_t whiteness = age_ms < kCollisionFlashMs ? (kCollisionFlashMs - age_ms) * 255 / kCollisionFlashMs : 0;
    const uint32_t level = perceptual(255 - age_ms * 255 / kCollisionBurstMs);
    const uint32_t flown_ms = age_ms * (2 * kCollisionBurstMs - age_ms) / (2 * kCollisionBurstMs);
    const uint32_t fragments = 2u + layer.ix / 64u;
    const auto center_q8 = static_cast<uint32_t>(meet_q8);
    draw_dot(leds, center_q8, scaled(mixed(left_color, Rgb{255, 255, 255}, whiteness), level));
    for (uint32_t fragment = 1; fragment <= fragments; ++fragment) {
        const uint32_t distance_q8 = flown_ms * fragment * 32 / 25;  // 5 LEDs a second per fragment.
        const Rgb color = scaled(mixed(fragment % 2 == 0 ? left_color : right_color, Rgb{255, 255, 255}, whiteness), level);
        draw_dot(leds, center_q8 + distance_q8, color);
        if (distance_q8 <= center_q8) {
            draw_dot(leds, center_q8 - distance_q8, color);
        }
    }
}

// One type of figurine at a time rises and hands over to the next, each in its own palette stop.
// ix keeps the others glowing at that level. Types follow the layout zones use.
void run_figure_parade(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    const uint32_t cycle_ms = kParadeStepMs * kFigurineCount;
    const uint32_t at = paced_time(time_ms, layer.sx) % cycle_ms;
    const uint32_t resting = perceptual(layer.ix);
    for (uint32_t led = 0; led < leds.size(); ++led) {
        const uint32_t figurine = led % kFigurineCount;
        const uint32_t ahead = (at + cycle_ms - figurine * kParadeStepMs) % cycle_ms;
        const uint32_t rise = ahead < kParadeStepMs       ? ahead * 255 / kParadeStepMs
                              : ahead < 2 * kParadeStepMs ? 255 - (ahead - kParadeStepMs) * 255 / kParadeStepMs
                                                          : 0;
        leds[led] = scaled(layer.palette.stops[figurine % layer.palette.count].color, std::max(resting, perceptual(rise)));
    }
}

}  // namespace

bool run_burst_algorithm(const Layer& layer, uint32_t time_ms, std::span<Rgb> leds) {
    switch (layer.algorithm) {
        case Algorithm::Starburst:
            run_starburst(layer, time_ms, leds);
            return true;
        case Algorithm::Rocket:
            run_rocket(layer, time_ms, leds);
            return true;
        case Algorithm::Rolling:
            run_rolling(layer, time_ms, leds);
            return true;
        case Algorithm::Popcorn:
            run_popcorn(layer, time_ms, leds);
            return true;
        case Algorithm::Blink:
            run_blink(layer, time_ms, leds);
            return true;
        case Algorithm::StrobeBurst:
            run_strobe_burst(layer, time_ms, leds);
            return true;
        case Algorithm::Alternate:
            run_alternate(layer, time_ms, leds);
            return true;
        case Algorithm::Lightning:
            run_lightning(layer, time_ms, leds);
            return true;
        case Algorithm::Hourglass:
            run_hourglass(layer, time_ms, leds);
            return true;
        case Algorithm::Sparkler:
            run_sparkler(layer, time_ms, leds);
            return true;
        case Algorithm::WaterWave:
            run_water_wave(layer, time_ms, leds);
            return true;
        case Algorithm::CometCollision:
            run_comet_collision(layer, time_ms, leds);
            return true;
        case Algorithm::FigureParade:
            run_figure_parade(layer, time_ms, leds);
            return true;
        default:
            return false;
    }
}

}  // namespace garland
