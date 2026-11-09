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
#include <cstring>

#include "effect_spec.h"

namespace garland {

constexpr size_t kMaxScheduleEntries = 32;
constexpr uint8_t kAllDays = 0x7F;  // Bit 0 is Sunday, as in struct tm.

// Rise is On, rising from dark over a minute. Added after 1.0.0, so a list saved then reads as ever.
enum class PowerStep : uint8_t { None, On, Off, Rise };

constexpr bool power_step_turns_on(PowerStep step) {
    return step == PowerStep::On || step == PowerStep::Rise;
}

// What schedule_v1 holds of a step, as 1.0.0 reads it: a rise is kept as on, and which rise apart. A
// firmware gone back to 1.0.0 then turns them on, where it would take a step it does not know for off.
constexpr PowerStep schedule_power_as_saved(PowerStep step) {
    return step == PowerStep::Rise ? PowerStep::On : step;
}

constexpr PowerStep schedule_power_as_read(PowerStep stored, bool rises) {
    return stored == PowerStep::On && rises ? PowerStep::Rise : stored;
}

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

    bool operator==(const ScheduleEntry&) const = default;
};

// Which entries rise is kept beside schedule_v1 as marks: a mask, bit i for entry i, with a hash of the
// schedule_v1 bytes it holds for, twice, for the list being saved and for the list before it. The marks
// are written before schedule_v1, so whether that write then succeeds, fails or is cut short, the bytes
// it leaves have their mask: two equal entries, one rising and one not, keep their own, a list refused
// never comes into force after a restart, and a list 1.0.0 saved since takes none of it.
constexpr size_t kRiseMarkBytes = sizeof(uint32_t) + sizeof(uint64_t);
constexpr size_t kRiseMarksBytes = 2 * kRiseMarkBytes;

inline uint32_t schedule_rise_mask(const ScheduleEntry* entries, size_t count) {
    uint32_t mask = 0;
    for (size_t i = 0; i < count && i < 32; ++i) {
        mask |= entries[i].power == PowerStep::Rise ? uint32_t{1} << i : 0;
    }
    return mask;
}

// FNV-1a: to tell one list from another, not to stand up to anyone.
constexpr uint64_t schedule_bytes_hash(const uint8_t* bytes, size_t length) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < length; ++i) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
    return hash;
}

// The marks for saved, the schedule_v1 bytes about to be written, with mask, and for before, the bytes
// it replaces, with theirs.
inline void schedule_rise_marks(uint32_t mask, const uint8_t* saved, size_t saved_bytes, uint32_t before_mask, const uint8_t* before,
                                size_t before_bytes, uint8_t (&out)[kRiseMarksBytes]) {
    const uint64_t hashes[2] = {schedule_bytes_hash(saved, saved_bytes), schedule_bytes_hash(before, before_bytes)};
    const uint32_t masks[2] = {mask, before_mask};
    for (size_t i = 0; i < 2; ++i) {
        std::memcpy(out + i * kRiseMarkBytes, &masks[i], sizeof(uint32_t));
        std::memcpy(out + i * kRiseMarkBytes + sizeof(uint32_t), &hashes[i], sizeof(uint64_t));
    }
}

// Before the marks, builds before 1.1.0 kept the rising entries whole, and found them by equality: still
// read, so a garland updated from such a build keeps its rises until its schedule is saved again.
inline bool schedule_entry_rises(const ScheduleEntry& stored, const ScheduleEntry* rises, size_t count) {
    if (stored.power != PowerStep::On) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (rises[i] == stored) {
            return true;
        }
    }
    return false;
}

