/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The screensavers drawn with LVGL, for the panel and for the documents' pictures alike
 * Contributors: None
 *
 * Needs LVGL alone, no panel and no clock: the display tells it the time and the schedule's words, and
 * firmware/app/tools/screens draws it on this computer, as it draws display_view.
 */

#pragma once

#include <cstdint>

#include "lvgl.h"
#include "saver_schedule.h"
#include "screensaver.h"

namespace garland {

// Builds the screensaver's screen, its parts hidden, and sizes its engine to the panel.
void saver_view_build();
lv_obj_t* saver_view_root();
// When it runs and where its sprite flies: see screensaver.h.
Screensaver& saver_view_engine();

// The Flying Clock's time, as time_format_clock gives it, and whether it is the time of day: else uptime.
void saver_view_take_clock(const char* local_time, bool synced);
// Scheduler24's words, as saver_text writes them: no wider than 80 % of the screen, wrapped to more lines up to
// 80 % of its height, and past that in a smaller font. Laid out again only when they changed: setting a
// label's text redraws it.
void saver_view_take_text(const char* text);
// Another kind: the sprite, which the clock and Scheduler24 share, is laid out again for it.
void saver_view_forget_layout();

// The parts of the engine's kind shown, and no other, on its background, as it starts.
void saver_view_show_kind();
void saver_view_draw(const ScreensaverView& view, uint32_t now_ms);

}  // namespace garland
