/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: What the Scheduler24 screensaver says: the next event and how soon, or the last and how long ago
 * Contributors: None
 *
 * No clock and no schedule of its own: it is told the moments and the time, so its words can be host tests.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "garland_state.h"
#include "schedule.h"

namespace garland {

// An effect's name as the screen shows it, or a few words of what happened.
constexpr size_t kSaverNameBytes = 40;
// Both lines, and the line break between them.
constexpr size_t kSaverTextBytes = 2 * (kSaverNameBytes + 16);

// One moment the screensaver may tell of, on the uptime clock: an event to come, as the schedule's next step
// or the playlist's next effect, or one that came, as the start, a turn on or off, or a step done.
struct SaverMoment {
    bool known = false;
    int64_t at_ms = 0;
    char name[kSaverNameBytes] = "";
};

inline SaverMoment saver_moment(int64_t at_ms, const char* name) {
    SaverMoment moment;
    moment.known = true;
    moment.at_ms = at_ms;
    std::snprintf(moment.name, sizeof(moment.name), "%s", name);
    return moment;
}

// What a step of the schedule does, in a word or two, as the screen has room for: the effect it plays, by
// name, or the playlist; else what it does to the power, the brightness or the speed, in that order.
// effect_name writes the name of a catalog position into a buffer of the size given.
template <typename EffectName>
void saver_action_name(const ResolvedAction& action, EffectName effect_name, char* out, size_t size) {
    if (size == 0) {
        return;
    }
    if (action.playlist) {
        std::snprintf(out, size, "%s", "Playlist");
    } else if (action.effect >= 0) {
        effect_name(action.effect, out, size);
    } else if (action.power == PowerStep::Off) {
        std::snprintf(out, size, "%s", "Off");
    } else if (action.power == PowerStep::On) {
        std::snprintf(out, size, "%s", "On");
    } else if (action.power == PowerStep::Rise) {
        std::snprintf(out, size, "%s", "Rise");
    } else if (action.brightness_percent != 0) {
        std::snprintf(out, size, "Brightness %u%%", static_cast<unsigned>(action.brightness_percent));
    } else if (action.speed_x4 != 0) {
        char speed[kSpeedTextBytes];
        speed_to_text(action.speed_x4, speed);
        std::snprintf(out, size, "Speed %sx", speed);
    } else {
        out[0] = '\0';
    }
}

// How long, to the second while under an hour: "45s", "12m 05s", "5h 02m", "2d 05h".
inline void saver_span_text(int64_t seconds, char* out, size_t size) {
    const auto s = static_cast<unsigned long long>(seconds < 0 ? 0 : seconds);
    if (s < 60) {
        std::snprintf(out, size, "%llus", s);
    } else if (s < 3600) {
        std::snprintf(out, size, "%llum %02llus", s / 60, s % 60);
    } else if (s < 86400) {
        std::snprintf(out, size, "%lluh %02llum", s / 3600, s / 60 % 60);
    } else {
        std::snprintf(out, size, "%llud %02lluh", s / 86400, s / 3600 % 24);
    }
}

// The soonest of ahead still to come, at now_ms or after: "next: Fireplace" and "in 2h 05m". With none, the
// latest of behind: "last: Off" and "3h 10m ago". The two lines with a line break between them.
inline void saver_text(const SaverMoment* ahead, size_t ahead_count, const SaverMoment* behind, size_t behind_count, int64_t now_ms,
                       char* out, size_t size) {
    const SaverMoment* next = nullptr;
    for (size_t i = 0; i < ahead_count; ++i) {
        if (ahead[i].known && ahead[i].at_ms >= now_ms && (next == nullptr || ahead[i].at_ms < next->at_ms)) {
            next = &ahead[i];
        }
    }
    char span[24] = "";
    if (next != nullptr) {
        // Rounded up, so the last second counts down to "in 0s" just as it comes, not before.
        saver_span_text((next->at_ms - now_ms + 999) / 1000, span, sizeof(span));
        std::snprintf(out, size, "next: %s\nin %s", next->name, span);
        return;
    }
    const SaverMoment* last = nullptr;
    for (size_t i = 0; i < behind_count; ++i) {
        if (behind[i].known && behind[i].at_ms <= now_ms && (last == nullptr || behind[i].at_ms >= last->at_ms)) {
            last = &behind[i];
        }
    }
    if (last == nullptr) {
        std::snprintf(out, size, "%s", "nothing yet");
        return;
    }
    saver_span_text((now_ms - last->at_ms) / 1000, span, sizeof(span));
    std::snprintf(out, size, "last: %s\n%s ago", last->name, span);
}

}  // namespace garland
