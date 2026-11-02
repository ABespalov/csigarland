/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Host tests for the schedule, its JSON and the time zone text
 * Contributors: None
 *
 * Included by test_main.cpp.
 */

#pragma once

// Minute 0 is Thursday 1970-01-01 00:00; the tests run on UTC.
static LocalMinute utc_minute(int64_t minute) {
    const int64_t day = minute / 1440;
    return LocalMinute{static_cast<uint8_t>((day + 4) % 7), static_cast<uint8_t>((minute % 1440) / 60),
                       static_cast<uint8_t>(minute % 60)};
}

static ScheduleEntry make_entry(uint8_t hour, uint8_t minute, PowerStep power, uint8_t brightness = 0,
                                uint8_t speed = 0, const char* effect = "") {
    ScheduleEntry entry{};
    entry.enabled = true;
    entry.hour = hour;
    entry.minute = minute;
    entry.days = kAllDays;
    entry.power = power;
    entry.brightness_percent = brightness;
    entry.speed_x4 = speed;
    std::snprintf(entry.effect, sizeof(entry.effect), "%s", effect);
    return entry;
}

static void test_schedule_entry_rules() {
    const char* error = nullptr;
    TEST_ASSERT_TRUE(entry_is_valid(make_entry(17, 30, PowerStep::On), error));
    TEST_ASSERT_TRUE(entry_is_valid(make_entry(17, 30, PowerStep::Off), error));
    TEST_ASSERT_TRUE(entry_is_valid(make_entry(17, 30, PowerStep::On, 50, 6, "fireplace"), error));
    TEST_ASSERT_TRUE(entry_is_valid(make_entry(17, 30, PowerStep::None, 50), error));

    // Nothing can go with "off".
    TEST_ASSERT_FALSE(entry_is_valid(make_entry(17, 30, PowerStep::Off, 50), error));
    TEST_ASSERT_FALSE(entry_is_valid(make_entry(17, 30, PowerStep::Off, 0, 15), error));
    TEST_ASSERT_FALSE(entry_is_valid(make_entry(17, 30, PowerStep::Off, 0, 0, "fireplace"), error));

    // An action without steps, and impossible values.
    TEST_ASSERT_FALSE(entry_is_valid(make_entry(17, 30, PowerStep::None), error));
    TEST_ASSERT_FALSE(entry_is_valid(make_entry(24, 0, PowerStep::On), error));
    TEST_ASSERT_FALSE(entry_is_valid(make_entry(0, 60, PowerStep::On), error));
    TEST_ASSERT_FALSE(entry_is_valid(make_entry(1, 0, PowerStep::None, 101), error));
    TEST_ASSERT_FALSE(entry_is_valid(make_entry(1, 0, PowerStep::None, 0, 5), error));
    ScheduleEntry no_days = make_entry(1, 0, PowerStep::On);
    no_days.days = 0;
    TEST_ASSERT_FALSE(entry_is_valid(no_days, error));
}

static void test_schedule_matching() {
    ScheduleEntry entry = make_entry(17, 30, PowerStep::On);
    TEST_ASSERT_TRUE(entry_matches(entry, LocalMinute{4, 17, 30}));
    TEST_ASSERT_FALSE(entry_matches(entry, LocalMinute{4, 17, 31}));
    TEST_ASSERT_FALSE(entry_matches(entry, LocalMinute{4, 18, 30}));

    entry.days = 1u << 1;  // Mondays only.
    TEST_ASSERT_TRUE(entry_matches(entry, LocalMinute{1, 17, 30}));
    TEST_ASSERT_FALSE(entry_matches(entry, LocalMinute{2, 17, 30}));

    entry.days = kAllDays;
    entry.enabled = false;
    TEST_ASSERT_FALSE(entry_matches(entry, LocalMinute{1, 17, 30}));
}

