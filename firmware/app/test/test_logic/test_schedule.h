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

// Scheduler24 tells of the soonest of what comes, a step of the schedule or the playlist's next effect; with
// nothing to come, of the latest of what came: the start, a turn on or off, the LEDs, a step done.
// A step that turns the garland on or off is told by its own name, not as the On or Off the screen sees at
// its next poll; one that came apart from it stays.
static void test_scheduler24_tells_a_step_by_its_name_not_its_power() {
    constexpr int64_t kNow = 100'000'000;
    const SaverMoment step = saver_moment(kNow - 60'000, "Fireplace");
    SaverMoment events[2] = {saver_moment(kNow - 60'000 + 25, "On"), saver_moment(kNow - 60'000 + 900, "LEDs on")};
    saver_fold_into_step(step, events, 2);
    TEST_ASSERT_FALSE(events[0].known || events[1].known);
    const SaverMoment behind[] = {saver_moment(0, "Start"), events[0], events[1], step};
    char text[kSaverTextBytes];
    saver_text(nullptr, 0, behind, 4, kNow, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("last: Fireplace\n1m 00s ago", text);
    // An Off of the owner's a minute after the step is the owner's, and the latest.
    SaverMoment later[1] = {saver_moment(kNow - 1'000, "Off")};
    saver_fold_into_step(step, later, 1);
    TEST_ASSERT_TRUE(later[0].known);
    // And one before the step is not the step's.
    SaverMoment before[1] = {saver_moment(kNow - 61'000, "On")};
    saver_fold_into_step(step, before, 1);
    TEST_ASSERT_TRUE(before[0].known);
}

static void test_scheduler24_says_what_comes_and_when() {
    // A day and some hours after the start: 27h 46m 40s.
    constexpr int64_t kNow = 100'000'000;
    char text[kSaverTextBytes];
    const SaverMoment step = saver_moment(kNow + (2 * 3600 + 5 * 60) * 1000, "Fireplace");
    // The playlist goes on in 45 s: sooner than the step, so it is the next.
    const SaverMoment playlist = saver_moment(kNow + 45 * 1000, "Candle");
    const SaverMoment ahead[] = {step, playlist};
    saver_text(ahead, 2, nullptr, 0, kNow, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("next: Candle\nin 45s", text);
    saver_text(ahead, 1, nullptr, 0, kNow, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("next: Fireplace\nin 2h 05m", text);
    // The playlist plays on, and a step comes before its next effect, which the step will take the place of:
    // the step is the next, as it comes first.
    const SaverMoment playing[] = {saver_moment(kNow + 20 * 1000, "Fireplace"), saver_moment(kNow + 45 * 1000, "Candle")};
    saver_text(playing, 2, nullptr, 0, kNow, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("next: Fireplace\nin 20s", text);
    // Counted up to the second, so "in 0s" comes only as the moment does.
    const SaverMoment soon = saver_moment(kNow + 1500, "Candle");
    saver_text(&soon, 1, nullptr, 0, kNow, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("next: Candle\nin 2s", text);
    // Gone by, a moment to come is none: the latest of what came, the start among them, is said.
    const SaverMoment behind[] = {saver_moment(0, "Start"), saver_moment(kNow - (3 * 3600 + 10 * 60) * 1000, "Off"), SaverMoment{}};
    saver_text(ahead, 2, behind, 3, kNow + 3 * 3600 * 1000, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("last: Off\n6h 10m ago", text);
    saver_text(nullptr, 0, behind, 1, kNow + 3 * 86400 * 1000LL, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("last: Start\n4d 03h ago", text);
    saver_text(nullptr, 0, nullptr, 0, kNow, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("nothing yet", text);
    // A step that stays the last for weeks, past 32 bits of ms and past their sign, keeps its age: whole 64-bit
    // moments, as the scheduler keeps them.
    const SaverMoment old_step[] = {saver_moment(0, "Start"), saver_moment(1000, "Brightness 50%")};
    saver_text(nullptr, 0, old_step, 2, 1000 + 25 * 86400 * 1000LL, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("last: Brightness 50%\n25d 00h ago", text);
    saver_text(nullptr, 0, old_step, 2, 1000 + 50 * 86400 * 1000LL, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("last: Brightness 50%\n50d 00h ago", text);
    // A step that only starts a playlist with none to start does nothing, and is no event to tell of.
    ResolvedAction empty_start;
    empty_start.any = true;
    empty_start.playlist = true;
    TEST_ASSERT_FALSE(empty_start.with_playlist(false).does_something());

    // What a step does, in its order: the effect by name, not the turn on with it, the playlist, then the
    // power, the brightness, the speed.
    const auto name = [](int effect, char* out, size_t size) { std::snprintf(out, size, "%s", effect == 3 ? "Fireplace" : "?"); };
    char words[kSaverNameBytes];
    ResolvedAction action;
    action.effect = 3;
    action.power = PowerStep::On;
    saver_action_name(action, name, words, sizeof(words));
    TEST_ASSERT_EQUAL_STRING("Fireplace", words);
    action = ResolvedAction{};
    action.playlist = true;
    action.power = PowerStep::On;
    saver_action_name(action, name, words, sizeof(words));
    TEST_ASSERT_EQUAL_STRING("Playlist", words);
    action = ResolvedAction{};
    action.power = PowerStep::Rise;
    saver_action_name(action, name, words, sizeof(words));
    TEST_ASSERT_EQUAL_STRING("Rise", words);
    action = ResolvedAction{};
    action.brightness_percent = 40;
    action.speed_x4 = 6;
    saver_action_name(action, name, words, sizeof(words));
    TEST_ASSERT_EQUAL_STRING("Brightness 40%", words);
    action.brightness_percent = 0;
    saver_action_name(action, name, words, sizeof(words));
    TEST_ASSERT_EQUAL_STRING("Speed 1.5x", words);
    char span[24];
    saver_span_text(-3, span, sizeof(span));
    TEST_ASSERT_EQUAL_STRING("0s", span);
    saver_span_text(125, span, sizeof(span));
    TEST_ASSERT_EQUAL_STRING("2m 05s", span);
    TEST_ASSERT_TRUE(saver_kind_is_valid(static_cast<int>(SaverKind::Scheduler24)));
    TEST_ASSERT_FALSE(saver_kind_is_valid(static_cast<int>(SaverKind::Scheduler24) + 1));
}

// The Lighting task says when the playlist goes on, for Scheduler24, and 0 while it does not.
static void test_the_state_holds_when_the_playlist_goes_on() {
    State state(32);
    TEST_ASSERT_EQUAL_UINT32(0, state.playlist_next_ms());
    state.set_playlist_next_ms(61'000);
    TEST_ASSERT_EQUAL_UINT32(61'000, state.playlist_next_ms());
}
static void run_schedule_tests() {
    RUN_TEST(test_schedule_entry_rules);
    RUN_TEST(test_the_state_holds_when_the_playlist_goes_on);
    RUN_TEST(test_scheduler24_says_what_comes_and_when);
    RUN_TEST(test_scheduler24_tells_a_step_by_its_name_not_its_power);
    RUN_TEST(test_schedule_matching);
    RUN_TEST(test_schedule_effect_starts_the_garland);
    RUN_TEST(test_schedule_replay_gives_the_final_state);
    RUN_TEST(test_schedule_json_round_trip);
    RUN_TEST(test_schedule_json_rejects_bad_lists);
    RUN_TEST(test_time_zone_strings);
    RUN_TEST(test_schedule_sets_only_what_it_names);
}
