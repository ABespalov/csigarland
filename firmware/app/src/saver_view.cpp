/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The screensavers drawn with LVGL, for the panel and for the documents' pictures alike
 * Contributors: None
 */

#include "saver_view.h"

#include <cstdio>
#include <cstring>

#include "display_view.h"

namespace garland {
namespace {

const lv_font_t* const kClockFont = &lv_font_montserrat_28;
// Scheduler24's words, no wider than this and no taller: a line longer wraps, and words taller still take the
// next smaller font, down to the smallest.
constexpr int kStepMostPixels = kScreenSize * 8 / 10;
const lv_font_t* const kStepFonts[] = {&lv_font_montserrat_28, &lv_font_montserrat_20, &lv_font_montserrat_16, &lv_font_montserrat_14};

constexpr int kDotSize = 10;
constexpr int kDotMargin = 10;
constexpr int kClockGap = 4;
constexpr uint32_t kStarMaxDistQ4 = kScreenSize * 16;

struct SaverScreen {
    lv_obj_t* root;
    lv_obj_t* stars[Screensaver::kStarCount];
    lv_obj_t* dot;
    lv_obj_t* clock_box;
    lv_obj_t* clock_label;
    lv_obj_t* clock_time;
    int clock_width;
    int clock_time_height;
    int clock_label_height;
    lv_obj_t* step_box;
    lv_obj_t* step_text;
};

SaverScreen s_saver;
Screensaver s_engine;
char s_clock_shown[12] = "";
bool s_clock_layout_valid = false;
bool s_clock_layout_synced = false;
char s_step_shown[kSaverTextBytes] = "";

void measure_text(const char* text, const lv_font_t* font, int& width, int& height) {
    lv_point_t size = {};
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    width = size.x;
    height = size.y;
}

}  // namespace

void saver_view_build() {
    s_saver.root = lv_obj_create(nullptr);
    lv_obj_set_style_bg_opa(s_saver.root, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s_saver.root, false);
    for (lv_obj_t*& star : s_saver.stars) {
        star = display_add_block(s_saver.root);
    }
    s_saver.dot = display_add_block(s_saver.root);
    lv_obj_set_size(s_saver.dot, kDotSize, kDotSize);
    lv_obj_set_style_radius(s_saver.dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_pos(s_saver.dot, kScreenSize - kDotMargin - kDotSize, kDotMargin);

    // Sized by the widest digits, so the bounce box does not jitter as they change.
    int unused = 0;
    measure_text("00:00:00", kClockFont, s_saver.clock_width, s_saver.clock_time_height);
    measure_text("uptime", kClockFont, unused, s_saver.clock_label_height);
    s_saver.clock_box = lv_obj_create(s_saver.root);
    lv_obj_remove_style_all(s_saver.clock_box);
    lv_obj_set_scrollable(s_saver.clock_box, false);
    s_saver.clock_label = lv_label_create(s_saver.clock_box);
    s_saver.clock_time = lv_label_create(s_saver.clock_box);
    for (lv_obj_t* label : {s_saver.clock_label, s_saver.clock_time}) {
        lv_obj_set_style_text_font(label, kClockFont, 0);
        lv_obj_set_style_text_color(label, kMuted, 0);
        lv_label_set_text_static(label, "");
    }

    s_saver.step_box = lv_obj_create(s_saver.root);
    lv_obj_remove_style_all(s_saver.step_box);
    lv_obj_set_scrollable(s_saver.step_box, false);
    s_saver.step_text = lv_label_create(s_saver.step_box);
    lv_obj_set_style_text_color(s_saver.step_text, kMuted, 0);
    lv_obj_set_style_text_align(s_saver.step_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_saver.step_text, LV_LABEL_LONG_WRAP);
    lv_label_set_text_static(s_saver.step_text, "");
    s_engine.configure(kScreenSize, kScreenSize);
}

lv_obj_t* saver_view_root() {
    return s_saver.root;
}

Screensaver& saver_view_engine() {
    return s_engine;
}

void saver_view_take_clock(const char* local_time, bool synced) {
    s_engine.set_wall_clock(local_time, synced);
    // Only on a change: setting a label's text redraws it, and this runs every poll.
    if (s_clock_layout_valid && synced == s_clock_layout_synced) {
        return;
    }
    s_clock_layout_valid = true;
    s_clock_layout_synced = synced;
    const int label_room = synced ? 0 : s_saver.clock_label_height + kClockGap;
    const int height = label_room + s_saver.clock_time_height;
    s_engine.set_sprite_size(static_cast<uint16_t>(s_saver.clock_width), static_cast<uint16_t>(height));
    lv_obj_set_size(s_saver.clock_box, s_saver.clock_width, height);
    lv_label_set_text_static(s_saver.clock_label, s_engine.clock_label());
    lv_obj_set_hidden(s_saver.clock_label, synced);
    lv_obj_set_pos(s_saver.clock_time, 0, label_room);
}

void saver_view_take_text(const char* text) {
    if (std::strcmp(text, s_step_shown) == 0) {
        return;
    }
    // The first words of a showing, after a layout forgotten: centred for this size, not for the one before.
    const bool first = s_step_shown[0] == '\0';
    std::snprintf(s_step_shown, sizeof(s_step_shown), "%s", text);
    // Laid out as the label will lay it out, wrapped at the widest it may be: the first font whose words then
    // stand no taller than they may, or the smallest.
    const lv_font_t* font = nullptr;
    lv_point_t size = {};
    for (const lv_font_t* candidate : kStepFonts) {
        font = candidate;
        lv_text_get_size(&size, s_step_shown, font, 0, 0, kStepMostPixels, LV_TEXT_FLAG_NONE);
        if (size.y <= kStepMostPixels) {
            break;
        }
    }
    lv_obj_set_style_text_font(s_saver.step_text, font, 0);
    lv_obj_set_size(s_saver.step_text, size.x, size.y);
    lv_label_set_text_static(s_saver.step_text, s_step_shown);
    s_engine.set_sprite_size(static_cast<uint16_t>(size.x), static_cast<uint16_t>(size.y));
    if (first) {
        s_engine.recenter();
    }
    lv_obj_set_size(s_saver.step_box, size.x, size.y);
    // Where the new size leaves it, kept within the screen, not where it stood with the old.
    lv_obj_set_pos(s_saver.step_box, s_engine.x(), s_engine.y());
}

void saver_view_forget_layout() {
    s_clock_layout_valid = false;
    s_step_shown[0] = '\0';
}

void saver_view_show_kind() {
    const SaverKind kind = s_engine.kind();
    for (lv_obj_t* star : s_saver.stars) {
        lv_obj_set_hidden(star, kind != SaverKind::Starfield);
    }
    lv_obj_set_hidden(s_saver.dot, kind != SaverKind::BreathingDot);
    lv_obj_set_hidden(s_saver.clock_box, kind != SaverKind::FlyingClock);
    lv_obj_set_hidden(s_saver.step_box, kind != SaverKind::Scheduler24);
    lv_obj_set_style_bg_color(s_saver.root, kind == SaverKind::BlackScreen ? kBlack : kBackground, 0);
}

void saver_view_draw(const ScreensaverView& view, uint32_t now_ms) {
    switch (s_engine.kind()) {
        case SaverKind::Starfield:
            for (size_t i = 0; i < Screensaver::kStarCount; ++i) {
                const ScreensaverStar& star = s_engine.stars()[i];
                const uint16_t approach = star_approach_q8(star, kStarMaxDistQ4);
                const int size = star_pixels(approach);
                lv_obj_set_pos(s_saver.stars[i], star.x, star.y);
                lv_obj_set_size(s_saver.stars[i], size, size);
                lv_obj_set_style_bg_color(s_saver.stars[i], lv_color_mix(kWhite, kBackground, star_brightness(star, approach)), 0);
            }
            break;
        case SaverKind::BreathingDot:
            lv_obj_set_style_bg_color(s_saver.dot, lv_color_mix(kBrandBlue, kBackground, breath_wave(now_ms)), 0);
            break;
        case SaverKind::FlyingClock:
            lv_obj_set_pos(s_saver.clock_box, view.x, view.y);
            // Only when it changed: setting a label's text redraws it.
            if (std::strcmp(s_clock_shown, s_engine.clock_time()) != 0) {
                std::snprintf(s_clock_shown, sizeof(s_clock_shown), "%s", s_engine.clock_time());
                lv_label_set_text_static(s_saver.clock_time, s_clock_shown);
            }
            break;
        case SaverKind::Scheduler24:
            // Where the engine has it now: its words may have taken a new size since the view was polled.
            lv_obj_set_pos(s_saver.step_box, s_engine.x(), s_engine.y());
            break;
        default:
            break;
    }
}

}  // namespace garland
