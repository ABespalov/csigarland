/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: When the screensaver runs and how its sprites move
 * Contributors: None
 *
 * Ported from the csiPass screensaver by the same author. No drawing, no clock of
 * its own and no settings store: it is told the time and answers, so its rules can
 * be host tests.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "garland_state.h"

namespace garland {

struct ScreensaverStar {
    int16_t x = 0;
    int16_t y = 0;
    uint16_t dist_q4 = 0;
    uint8_t brightness = 0;
    uint8_t speed = 1;
    int8_t dir_x = 0;
    int8_t dir_y = 0;
};

struct ScreensaverView {
    bool active = false;
    // True when the screensaver started or stopped, or a new frame is due.
    bool changed = false;
    // Top-left of the bouncing clock, or of Scheduler24's two lines, which bounce alike.
    int16_t x = 0;
    int16_t y = 0;
};

// How close a star has come, 0 at the centre and 256 for the fastest star at the
// edge. Slower stars are farther away, so the same travel grows them less.
uint16_t star_approach_q8(const ScreensaverStar& star, uint32_t max_dist_q4);
int16_t star_pixels(uint16_t approach_q8);
uint8_t star_brightness(const ScreensaverStar& star, uint16_t approach_q8);

// 0 at the start of a breath, 255 halfway, over kBreathPeriodMs.
constexpr uint32_t kBreathPeriodMs = 4000;
uint8_t breath_wave(uint32_t now_ms);

class Screensaver {
public:
    static constexpr size_t kStarCount = 48;

    // The sprite never crosses an edge, so its position stays within panel - sprite.
    void configure(uint16_t panel_width, uint16_t panel_height);
    void set_sprite_size(uint16_t width, uint16_t height);
    // Back to the centre for the sprite's size now, as at the start: for words first given their size as the
    // screensaver comes up, after it was centred for the old.
    void recenter();

    // Zero, like SaverKind::None, disables it.
    void set_idle_delay(uint32_t idle_ms) { idle_ms_ = idle_ms; }
    void set_kind(SaverKind kind) { kind_ = kind; }
    SaverKind kind() const { return kind_; }

    // A button press. Stops the screensaver and restarts the idle count.
    void note_activity(uint64_t now_ms);

    // veto holds it off, and the idle count does not run while it does.
    ScreensaverView poll(uint64_t now_ms, bool veto);

    bool active() const { return active_; }
    // Where the sprite stands now, for one drawn again at once after its size changed.
    int16_t x() const { return x_; }
    int16_t y() const { return y_; }
    const ScreensaverStar* stars() const { return stars_; }

    // A wall clock outranks the uptime it replaces. Passing invalid goes back to
    // uptime, which needs its label because the digits alone look like the hour.
    void set_wall_clock(const char* local_time, bool valid);
    const char* clock_label() const { return wall_clock_ ? "" : "uptime"; }
    const char* clock_time() const { return clock_time_; }

private:
    void activate(uint64_t now_ms);
    void step_bounce();
    void step_starfield();
    void format_uptime(uint64_t now_ms);

    uint16_t panel_width_ = 0;
    uint16_t panel_height_ = 0;
    uint16_t sprite_width_ = 0;
    uint16_t sprite_height_ = 0;
    uint32_t idle_ms_ = 0;
    SaverKind kind_ = SaverKind::None;
    bool active_ = false;
    uint64_t last_activity_ms_ = 0;
    uint64_t last_step_ms_ = 0;
    // Whole pixels, so the tests describe motion rather than rounding.
    int16_t x_ = 0;
    int16_t y_ = 0;
    int8_t step_x_ = 2;
    int8_t step_y_ = 2;
    // Perturbed on every bounce so the path does not settle into a short loop,
    // which is what leaves a burnt-in track.
    uint32_t drift_ = 0x2545F491u;
    ScreensaverStar stars_[kStarCount]{};
    bool wall_clock_ = false;
    char clock_time_[12]{};
};

}  // namespace garland