static void test_schedule_effect_starts_the_garland() {
    load_shipped_catalog();
    ResolvedAction action;
    merge_entry(action, make_entry(1, 0, PowerStep::None, 0, 0, catalog_effect(1).id));
    TEST_ASSERT_TRUE(action.power == PowerStep::On);
    TEST_ASSERT_EQUAL_INT(1, action.effect);

    // Brightness and speed alone change settings and leave the power alone.
    ResolvedAction settings;
    merge_entry(settings, make_entry(1, 0, PowerStep::None, 40, 8));
    TEST_ASSERT_TRUE(settings.power == PowerStep::None);
    TEST_ASSERT_EQUAL_UINT8(40, settings.brightness_percent);
    TEST_ASSERT_EQUAL_UINT8(8, settings.speed_x4);

    // An effect removed from a later catalog does nothing to the power.
    ResolvedAction stale;
    merge_entry(stale, make_entry(1, 0, PowerStep::None, 0, 0, "gone-effect"));
    TEST_ASSERT_TRUE(stale.power == PowerStep::None);
    TEST_ASSERT_EQUAL_INT(-1, stale.effect);
}

static void test_schedule_replay_gives_the_final_state() {
    // On at 17:30 at 75 %, off at 00:30. The power comes back at 20:00.
    const std::array<ScheduleEntry, 2> entries = {make_entry(17, 30, PowerStep::On, 75), make_entry(0, 30, PowerStep::Off)};
    const int64_t day = 1440 * 100;
    const int64_t now = day + 20 * 60;
    const ResolvedAction evening = resolve_minutes(entries.data(), entries.size(), now - 1439, now, utc_minute);
    TEST_ASSERT_TRUE(evening.any);
    TEST_ASSERT_TRUE(evening.power == PowerStep::On);
    TEST_ASSERT_EQUAL_UINT8(75, evening.brightness_percent);

    // At 03:00 the last thing due was the "off".
    const int64_t night = day + 3 * 60;
    const ResolvedAction late = resolve_minutes(entries.data(), entries.size(), night - 1439, night, utc_minute);
    TEST_ASSERT_TRUE(late.power == PowerStep::Off);

    // Nothing due in a minute with no action.
    const ResolvedAction quiet = resolve_minutes(entries.data(), entries.size(), now, now, utc_minute);
    TEST_ASSERT_FALSE(quiet.any);
    // And no actions at all.
    TEST_ASSERT_FALSE(resolve_minutes(entries.data(), 0, now - 1439, now, utc_minute).any);
}

static void test_schedule_json_round_trip() {
    load_shipped_catalog();
    const std::string json = std::string(R"([{"time":"17:30","days":62,"power":"on","brightness":40,"speed":1.5,"effect":")") +
                             catalog_effect(2).id + R"("},{"time":"00:05","power":"off","enabled":false}])";
    const JsonPtrForTest parsed(cJSON_Parse(json.c_str()));
    std::array<ScheduleEntry, kMaxScheduleEntries> entries{};
    size_t count = 0;
    const char* error = nullptr;
    TEST_ASSERT_TRUE_MESSAGE(schedule_from_json(parsed.get(), entries.data(), count, error), error);
    TEST_ASSERT_EQUAL_size_t(2, count);
    TEST_ASSERT_EQUAL_UINT8(17, entries[0].hour);
    TEST_ASSERT_EQUAL_UINT8(30, entries[0].minute);
    TEST_ASSERT_EQUAL_UINT8(62, entries[0].days);
    TEST_ASSERT_EQUAL_UINT8(40, entries[0].brightness_percent);
    TEST_ASSERT_EQUAL_UINT8(6, entries[0].speed_x4);
    TEST_ASSERT_TRUE(entries[1].power == PowerStep::Off);
    TEST_ASSERT_FALSE(entries[1].enabled);
    TEST_ASSERT_EQUAL_UINT8(kAllDays, entries[1].days);

    const JsonPtrForTest back(schedule_to_json(entries.data(), count));
    std::array<ScheduleEntry, kMaxScheduleEntries> again{};
    size_t again_count = 0;
    TEST_ASSERT_TRUE_MESSAGE(schedule_from_json(back.get(), again.data(), again_count, error), error);
    TEST_ASSERT_EQUAL_size_t(count, again_count);
    TEST_ASSERT_EQUAL_MEMORY(entries.data(), again.data(), sizeof(ScheduleEntry) * count);
}

