/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Hardware-free part of remembering the effect, brightness, speed and on or off
 * Contributors: None
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "garland_state.h"

namespace garland {

// As stored in NVS, which already spreads writes over its pages and keeps the old value
// until a new one is complete. Rare writes, see MemorySaveTimer, are what is left to do.
struct StoredSettings {
    uint8_t brightness_percent = 0;
    uint8_t speed_x4 = 0;
    char effect_id[33] = {};  // By id, so a change in the catalog cannot point it at another effect.
    uint8_t playing = 0;  // 0 or 1, and nothing else, as 1.0.0 reads any other value as on.
    int64_t choice_epoch = 0;  // When this was chosen, 0 with no clock then.
};

// Nothing has been remembered yet, so nobody has chosen anything.
constexpr int64_t kNoChoiceYet = -1;
constexpr int64_t kReplayMinutes = 24 * 60;

// The first minute of the schedule to carry out now that the time is known for the first time
// since a start, or -1 for none. The remembered state is what the owner left, and only steps from
// after it were missed. A choice made with no clock may be later than any of them, so it stands.
constexpr int64_t replay_first_minute(int64_t choice_epoch, int64_t now_minute) {
    const int64_t day_ago = now_minute - kReplayMinutes + 1;
    if (choice_epoch == kNoChoiceYet) {
        return day_ago;
    }
    if (choice_epoch <= 0) {
        return -1;
    }
    const int64_t after_choice = choice_epoch / 60 + 1;
    const int64_t first = after_choice > day_ago ? after_choice : day_ago;
    return first <= now_minute ? first : -1;
}

// A choice made with no clock outranks what came before the first time known, and no more than
// that: from then on, it has to count as made at that time, or it would outrank every step for good.
constexpr bool choice_needs_a_time(int64_t choice_epoch) {
    return choice_epoch == 0;
}

// A forward clock step within this still runs what it passed over. A bigger one
// is the clock being corrected, not time passing.
constexpr int64_t kMaxCatchUpMinutes = 5;

// The first minute of the schedule to carry out as time passes, past now_minute if none. A step
// already due when the owner chose something is older than that choice, in the same minute too, and
// the one after it is not. A choice dated later than now was dated by a clock since corrected.
constexpr int64_t catch_up_first_minute(int64_t last_minute, int64_t now_minute, int64_t choice_minute) {
    const int64_t next = last_minute + 1;
    const int64_t oldest = now_minute - kMaxCatchUpMinutes + 1;
    const int64_t first = next > oldest ? next : oldest;
    return choice_minute <= now_minute && choice_minute >= first ? choice_minute + 1 : first;
}

// The moment a save of the settings, with the clock known, is dated by, now being the clock's reading.
// An owner's choice by its own moment, owner_choice, -1 for none, as the save comes up to 10 s after it
// and a step in between would else fall behind it at the next start; one made before the clock was known
// as at first_known, which it outranks no step after. What a scheduled step did since the choice by now,
// as the step is the newer: but for steps from minute wait_from on that wait for the playlist, -1 for none,
// as just before the first of those, which the next start then still has to do.
constexpr int64_t save_epoch(int64_t now, int64_t owner_choice, int64_t first_known, int64_t last_step, int64_t wait_from) {
    if (owner_choice >= 0) {
        const int64_t choice = owner_choice > first_known ? owner_choice : first_known;
        if (choice >= last_step) {
            return choice < now ? choice : now;
        }
    }
    const int64_t before_waiting = wait_from * 60 - 1;
    return wait_from >= 0 && before_waiting < now ? before_waiting : now;
}

// How long a failed save of that time waits to try again. Nothing else would write it while the
// settings stand still, and the next start would take it for a choice with no clock.
constexpr uint32_t kChoiceTimeRetryMs = 60000;

// holds: nothing newer is waiting, whose own save will carry its own time.
constexpr bool choice_time_is_due(int64_t choice_epoch, bool holds, bool tried, uint32_t since_try_ms) {
    return choice_needs_a_time(choice_epoch) && holds && (!tried || since_try_ms >= kChoiceTimeRetryMs);
}

// A pause around a clock read widens what it can say of how the two clocks relate.
constexpr int kClockReadTries = 4;
constexpr uint32_t kClockReadPauseMs = 1;
// Each of the three clock values is cut down to a whole millisecond.
constexpr int64_t kClockReadRoundingMs = 2;

// The latest the time of day can have been at a moment on the uptime clock, in ms. The two clocks
// cannot be read at once, so this can only be a range, and the narrowest of a few tries is used. Its
// late end, since a choice this close to a minute's start cannot be told from one just after it, and
// the rule is that what the owner chose outranks the schedule: at worst a step due within that range
// after the choice waits for the owner's next word, and never overrides it.
template <typename Uptime, typename Wall>
int64_t latest_wall_ms_of_uptime(uint32_t moment_ms, Uptime uptime_ms, Wall wall_ms) {
    int64_t latest = 0;
    uint32_t narrowest = UINT32_MAX;
    for (int i = 0; i < kClockReadTries && narrowest > kClockReadPauseMs; ++i) {
        const uint32_t before = uptime_ms();
        const int64_t wall = wall_ms();
        const uint32_t width = uptime_ms() - before;
        if (width < narrowest) {
            narrowest = width;
            latest = wall - static_cast<int64_t>(before - moment_ms) + kClockReadRoundingMs;
        }
    }
    return latest;
}

constexpr bool remembered_brightness_is_valid(uint8_t percent) {
    return percent >= 1 && percent <= 100;
}

struct Remembered {
    uint16_t effect = 0;
    uint8_t brightness_percent = 0;
    uint8_t speed_x4 = 0;
    bool playing = false;
    bool playlist = false;  // Saved apart from the rest: see settings_save_playlist_playing.

    bool operator==(const Remembered&) const = default;
};

// Clicking through effects or levels must not write each one: the settings are saved only
// once all of them have stayed unchanged for kMemorySaveDelayMs. Turning on or off is saved
// at once, as a garland is often turned off just before the power goes.
constexpr uint32_t kMemorySaveDelayMs = 10000;

class MemorySaveTimer {
public:
    explicit MemorySaveTimer(const Remembered& saved) : saved_(saved) {}

    // True when current should be saved now. Time wraps safely.
    bool should_save(const Remembered& current, uint32_t now_ms) {
        if (current == saved_ && !chosen_) {
            waiting_ = false;
            return false;
        }
        if (!waiting_ || !(current == candidate_)) {
            waiting_ = true;
            candidate_ = current;
            since_ms_ = now_ms;
            // As a playlist stopped, or started, at once too: a power cut must not bring back the other.
            return current.playing != saved_.playing || current.playlist != saved_.playlist;
        }
        return now_ms - since_ms_ >= kMemorySaveDelayMs;
    }

    // The owner chose something, which may be just what was saved. The moment still counts, as it
    // outranks every schedule step from before it, so it is saved like a change.
    void note_choice() { chosen_ = true; }

    // Nothing is waiting to be saved: what is chosen now is what was saved, and when.
    bool holds(const Remembered& current) const { return current == saved_ && !chosen_; }

    void saved(const Remembered& current) {
        saved_ = current;
        waiting_ = false;
        chosen_ = false;
    }

    // After a failed write: waits the whole delay again before the next try.
    void retry_later(uint32_t now_ms) { since_ms_ = now_ms; }

private:
    Remembered saved_;
    Remembered candidate_;
    bool waiting_ = false;
    bool chosen_ = false;
    uint32_t since_ms_ = 0;
};

}  // namespace garland
