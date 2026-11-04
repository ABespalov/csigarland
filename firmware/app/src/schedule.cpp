/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Hardware-free schedule logic: actions, their rules, JSON and resolving
 * Contributors: None
 */

#include "schedule.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>

#include "effect_catalog.h"
#include "garland_state.h"

namespace garland {
namespace {

constexpr size_t kMaxTimeZoneLength = 48;

struct ScheduleJsonDeleter {
    void operator()(cJSON* json) const { cJSON_Delete(json); }
};

bool is_whole_number(const cJSON* item) {
    return cJSON_IsNumber(item) && item->valuedouble == item->valueint;
}

bool has_effect(const ScheduleEntry& entry) {
    return entry.effect[0] != '\0';
}

bool starts_playlist(const ScheduleEntry& entry) {
    return std::strcmp(entry.effect, kPlaylistStep) == 0;
}

bool parse_time(const char* text, ScheduleEntry& entry) {
    if (std::strlen(text) != 5 || text[2] != ':' || std::isdigit(static_cast<unsigned char>(text[0])) == 0 ||
        std::isdigit(static_cast<unsigned char>(text[1])) == 0 || std::isdigit(static_cast<unsigned char>(text[3])) == 0 ||
        std::isdigit(static_cast<unsigned char>(text[4])) == 0) {
        return false;
    }
    entry.hour = static_cast<uint8_t>((text[0] - '0') * 10 + (text[1] - '0'));
    entry.minute = static_cast<uint8_t>((text[3] - '0') * 10 + (text[4] - '0'));
    return true;
}

// Whether the list held names the effect id, as one of its steps: an effect the catalog lacks may be kept.
bool held_names(const ScheduleEntry* held, size_t held_count, const char* id) {
    // A step with no effect names none, and an empty id is no effect kept.
    if (id[0] == '\0') {
        return false;
    }
    for (size_t i = 0; held != nullptr && i < held_count; ++i) {
        if (std::strcmp(held[i].effect, id) == 0) {
            return true;
        }
    }
    return false;
}

bool parse_entry(const cJSON* item, ScheduleEntry& entry, const char*& error, const ScheduleEntry* held, size_t held_count) {
    entry = ScheduleEntry{};
    entry.enabled = true;
    entry.days = kAllDays;

    const cJSON* time = cJSON_GetObjectItemCaseSensitive(item, "time");
    if (!cJSON_IsString(time) || !parse_time(time->valuestring, entry)) {
        error = "time must be HH:MM";
        return false;
    }
    const cJSON* enabled = cJSON_GetObjectItemCaseSensitive(item, "enabled");
    if (enabled != nullptr) {
        if (!cJSON_IsBool(enabled)) {
            error = "enabled must be true or false";
            return false;
        }
        entry.enabled = cJSON_IsTrue(enabled);
    }
    const cJSON* days = cJSON_GetObjectItemCaseSensitive(item, "days");
    if (days != nullptr) {
        if (!is_whole_number(days) || days->valueint < 0 || days->valueint > kAllDays) {
            error = "days must be a mask of 0 to 127";
            return false;
        }
        entry.days = static_cast<uint8_t>(days->valueint);
    }
    const cJSON* power = cJSON_GetObjectItemCaseSensitive(item, "power");
    if (power != nullptr) {
        if (cJSON_IsString(power) && std::strcmp(power->valuestring, "on") == 0) {
            entry.power = PowerStep::On;
        } else if (cJSON_IsString(power) && std::strcmp(power->valuestring, "off") == 0) {
            entry.power = PowerStep::Off;
        } else if (cJSON_IsString(power) && std::strcmp(power->valuestring, "rise") == 0) {
            entry.power = PowerStep::Rise;
        } else {
            error = "power must be on, rise or off";
            return false;
        }
    }
    const cJSON* brightness = cJSON_GetObjectItemCaseSensitive(item, "brightness");
    if (brightness != nullptr) {
        if (!is_whole_number(brightness) || !brightness_percent_is_valid(brightness->valueint)) {
            error = "brightness must be 1 to 100";
            return false;
        }
        entry.brightness_percent = static_cast<uint8_t>(brightness->valueint);
    }
    const cJSON* speed = cJSON_GetObjectItemCaseSensitive(item, "speed");
    if (speed != nullptr) {
        entry.speed_x4 = cJSON_IsNumber(speed) ? speed_x4_from_value(speed->valuedouble) : 0;
        if (entry.speed_x4 == 0) {
            error = "speed must be 0.25, 0.5, 0.75, 1, 1.5, 2 or 3";
            return false;
        }
    }
    const cJSON* playlist = cJSON_GetObjectItemCaseSensitive(item, "playlist");
    const cJSON* effect = cJSON_GetObjectItemCaseSensitive(item, "effect");
    if (playlist != nullptr) {
        if (!cJSON_IsTrue(playlist) || effect != nullptr) {
            error = "playlist must be true, and goes with no effect";
            return false;
        }
        std::snprintf(entry.effect, sizeof(entry.effect), "%s", kPlaylistStep);
    }
    if (effect != nullptr) {
        // One the catalog lacks, as after an update of the effects, stays if the list held it already: an
        // editor sends the list whole, and a change of another action would else be refused.
        if (!cJSON_IsString(effect) || std::strlen(effect->valuestring) >= sizeof(entry.effect) ||
            (catalog_find(effect->valuestring) < 0 && !held_names(held, held_count, effect->valuestring))) {
            error = "effect is not in the catalog or not ready";
            return false;
        }
        std::snprintf(entry.effect, sizeof(entry.effect), "%s", effect->valuestring);
    }
    return entry_is_valid(entry, error);
}

}  // namespace

bool entry_is_valid(const ScheduleEntry& entry, const char*& error) {
    if (entry.hour > 23 || entry.minute > 59) {
        error = "time must be 00:00 to 23:59";
        return false;
    }
    if (entry.days == 0 || entry.days > kAllDays) {
        error = "pick at least one day";
        return false;
    }
    if (entry.brightness_percent > 100) {
        error = "brightness must be 1 to 100";
        return false;
    }
    if (entry.speed_x4 != 0 && !speed_x4_is_valid(entry.speed_x4)) {
        error = "speed must be 0.25, 0.5, 0.75, 1, 1.5, 2 or 3";
        return false;
    }
    if (entry.effect[sizeof(entry.effect) - 1] != '\0') {
        error = "effect id is too long";
        return false;
    }
    const bool has_settings = entry.brightness_percent != 0 || entry.speed_x4 != 0 || has_effect(entry);
    if (entry.power == PowerStep::Off && has_settings) {
        error = "off cannot be combined with other steps";
        return false;
    }
    if (entry.power == PowerStep::None && !has_settings) {
        error = "an action needs at least one step";
        return false;
    }
    return true;
}

bool entry_matches(const ScheduleEntry& entry, const LocalMinute& now) {
    return entry.enabled && entry.hour == now.hour && entry.minute == now.minute &&
           (entry.days & (1u << now.weekday)) != 0;
}

void merge_entry(ResolvedAction& action, const ScheduleEntry& entry) {
    action.any = true;
    if (entry.brightness_percent != 0) {
        action.brightness_percent = entry.brightness_percent;
    }
    if (entry.speed_x4 != 0) {
        action.speed_x4 = entry.speed_x4;
    }
    // Starting either turns the garland on, keeping a rise asked for before it in the minute.
    const auto turned_on = [](PowerStep before) { return before == PowerStep::Rise ? PowerStep::Rise : PowerStep::On; };
    if (starts_playlist(entry)) {
        // With a playlist: started here, on. With none: as if this step were not, but its own power.
        action.playlist = true;
        action.effect = -1;
        action.power = turned_on(action.power);
    } else if (has_effect(entry)) {
        const int index = catalog_find(entry.effect);
        if (index >= 0) {
            action.effect = index;
            action.playlist = false;
            action.power = turned_on(action.power);
            action.effect_without_playlist = index;
            action.power_without_playlist = turned_on(action.power_without_playlist);
        }
    }
    if (entry.power != PowerStep::None) {
        action.power = entry.power;
        action.power_without_playlist = entry.power;
    }
}

void combine_minutes(ResolvedAction& action, const ResolvedAction& later) {
    if (!later.any) {
        return;
    }
    action.any = true;
    if (later.brightness_percent != 0) {
        action.brightness_percent = later.brightness_percent;
    }
    if (later.speed_x4 != 0) {
        action.speed_x4 = later.speed_x4;
    }
    if (later.effect >= 0 || later.playlist) {
        action.effect = later.effect;
        action.playlist = later.playlist;
    }
    if (later.power != PowerStep::None) {
        action.power = later.power;
    }
    if (later.effect_without_playlist >= 0) {
        action.effect_without_playlist = later.effect_without_playlist;
    }
    if (later.power_without_playlist != PowerStep::None) {
        action.power_without_playlist = later.power_without_playlist;
    }
}

// Each minute is worked out on its own, then the minutes one after another: an effect started in a
// minute keeps a rise asked for in that minute, never one from a minute before it, which the garland,
// running all along, would have carried out apart.
ResolvedAction resolve_minutes(const ScheduleEntry* entries, size_t count, int64_t first_minute, int64_t last_minute,
                               LocalMinuteFn to_local) {
    ResolvedAction action;
    if (count == 0) {
        return action;
    }
    for (int64_t minute = first_minute; minute <= last_minute; ++minute) {
        const LocalMinute local = to_local(minute);
        ResolvedAction in_minute;
        for (size_t i = 0; i < count; ++i) {
            if (entry_matches(entries[i], local)) {
                merge_entry(in_minute, entries[i]);
            }
        }
        combine_minutes(action, in_minute);
    }
    return action;
}

int64_t next_doing_minute(const ScheduleEntry* entries, size_t count, int64_t found, int64_t until, LocalMinuteFn to_local, bool ready,
                          ResolvedAction& action) {
    while (found >= 0 && found <= until) {
        action = resolve_minutes(entries, count, found, found, to_local).with_playlist(ready);
        if (action.does_something()) {
            return found;
        }
        // No further than until: a step that only starts an empty playlist, weekly, would otherwise walk
        // to its own next week each time.
        found = next_action_minute(entries, count, found, to_local, until);
    }
    return -1;
}

int64_t next_action_minute(const ScheduleEntry* entries, size_t count, int64_t after_minute, LocalMinuteFn to_local, int64_t until) {
    if (count == 0 || std::none_of(entries, entries + count, [](const ScheduleEntry& e) { return e.enabled; })) {
        return -1;
    }
    constexpr int64_t kMinutesInAWeek = 7 * 24 * 60;
    const int64_t last = until >= 0 && until < after_minute + kMinutesInAWeek ? until : after_minute + kMinutesInAWeek;
    for (int64_t minute = after_minute + 1; minute <= last; ++minute) {
        const LocalMinute local = to_local(minute);
        for (size_t i = 0; i < count; ++i) {
            if (entry_matches(entries[i], local)) {
                return minute;
            }
        }
    }
    return -1;
}

bool schedule_has_newer_steps(const ScheduleEntry* entries, size_t count) {
    return std::any_of(entries, entries + count, [](const ScheduleEntry& entry) {
        return entry.power == PowerStep::Rise || starts_playlist(entry);
    });
}

const char* power_step_name(PowerStep step) {
    switch (step) {
        case PowerStep::On:
            return "on";
        case PowerStep::Off:
            return "off";
        case PowerStep::Rise:
            return "rise";
        case PowerStep::None:
            break;
    }
    return "";
}

size_t schedule_timeline(const ScheduleEntry* entries, size_t count, int64_t after_minute, LocalMinuteFn to_local,
                         TimelineEvent* out, size_t max_events, bool playlist_ready, int64_t last_minute) {
    size_t found = 0;
    const int64_t last = last_minute >= 0 ? last_minute : after_minute + kTimelineMinutes;
    if (count == 0 || std::none_of(entries, entries + count, [](const ScheduleEntry& e) { return e.enabled; })) {
        return 0;
    }
    for (int64_t minute = after_minute + 1; minute <= last && found < max_events; ++minute) {
        const LocalMinute local = to_local(minute);
        TimelineEvent event;
        for (size_t i = 0; i < count; ++i) {
            if (entry_matches(entries[i], local)) {
                merge_entry(event.action, entries[i]);
                event.entries |= uint32_t{1} << i;
                ++event.actions;
            }
        }
        if (event.actions > 0) {
            event.action = event.action.with_playlist(playlist_ready);
            event.minute = minute;
            event.local = local;
            out[found++] = event;
        }
    }
    return found;
}

cJSON* timeline_to_json(const TimelineEvent* events, size_t count) {
    std::unique_ptr<cJSON, ScheduleJsonDeleter> array(cJSON_CreateArray());
    if (array == nullptr) {
        return nullptr;
    }
    // Whole or not at all: a part that ran out of memory would leave a day a page cannot draw, which it
    // would take for the day.
    bool whole = true;
    for (size_t i = 0; i < count && whole; ++i) {
        const TimelineEvent& event = events[i];
        cJSON* item = cJSON_CreateObject();
        if (item == nullptr || !cJSON_AddItemToArray(array.get(), item)) {
            cJSON_Delete(item);
            return nullptr;
        }
        cJSON* indices = cJSON_AddArrayToObject(item, "entries");
        char time[6];
        std::snprintf(time, sizeof(time), "%02u:%02u", static_cast<unsigned>(event.local.hour) % 24,
                      static_cast<unsigned>(event.local.minute) % 60);
        whole = indices != nullptr && cJSON_AddNumberToObject(item, "at", static_cast<double>(event.minute * 60)) != nullptr &&
                cJSON_AddStringToObject(item, "time", time) != nullptr && cJSON_AddNumberToObject(item, "day", event.local.weekday) != nullptr;
        for (size_t entry = 0; whole && entry < kMaxScheduleEntries; ++entry) {
            if ((event.entries & (uint32_t{1} << entry)) != 0) {
                cJSON* number = cJSON_CreateNumber(static_cast<double>(entry));
                whole = number != nullptr && cJSON_AddItemToArray(indices, number);
                if (!whole) {
                    cJSON_Delete(number);
                }
            }
        }
        if (whole && event.action.power != PowerStep::None) {
            whole = cJSON_AddStringToObject(item, "power", power_step_name(event.action.power)) != nullptr;
        }
        if (whole && event.action.brightness_percent != 0) {
            whole = cJSON_AddNumberToObject(item, "brightness", event.action.brightness_percent) != nullptr;
        }
        if (whole && event.action.speed_x4 != 0) {
            whole = cJSON_AddNumberToObject(item, "speed", event.action.speed_x4 / 4.0) != nullptr;
        }
        if (whole && event.action.playlist) {
            whole = cJSON_AddBoolToObject(item, "playlist", true) != nullptr;
        } else if (whole && event.action.effect >= 0) {
            whole = cJSON_AddStringToObject(item, "effect", catalog_id(static_cast<uint16_t>(event.action.effect)).c_str()) != nullptr;
        }
    }
    if (!whole) {
        return nullptr;
    }
    return array.release();
}

bool schedule_from_json(const cJSON* array, ScheduleEntry* out, size_t& out_count, const char*& error, const ScheduleEntry* held,
                        size_t held_count) {
    if (!cJSON_IsArray(array)) {
        error = "entries must be a list";
        return false;
    }
    const int size = cJSON_GetArraySize(array);
    if (size < 0 || static_cast<size_t>(size) > kMaxScheduleEntries) {
        error = "too many actions";
        return false;
    }
    int index = 0;
    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, array) {
        if (!cJSON_IsObject(item) || !parse_entry(item, out[index], error, held, held_count)) {
            if (!cJSON_IsObject(item)) {
                error = "every action must be an object";
            }
            return false;
        }
        ++index;
    }
    out_count = static_cast<size_t>(index);
    return true;
}