// Which of stored, the schedule_v1 entries as read, rise, by what is saved beside them in rises: the
// marks, or the entries whole of a build before them. None when neither holds for these bytes.
inline uint32_t schedule_rises_of(const ScheduleEntry* stored, size_t count, const uint8_t* rises, size_t rises_bytes) {
    const auto* const saved = reinterpret_cast<const uint8_t*>(stored);
    if (rises_bytes == kRiseMarksBytes) {
        const uint64_t hash = schedule_bytes_hash(saved, count * sizeof(ScheduleEntry));
        for (size_t i = 0; i < 2; ++i) {
            uint32_t mask = 0;
            uint64_t marked = 0;
            std::memcpy(&mask, rises + i * kRiseMarkBytes, sizeof(uint32_t));
            std::memcpy(&marked, rises + i * kRiseMarkBytes + sizeof(uint32_t), sizeof(uint64_t));
            if (marked == hash) {
                return mask;
            }
        }
        return 0;
    }
    if (rises_bytes % sizeof(ScheduleEntry) != 0) {
        return 0;
    }
    ScheduleEntry whole[kMaxScheduleEntries];
    const size_t whole_count = rises_bytes / sizeof(ScheduleEntry) < kMaxScheduleEntries ? rises_bytes / sizeof(ScheduleEntry) : kMaxScheduleEntries;
    std::memcpy(whole, rises, whole_count * sizeof(ScheduleEntry));
    uint32_t mask = 0;
    for (size_t i = 0; i < count && i < 32; ++i) {
        mask |= schedule_entry_rises(stored[i], whole, whole_count) ? uint32_t{1} << i : 0;
    }
    return mask;
}

// The marks as a save leaves them once it is done, for stored, the schedule_v1 entries read, and true when
// those in rises differ: a cut between the writes of a save, or a third write that failed, leaves a mark
// of a list not in force, the one before or the one refused, which 1.0.0 could match by saving its bytes.
// Written at a start, they hold the list in force alone. Entries kept whole by a build before the marks
// are left as they are, until the schedule is saved again.
inline bool schedule_rise_marks_tidy(const ScheduleEntry* stored, size_t count, const uint8_t* rises, size_t rises_bytes,
                                     uint8_t (&out)[kRiseMarksBytes]) {
    if (rises_bytes != kRiseMarksBytes) {
        return false;
    }
    const auto* const saved = reinterpret_cast<const uint8_t*>(stored);
    const size_t saved_bytes = count * sizeof(ScheduleEntry);
    const uint32_t mask = schedule_rises_of(stored, count, rises, rises_bytes);
    schedule_rise_marks(mask, saved, saved_bytes, mask, saved, saved_bytes, out);
    return std::memcmp(out, rises, kRiseMarksBytes) != 0;
}

// The effect a step starting the playlist holds, as an action has no other room for it and its blob
// layout must stay. No effect's id can begin with @, and 1.0.0 saved none such.
inline constexpr char kPlaylistStep[] = "@playlist";

// What one or more actions add up to. Applied as a whole, so replaying a day of
// actions never flashes the garland through the states in between.
struct ResolvedAction {
    bool any = false;
    PowerStep power = PowerStep::None;
    uint8_t brightness_percent = 0;
    uint8_t speed_x4 = 0;
    int effect = -1;
    bool playlist = false;  // Start the playlist, in place of one effect: the later of the two wins.
    // As if every step starting the playlist were left out, which is what comes of them with no playlist
    // to start: power, and the effect, as the steps in their order leave them then.
    PowerStep power_without_playlist = PowerStep::None;
    int effect_without_playlist = -1;

    // Whether it changes anything at all: a start of an empty playlist alone does not.
    constexpr bool does_something() const {
        return power != PowerStep::None || brightness_percent != 0 || speed_x4 != 0 || effect >= 0 || playlist;
    }

