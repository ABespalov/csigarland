/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Hardware-free schedule logic: actions, their rules, JSON and resolving
 * Contributors: None
 */

#pragma once

#include <cJSON.h>

#include <cstddef>
#include <cstdint>

#include "effect_spec.h"

namespace garland {

constexpr size_t kMaxScheduleEntries = 32;
constexpr uint8_t kAllDays = 0x7F;  // Bit 0 is Sunday, as in struct tm.

enum class PowerStep : uint8_t { None, On, Off };

// One action: steps bound to a time of day. A zero or empty step field means the
// step is absent. Stored as a blob in NVS, so changing the layout needs a new key.
struct ScheduleEntry {
    bool enabled;
    uint8_t hour;
    uint8_t minute;
    uint8_t days;
    PowerStep power;
    uint8_t brightness_percent;
    uint8_t speed_x4;
    char effect[kMaxEffectIdLength + 1];
};

// What one or more actions add up to. Applied as a whole, so replaying a day of
// actions never flashes the garland through the states in between.
struct ResolvedAction {
    bool any = false;
    PowerStep power = PowerStep::None;
    uint8_t brightness_percent = 0;
    uint8_t speed_x4 = 0;
    int effect = -1;
};

struct LocalMinute {
    uint8_t weekday;  // 0 is Sunday.
    uint8_t hour;
    uint8_t minute;
};

using LocalMinuteFn = LocalMinute (*)(int64_t minutes_since_epoch);

// Checks an action's own rules: a valid time and days, at least one step, and
// nothing alongside "off". Returns false and points error at a static description.
bool entry_is_valid(const ScheduleEntry& entry, const char*& error);

bool entry_matches(const ScheduleEntry& entry, const LocalMinute& now);

// Adds one action to what came before it. An effect turns the garland on, since
// starting one is what it asks for; a later "off" overrides that. An effect no
// longer in the catalog is skipped.
void merge_entry(ResolvedAction& action, const ScheduleEntry& entry);

// Everything due in [first_minute, last_minute], oldest first and in list order
// within one minute.
ResolvedAction resolve_minutes(const ScheduleEntry* entries, size_t count, int64_t first_minute, int64_t last_minute,
                               LocalMinuteFn to_local);

// The first minute after after_minute with an action due, or -1 when none comes within a
// week, by which time every day has had its turn. Walks by the minute, so that a change of
// clocks counts: a call takes up to some 10000 conversions by to_local.
int64_t next_action_minute(const ScheduleEntry* entries, size_t count, int64_t after_minute, LocalMinuteFn to_local);

constexpr int64_t kActionSoonMinutes = 24 * 60;
// Finding none takes the whole week's walk, so that answer stands for a while. Only time can change
// it, as a new list, zone or clock forgets it, and it said nothing comes for a week.
constexpr int64_t kNoActionStandsMinutes = 60;

// Whether what was found at found_at, -1 for nothing yet, still holds at now_minute.
constexpr bool next_action_still_holds(int64_t found_at, int64_t action_minute, int64_t now_minute) {
    if (found_at < 0 || now_minute < found_at) {
        return false;
    }
    return action_minute >= 0 ? action_minute > now_minute : now_minute - found_at < kNoActionStandsMinutes;
}

constexpr bool action_is_soon(int64_t action_minute, int64_t now_minute) {
    return action_minute > now_minute && action_minute - now_minute <= kActionSoonMinutes;
}

// All or nothing: any invalid entry rejects the list. Effects must be in the catalog.
bool schedule_from_json(const cJSON* array, ScheduleEntry* out, size_t& out_count, const char*& error);

// Returns a new array the caller owns, or nullptr when out of memory.
cJSON* schedule_to_json(const ScheduleEntry* entries, size_t count);

// A POSIX TZ string such as "CET-1CEST,M3.5.0,M10.5.0/3". Only guards what reaches
// setenv: the C library treats an unparsable string as UTC.
bool time_zone_is_plausible(const char* zone);

}  // namespace garland