cJSON* schedule_missing_json(const ScheduleEntry* entries, size_t count) {
    cJSON* missing = cJSON_CreateArray();
    for (size_t i = 0; missing != nullptr && i < count; ++i) {
        if (!has_effect(entries[i]) || catalog_find(entries[i].effect) >= 0 || held_names(entries, i, entries[i].effect)) {
            continue;
        }
        cJSON* id = cJSON_CreateString(entries[i].effect);
        if (id == nullptr || !cJSON_AddItemToArray(missing, id)) {
            cJSON_Delete(id);
            cJSON_Delete(missing);
            return nullptr;
        }
    }
    return missing;
}

cJSON* schedule_to_json(const ScheduleEntry* entries, size_t count) {
    std::unique_ptr<cJSON, ScheduleJsonDeleter> array(cJSON_CreateArray());
    if (array == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < count; ++i) {
        const ScheduleEntry& entry = entries[i];
        cJSON* item = cJSON_CreateObject();
        if (item == nullptr) {
            return nullptr;
        }
        cJSON_AddItemToArray(array.get(), item);
        char time[6];
        std::snprintf(time, sizeof(time), "%02u:%02u", static_cast<unsigned>(entry.hour) % 24,
                      static_cast<unsigned>(entry.minute) % 60);
        cJSON_AddBoolToObject(item, "enabled", entry.enabled);
        cJSON_AddStringToObject(item, "time", time);
        cJSON_AddNumberToObject(item, "days", entry.days);
        if (entry.power != PowerStep::None) {
            cJSON_AddStringToObject(item, "power", power_step_name(entry.power));
        }
        if (entry.brightness_percent != 0) {
            cJSON_AddNumberToObject(item, "brightness", entry.brightness_percent);
        }
        if (entry.speed_x4 != 0) {
            cJSON_AddNumberToObject(item, "speed", entry.speed_x4 / 4.0);
        }
        if (starts_playlist(entry)) {
            cJSON_AddBoolToObject(item, "playlist", true);
        } else if (has_effect(entry)) {
            cJSON_AddStringToObject(item, "effect", entry.effect);
        }
    }
    return array.release();
}

bool time_zone_is_plausible(const char* zone) {
    const size_t length = std::strlen(zone);
    if (length < 3 || length > kMaxTimeZoneLength) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        const auto c = static_cast<unsigned char>(zone[i]);
        if (std::isalnum(c) == 0 && std::strchr("<>+-:,./", c) == nullptr) {
            return false;
        }
    }
    return true;
}

}  // namespace garland