    // What the action adds up to for a garland with a playlist to start or with none.
    constexpr ResolvedAction with_playlist(bool ready) const {
        if (ready || !playlist) {
            return *this;
        }
        ResolvedAction none = *this;
        none.playlist = false;
        none.power = power_without_playlist;
        none.effect = effect_without_playlist;
        return none;
    }
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

// Whether what a minute's steps leave playing is an effect the catalog lacks, which merge_entry passes by: the
// last of them, in list order, that names an effect or starts the playlist names one catalog_find does not find.
bool minute_plays_unknown_effect(const ScheduleEntry* entries, size_t count, int64_t minute, LocalMinuteFn to_local);

// Whether a minute's steps, action as resolve_minutes gives it, wait rather than be done now and count as done: a
// start of the playlist while its list cannot be played yet, as one not read, or read with every effect it
// names an upload not read yet; or an effect that may be such an upload. Done now, either would come to nothing
// and never come again once read. One whose minute ends off is done now: what it comes to is known without them.
constexpr bool minute_waits(const ResolvedAction& action, bool list_waits, bool plays_unknown, bool uploads_may_hold) {
    if (action.power == PowerStep::Off) {
        return false;
    }
    return (action.playlist && list_waits) || (plays_unknown && uploads_may_hold);
}

// Adds what a later minute comes to onto what the minutes before came to: what it names takes the place
// of what they did, a rise only as its own minute asked it.
void combine_minutes(ResolvedAction& action, const ResolvedAction& later);

// The first minute after after_minute with an action due, or -1 when none comes within a
// week, by which time every day has had its turn. Walks by the minute, so that a change of
// clocks counts: a call takes up to some 10000 conversions by to_local.
// With until, no further than that minute.
int64_t next_action_minute(const ScheduleEntry* entries, size_t count, int64_t after_minute, LocalMinuteFn to_local, int64_t until = -1);

// From found, a minute next_action_minute gave, on to the first up to until whose actions come to
// something for a garland with a playlist to start or with none, as ready says, with what they come to
// in action, or -1. A minute that only starts an empty playlist is passed over, as many as there are: the
// walks from one to the next add up to no more than the way to until, and one more week at the last.
int64_t next_doing_minute(const ScheduleEntry* entries, size_t count, int64_t found, int64_t until, LocalMinuteFn to_local, bool ready,
                          ResolvedAction& action);

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

// Whether the time of day is the owner's: the clock set, and the zone saved read, or found to be none. Until
// then the clock goes by UTC, and the schedule, written in local time, is neither done nor told by it: its next
// action, its day, the screen's mark of one soon and the time of day on the screen would be hours off.
constexpr bool local_time_is_known(bool clock_synced, bool zone_read) {
    return clock_synced && zone_read;
}

// "on", "off", "rise", or "" for none, as the JSON spells it.
const char* power_step_name(PowerStep step);

// The schedule on a timeline: every minute of the next day with an action due, what the actions in it
// add up to, and which ones they are. Where more than one meet, the later in the list win, step by step,
// as they will when the minute comes.
constexpr int64_t kTimelineMinutes = 24 * 60;
constexpr size_t kMaxTimelineEvents = 64;

struct TimelineEvent {
    int64_t minute = 0;  // Minutes since the epoch.
    LocalMinute local{};
    ResolvedAction action;
    uint8_t actions = 0;   // How many are due in that minute.
    uint32_t entries = 0;  // One bit per position in the list.
};

// Oldest first, from the minute after after_minute, up to max_events. Walks the day by the minute,
// so that a change of clocks counts.
// With playlist_ready false, a step starting the playlist comes to what it does with none to start.
// Up to last_minute when it is given, else a day from after_minute.
size_t schedule_timeline(const ScheduleEntry* entries, size_t count, int64_t after_minute, LocalMinuteFn to_local,
                         TimelineEvent* out, size_t max_events, bool playlist_ready = true, int64_t last_minute = -1);

// {"at", "time", "day", "entries", and the steps as an action spells them}. A new array the caller
// owns, or nullptr when out of memory.
cJSON* timeline_to_json(const TimelineEvent* events, size_t count);

// The schedule's JSON as this firmware writes it. 1.0.0's had no number, and a client that sends none
// knows no rise and no playlist step.
constexpr int kScheduleSchema = 2;

// Whether a list holds steps a client of 1.0.0 does not know: one replacing it would drop them unseen.
bool schedule_has_newer_steps(const ScheduleEntry* entries, size_t count);

// All or nothing: any invalid entry rejects the list. Effects must be in the catalog, or named by held, the
// list there now, of held_count, which may keep one the catalog lacks.
bool schedule_from_json(const cJSON* array, ScheduleEntry* out, size_t& out_count, const char*& error, const ScheduleEntry* held = nullptr,
                        size_t held_count = 0);

// The ids of effects the entries name that the catalog lacks, each once: kept, and done once they are
// there. A new array the caller owns, or nullptr when out of memory.
cJSON* schedule_missing_json(const ScheduleEntry* entries, size_t count);

// Returns a new array the caller owns, or nullptr when out of memory.
cJSON* schedule_to_json(const ScheduleEntry* entries, size_t count);

// A POSIX TZ string such as "CET-1CEST,M3.5.0,M10.5.0/3". Only guards what reaches
// setenv: the C library treats an unparsable string as UTC.
bool time_zone_is_plausible(const char* zone);

}  // namespace garland
