/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: When the screensaver runs and how its sprites move
 * Contributors: None
 *
 * Ported from the csiPass screensaver by the same author.
 */

#include "screensaver.h"

#include <cstdio>

namespace garland {
namespace {

constexpr uint32_t kBreathRedrawMs = 25;
constexpr uint32_t kStarIntervalMs = 50;
constexpr uint32_t kClockIntervalMs = 60;
constexpr int8_t kSpeed = 2;
constexpr int16_t kStarFarPixels = 3;

// Quarter-wave sin(0..90deg) as 0..127 for angle index 0..64.
constexpr int8_t kSinQuarter[65] = {0,   3,   6,   9,   12,  16,  19,  22,  25,  28,  31,  34,  37,
                                    40,  43,  46,  49,  51,  54,  57,  60,  63,  65,  68,  71,  73,
                                    76,  78,  81,  83,  85,  88,  90,  92,  94,  96,  98,  100, 102,
                                    104, 106, 107, 109, 111, 112, 113, 115, 116, 117, 118, 120, 121,
                                    122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127, 127};

int16_t sin1024(uint16_t angle) {
    const uint16_t wrapped = static_cast<uint16_t>(angle & 1023u);
    const uint8_t quadrant = static_cast<uint8_t>(wrapped >> 8);
    const uint16_t offset = static_cast<uint16_t>(wrapped & 255u);
    const uint16_t position = (quadrant & 1u) != 0 ? static_cast<uint16_t>(256u - offset) : offset;
    const uint8_t index = static_cast<uint8_t>(position >> 2);
    const uint8_t fraction = static_cast<uint8_t>(position & 3u);
    const int16_t v0 = kSinQuarter[index];
    const int16_t v1 = kSinQuarter[index < 64 ? index + 1 : 64];
    const int16_t magnitude = static_cast<int16_t>(((4 - fraction) * v0 + fraction * v1 + 2) / 4);
    return quadrant >= 2 ? static_cast<int16_t>(-magnitude) : magnitude;
}

int16_t cos1024(uint16_t angle) {
    return sin1024(static_cast<uint16_t>(angle + 256u));
}

void update_star_projection(ScreensaverStar& star, uint16_t panel_width, uint16_t panel_height) {
    const int16_t cx = static_cast<int16_t>(panel_width / 2);
    const int16_t cy = static_cast<int16_t>(panel_height / 2);
    const int32_t dx = (static_cast<int32_t>(star.dir_x) * star.dist_q4) / (127 * 16);
    const int32_t dy = (static_cast<int32_t>(star.dir_y) * star.dist_q4) / (127 * 16);
    star.x = static_cast<int16_t>(cx + dx);
    star.y = static_cast<int16_t>(cy + dy);

    const uint16_t half_diag = static_cast<uint16_t>((panel_width + panel_height) / 2 > 0 ? (panel_width + panel_height) / 2 : 1);
    const uint32_t max_dist_q4 = static_cast<uint32_t>(half_diag) * 16u;
    const uint32_t clamped = star.dist_q4 < max_dist_q4 ? star.dist_q4 : max_dist_q4;
    star.brightness = static_cast<uint8_t>(64u + (clamped * 191u) / max_dist_q4);
}

void spawn_star(ScreensaverStar& star, uint32_t& drift, uint16_t panel_width, uint16_t panel_height,
                bool initial_spread) {
    drift = drift * 1664525u + 1013904223u;
    const uint16_t angle = static_cast<uint16_t>((drift >> 16) & 1023u);
    int8_t dx = static_cast<int8_t>(cos1024(angle));
    const int8_t dy = static_cast<int8_t>(sin1024(angle));
    if (dx == 0 && dy == 0) {
        dx = 127;
    }
    star.dir_x = dx;
    star.dir_y = dy;

    drift = drift * 1664525u + 1013904223u;
    star.speed = static_cast<uint8_t>(1u + ((drift >> 16) % 3u));

    const uint16_t min_half = static_cast<uint16_t>((panel_width < panel_height ? panel_width : panel_height) / 2);
    const uint16_t max_initial_px = min_half > 4 ? static_cast<uint16_t>(min_half - 2) : 1;

    drift = drift * 1664525u + 1013904223u;
    if (initial_spread) {
        const uint16_t px = static_cast<uint16_t>(1u + ((drift >> 16) % max_initial_px));
        star.dist_q4 = static_cast<uint16_t>(px * 16u);
    } else {
        const uint16_t spawn_span = max_initial_px < 5 ? max_initial_px : 5;
        star.dist_q4 = static_cast<uint16_t>(16u + ((drift >> 16) % (spawn_span * 16u)));
    }
    update_star_projection(star, panel_width, panel_height);
}

int16_t clamped(int16_t value, int16_t low, int16_t high) {
    return value < low ? low : value > high ? high : value;
}

}  // namespace

uint16_t star_approach_q8(const ScreensaverStar& star, uint32_t max_dist_q4) {
    if (max_dist_q4 == 0 || star.speed == 0 || star.dist_q4 == 0) {
        return 0;
    }
    uint32_t progress = static_cast<uint32_t>(star.dist_q4) * 256u / max_dist_q4;
    if (progress > 256u) {
        progress = 256u;
    }
    const uint32_t layer = star.speed > 3 ? 3u : star.speed;
    return static_cast<uint16_t>(progress * layer / 3u);
}

int16_t star_pixels(uint16_t approach_q8) {
    return static_cast<int16_t>((kStarFarPixels * (256 + static_cast<int32_t>(approach_q8)) + 128) / 256);
}

uint8_t star_brightness(const ScreensaverStar& star, uint16_t approach_q8) {
    const uint32_t scaled = static_cast<uint32_t>(star.brightness) * (512u + approach_q8) / 512u;
    return static_cast<uint8_t>(scaled > 255u ? 255u : scaled);
}

// Folds the period to the rising half so inhale and exhale share one integer path.
// Angle 0..90 deg covers sin from 0 to 1; squaring yields the raised cosine.
uint8_t breath_wave(uint32_t now_ms) {
    constexpr uint32_t kHalf = kBreathPeriodMs / 2U;
    uint32_t t = now_ms % kBreathPeriodMs;
    if (t > kHalf) {
        t = kBreathPeriodMs - t;
    }
    const uint32_t x = (t * 90U + kHalf / 2U) / kHalf;
    if (x == 0U) {
        return 0;
    }
    if (x >= 90U) {
        return 255;
    }
    const uint32_t span = x * (180U - x);
    const uint32_t numerator = 4U * span;
    const uint32_t denominator = 40500U - span;
    const uint32_t sine = (numerator * 255U + denominator / 2U) / denominator;
    return static_cast<uint8_t>((sine * sine + 127U) / 255U);
}

void Screensaver::configure(uint16_t panel_width, uint16_t panel_height) {
    panel_width_ = panel_width;
    panel_height_ = panel_height;
}

void Screensaver::set_sprite_size(uint16_t width, uint16_t height) {
    sprite_width_ = width > panel_width_ ? panel_width_ : width;
    sprite_height_ = height > panel_height_ ? panel_height_ : height;
}

void Screensaver::note_activity(uint64_t now_ms) {
    active_ = false;
    last_activity_ms_ = now_ms;
}

void Screensaver::set_wall_clock(const char* local_time, bool valid) {
    wall_clock_ = valid && local_time != nullptr && local_time[0] != '\0';
    if (wall_clock_) {
        std::snprintf(clock_time_, sizeof(clock_time_), "%s", local_time);
    }
}

void Screensaver::format_uptime(uint64_t now_ms) {
    if (wall_clock_) {
        return;
    }
    const uint32_t total_seconds = static_cast<uint32_t>(now_ms / 1000ULL);
    std::snprintf(clock_time_, sizeof(clock_time_), "%02u:%02u:%02u", static_cast<unsigned>((total_seconds / 3600u) % 100u),
                  static_cast<unsigned>((total_seconds / 60u) % 60u), static_cast<unsigned>(total_seconds % 60u));
}

void Screensaver::activate(uint64_t now_ms) {
    active_ = true;
    last_step_ms_ = now_ms;
    step_x_ = kSpeed;
    step_y_ = kSpeed;
    x_ = static_cast<int16_t>((panel_width_ - sprite_width_) / 2);
    y_ = static_cast<int16_t>((panel_height_ - sprite_height_) / 2);
    if (kind_ == SaverKind::Starfield) {
        for (ScreensaverStar& star : stars_) {
            spawn_star(star, drift_, panel_width_, panel_height_, true);
        }
    } else if (kind_ == SaverKind::FlyingClock) {
        format_uptime(now_ms);
    }
}

void Screensaver::step_bounce() {
    const int16_t maximum_x = static_cast<int16_t>(panel_width_ > sprite_width_ ? panel_width_ - sprite_width_ : 0);
    const int16_t maximum_y = static_cast<int16_t>(panel_height_ > sprite_height_ ? panel_height_ - sprite_height_ : 0);

    int16_t next_x = static_cast<int16_t>(x_ + step_x_);
    int16_t next_y = static_cast<int16_t>(y_ + step_y_);

    bool bounced = false;
    if (next_x < 0 || next_x > maximum_x) {
        step_x_ = static_cast<int8_t>(-step_x_);
        next_x = clamped(static_cast<int16_t>(x_ + step_x_), 0, maximum_x);
        bounced = true;
    }
    if (next_y < 0 || next_y > maximum_y) {
        step_y_ = static_cast<int8_t>(-step_y_);
        next_y = clamped(static_cast<int16_t>(y_ + step_y_), 0, maximum_y);
        bounced = true;
    }
    if (bounced) {
        drift_ = drift_ * 1664525u + 1013904223u;
        const int8_t magnitude = static_cast<int8_t>(1 + ((drift_ >> 16) % kSpeed));
        step_y_ = static_cast<int8_t>(step_y_ < 0 ? -magnitude : magnitude);
    }
    x_ = clamped(next_x, 0, maximum_x);
    y_ = clamped(next_y, 0, maximum_y);
}

void Screensaver::step_starfield() {
    for (ScreensaverStar& star : stars_) {
        // Radial velocity grows with distance from the centre, which gives depth as
        // stars approach the viewer.
        const uint16_t step_q4 = static_cast<uint16_t>(star.speed * (4u + (star.dist_q4 >> 5)));
        star.dist_q4 = static_cast<uint16_t>(star.dist_q4 + step_q4);
        update_star_projection(star, panel_width_, panel_height_);
        if (star.x < 0 || star.x >= static_cast<int16_t>(panel_width_) || star.y < 0 ||
            star.y >= static_cast<int16_t>(panel_height_)) {
            spawn_star(star, drift_, panel_width_, panel_height_, false);
        }
    }
}

ScreensaverView Screensaver::poll(uint64_t now_ms, bool veto) {
    ScreensaverView view;
    const bool was_active = active_;

    if (veto || idle_ms_ == 0 || kind_ == SaverKind::None) {
        active_ = false;
        last_activity_ms_ = now_ms;
        view.changed = was_active;
        return view;
    }
    if (!active_) {
        if (now_ms - last_activity_ms_ < idle_ms_) {
            view.changed = was_active;
            return view;
        }
        activate(now_ms);
        view.changed = true;
    } else if (kind_ == SaverKind::BreathingDot) {
        if (now_ms - last_step_ms_ >= kBreathRedrawMs) {
            last_step_ms_ = now_ms;
            view.changed = true;
        }
    } else if (kind_ == SaverKind::Starfield) {
        if (now_ms - last_step_ms_ >= kStarIntervalMs) {
            last_step_ms_ = now_ms;
            step_starfield();
            view.changed = true;
        }
    } else if (kind_ == SaverKind::FlyingClock) {
        if (now_ms - last_step_ms_ >= kClockIntervalMs) {
            last_step_ms_ = now_ms;
            format_uptime(now_ms);
            step_bounce();
            view.changed = true;
        }
    }
    view.active = active_;
    view.x = x_;
    view.y = y_;
    return view;
}

}  // namespace garland
