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

bool parse_entry(const cJSON* item, ScheduleEntry& entry, const char*& error) {
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
        } else {
            error = "power must be on or off";
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
    const cJSON* effect = cJSON_GetObjectItemCaseSensitive(item, "effect");
    if (effect != nullptr) {
        if (!cJSON_IsString(effect) || catalog_find(effect->valuestring) < 0) {
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
    if (has_effect(entry)) {
        const int index = catalog_find(entry.effect);
        if (index >= 0) {
            action.effect = index;
            action.power = PowerStep::On;
        }
    }
    if (entry.power != PowerStep::None) {
        action.power = entry.power;
    }
}

ResolvedAction resolve_minutes(const ScheduleEntry* entries, size_t count, int64_t first_minute, int64_t last_minute,
                               LocalMinuteFn to_local) {
    ResolvedAction action;
    if (count == 0) {
        return action;
    }
    for (int64_t minute = first_minute; minute <= last_minute; ++minute) {
        const LocalMinute local = to_local(minute);
        for (size_t i = 0; i < count; ++i) {
            if (entry_matches(entries[i], local)) {
                merge_entry(action, entries[i]);
            }
        }
    }
    return action;
}

int64_t next_action_minute(const ScheduleEntry* entries, size_t count, int64_t after_minute, LocalMinuteFn to_local) {
    if (count == 0 || std::none_of(entries, entries + count, [](const ScheduleEntry& e) { return e.enabled; })) {
        return -1;
    }
    constexpr int64_t kMinutesInAWeek = 7 * 24 * 60;
    for (int64_t minute = after_minute + 1; minute <= after_minute + kMinutesInAWeek; ++minute) {
        const LocalMinute local = to_local(minute);
        for (size_t i = 0; i < count; ++i) {
            if (entry_matches(entries[i], local)) {
                return minute;
            }
        }
    }
    return -1;
}

bool schedule_from_json(const cJSON* array, ScheduleEntry* out, size_t& out_count, const char*& error) {
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
        if (!cJSON_IsObject(item) || !parse_entry(item, out[index], error)) {
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
            cJSON_AddStringToObject(item, "power", entry.power == PowerStep::On ? "on" : "off");
        }
        if (entry.brightness_percent != 0) {
            cJSON_AddNumberToObject(item, "brightness", entry.brightness_percent);
        }
        if (entry.speed_x4 != 0) {
            cJSON_AddNumberToObject(item, "speed", entry.speed_x4 / 4.0);
        }
        if (has_effect(entry)) {
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
