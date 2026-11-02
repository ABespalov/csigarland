/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The main screen as LVGL objects, and the theme it shares with the screensavers
 * Contributors: None
 */

#pragma once

#include "lvgl.h"
#include "screen_model.h"

namespace garland {

inline constexpr int kScreenSize = 240;

// csiPass's device theme. One accent colours a whole screen and says its state, see
// ScreenTone; text stays white, and only the wordmark keeps its own blue.
inline const lv_color_t kBackground = lv_color_hex(0x0F172A);
inline const lv_color_t kBrandBlue = lv_color_hex(0x3B82F6);
inline const lv_color_t kAttention = lv_color_hex(0xF59E0B);
inline const lv_color_t kSuccess = lv_color_hex(0x22C55E);
inline const lv_color_t kWhite = lv_color_hex(0xF8FAFC);
inline const lv_color_t kMuted = lv_color_hex(0x94A3B8);
inline const lv_color_t kVersionText = lv_color_hex(0xE2E8F0);
inline const lv_color_t kDimmedBrand = lv_color_hex(0xCBD5E1);
inline const lv_color_t kBlack = lv_color_hex(0x000000);

// On the active screen, under the LVGL lock.
void display_view_build(const char* firmware_label);
void display_view_draw(const ScreenModel& model);
lv_obj_t* display_view_root();

// A bare rectangle with none of a default object's theme, scrolling or clicks.
lv_obj_t* display_add_block(lv_obj_t* parent);

}  // namespace garland