static void test_schedule_json_rejects_bad_lists() {
    load_shipped_catalog();
    const auto rejected = [](const std::string& json) {
        const JsonPtrForTest parsed(cJSON_Parse(json.c_str()));
        std::array<ScheduleEntry, kMaxScheduleEntries> entries{};
        size_t count = 0;
        const char* error = nullptr;
        return !schedule_from_json(parsed.get(), entries.data(), count, error) && error != nullptr;
    };
    TEST_ASSERT_TRUE(rejected(R"({"time":"17:30"})"));
    TEST_ASSERT_TRUE(rejected(R"([{"power":"on"}])"));
    TEST_ASSERT_TRUE(rejected(R"([{"time":"7:30","power":"on"}])"));
    TEST_ASSERT_TRUE(rejected(R"([{"time":"25:00","power":"on"}])"));
    TEST_ASSERT_TRUE(rejected(R"([{"time":"17:30"}])"));
    TEST_ASSERT_TRUE(rejected(R"([{"time":"17:30","power":"maybe"}])"));
    TEST_ASSERT_TRUE(rejected(R"([{"time":"17:30","power":"off","brightness":50}])"));
    TEST_ASSERT_TRUE(rejected(R"([{"time":"17:30","brightness":0}])"));
    TEST_ASSERT_TRUE(rejected(R"([{"time":"17:30","speed":4}])"));
    // Close is not equal: nothing is rounded into a valid value.
    TEST_ASSERT_TRUE(rejected(R"([{"time":"17:30","speed":1.49}])"));
    TEST_ASSERT_TRUE(rejected(R"([{"time":"17:30","speed":0}])"));
    TEST_ASSERT_TRUE(rejected(R"([{"time":"17:30","brightness":50.9}])"));
    TEST_ASSERT_TRUE(rejected(R"([{"time":"17:30","power":"on","days":1.9}])"));
    TEST_ASSERT_TRUE(rejected(R"([{"time":"17:30","power":"on","days":0}])"));
    TEST_ASSERT_TRUE(rejected(R"([{"time":"17:30","power":"on","days":200}])"));
    // An effect the catalog does not have.
    TEST_ASSERT_TRUE(rejected(R"([{"time":"17:30","effect":"no-such-effect"}])"));

    std::string too_many = "[";
    for (size_t i = 0; i <= kMaxScheduleEntries; ++i) {
        too_many += std::string(i > 0 ? "," : "") + R"({"time":"12:00","power":"on"})";
    }
    TEST_ASSERT_TRUE(rejected(too_many + "]"));
}

static void test_time_zone_strings() {
    TEST_ASSERT_TRUE(time_zone_is_plausible("UTC0"));
    TEST_ASSERT_TRUE(time_zone_is_plausible("MSK-3"));
    TEST_ASSERT_TRUE(time_zone_is_plausible("<+03>-3"));
    TEST_ASSERT_TRUE(time_zone_is_plausible("CET-1CEST,M3.5.0,M10.5.0/3"));
    TEST_ASSERT_FALSE(time_zone_is_plausible(""));
    TEST_ASSERT_FALSE(time_zone_is_plausible("x\"y"));
    TEST_ASSERT_FALSE(time_zone_is_plausible("UTC 0"));
    TEST_ASSERT_FALSE(time_zone_is_plausible(std::string(49, 'A').c_str()));
}

static void test_schedule_sets_only_what_it_names() {
    State state;
    state.set_effect_count(3);
    state.set_brightness(40);
    state.set_speed(8);
    state.set_effect(2);
    const Snapshot s = state.snapshot();
    TEST_ASSERT_FALSE(s.power_requested);
    TEST_ASSERT_EQUAL_UINT8(40, s.brightness_percent);
    TEST_ASSERT_EQUAL_UINT8(8, s.speed_x4);
    TEST_ASSERT_EQUAL_UINT8(2, s.effect);
    state.set_power(true);
    TEST_ASSERT_TRUE(state.snapshot().power_requested);
    state.set_power(false);
    TEST_ASSERT_FALSE(state.snapshot().power_requested);
}

static void run_schedule_tests() {
    RUN_TEST(test_schedule_entry_rules);
    RUN_TEST(test_schedule_matching);
    RUN_TEST(test_schedule_effect_starts_the_garland);
    RUN_TEST(test_schedule_replay_gives_the_final_state);
    RUN_TEST(test_schedule_json_round_trip);
    RUN_TEST(test_schedule_json_rejects_bad_lists);
    RUN_TEST(test_time_zone_strings);
    RUN_TEST(test_schedule_sets_only_what_it_names);
}
