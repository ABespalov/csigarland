/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Host tests for fades, cross-fades, the playlist, the scheduled rise and the timeline
 * Contributors: None
 *
 * Included by test_main.cpp, after test_schedule.h, whose helpers it uses.
 */

#pragma once

static void test_a_fade_rises_and_falls_at_its_pace() {
    LightFade fade;
    TEST_ASSERT_EQUAL_UINT32(0, fade.level(0));
    fade.go(LightFade::kFull, 800, 0);
    TEST_ASSERT_EQUAL_UINT32(0, fade.level(0));
    TEST_ASSERT_UINT32_WITHIN(2, LightFade::kFull / 2, fade.level(400'000));
    TEST_ASSERT_EQUAL_UINT32(LightFade::kFull, fade.level(800'000));
    TEST_ASSERT_EQUAL_UINT32(LightFade::kFull, fade.level(5'000'000));
    // Turned off halfway up: it goes back down from there, in half the time.
    LightFade half;
    half.go(LightFade::kFull, 800, 0);
    half.go(0, 800, 400'000);
    TEST_ASSERT_FALSE(half.dark(500'000));
    TEST_ASSERT_TRUE(half.dark(800'001));
    // The light is the level squared, so the dim end goes slowly.
    TEST_ASSERT_EQUAL_UINT32(kFullBrightnessGain, fade.apply(kFullBrightnessGain, 2'000'000));
    LightFade rise;
    rise.go(LightFade::kFull, kRiseMs, 0);
    const uint32_t tenth = rise.apply(kFullBrightnessGain, int64_t{kRiseMs} * 100);
    TEST_ASSERT_UINT32_WITHIN(kFullBrightnessGain / 200, kFullBrightnessGain / 100, tenth);
    TEST_ASSERT_EQUAL_UINT32(0, rise.apply(kFullBrightnessGain, 0));
}

static void test_a_cross_fade_mixes_the_old_frame_into_the_new() {
    const std::array<Rgb, 3> from{{{200, 0, 0}, {0, 200, 0}, {10, 10, 10}}};
    std::array<Rgb, 4> frame{{{0, 0, 200}, {0, 0, 200}, {0, 0, 200}, {7, 7, 7}}};
    cross_fade(from, frame, 0);
    TEST_ASSERT_TRUE((frame[0] == Rgb{200, 0, 0}));
    frame = {{{0, 0, 200}, {0, 0, 200}, {0, 0, 200}, {7, 7, 7}}};
    cross_fade(from, frame, 32768);
    TEST_ASSERT_TRUE((frame[0] == Rgb{100, 0, 100}));
    // Past the old frame's end, the new one shows as it is.
    TEST_ASSERT_TRUE((frame[3] == Rgb{7, 7, 7}));
    TEST_ASSERT_EQUAL_UINT32(0, cross_fade_weight(0, 1000));
    TEST_ASSERT_EQUAL_UINT32(32768, cross_fade_weight(500, 1000));
    TEST_ASSERT_EQUAL_UINT32(65536, cross_fade_weight(1000, 1000));
    TEST_ASSERT_EQUAL_UINT32(65536, cross_fade_weight(0, 0));
}

static void test_the_playlist_moves_on_and_yields_to_a_choice() {
    const uint16_t list[] = {4, 9, 2};
    TEST_ASSERT_EQUAL_INT(9, playlist_next(list, 3, 4));
    TEST_ASSERT_EQUAL_INT(4, playlist_next(list, 3, 2));
    TEST_ASSERT_EQUAL_INT(4, playlist_next(list, 3, 7));
    TEST_ASSERT_EQUAL_INT(-1, playlist_next(list, 0, 4));

    State state;
    state.set_effect_count(12);
    TEST_ASSERT_FALSE(state.play_playlist(true));
    state.set_playlist(list, 3, 30);
    TEST_ASSERT_TRUE(state.play_playlist(true));
    Snapshot now = state.snapshot();
    TEST_ASSERT_TRUE(now.playlist && now.power_requested);
    TEST_ASSERT_EQUAL_UINT16(4, now.effect);
    const uint8_t mark = now.playlist_mark;
    TEST_ASSERT_TRUE(state.advance_playlist(4, 9, mark));
    TEST_ASSERT_EQUAL_UINT16(9, state.snapshot().effect);
    // One step worked out from 4 is stale once 9 plays.
    TEST_ASSERT_FALSE(state.advance_playlist(4, 9, mark));
    // A choice of one effect ends it, and a step after that changes nothing.
    state.activate_effect(7);
    TEST_ASSERT_FALSE(state.snapshot().playlist);
    TEST_ASSERT_FALSE(state.advance_playlist(7, 4, state.snapshot().playlist_mark));
    TEST_ASSERT_EQUAL_UINT16(7, state.snapshot().effect);
    // So does a scheduled effect, while brightness alone leaves it playing. The scheduler takes the
    // owner's choice before it works a step out, as starting the playlist was one.
    TEST_ASSERT_TRUE(state.play_playlist(true));
    state.take_owner_choice();
    TEST_ASSERT_TRUE(state.apply_scheduled(30, 0, -1, kPowerKeep));
    TEST_ASSERT_TRUE(state.snapshot().playlist);
    TEST_ASSERT_TRUE(state.apply_scheduled(0, 0, 2, kPowerKeep));
    TEST_ASSERT_FALSE(state.snapshot().playlist);
    // Stopping keeps what plays; an empty list stops it too.
    TEST_ASSERT_TRUE(state.play_playlist(true));
    TEST_ASSERT_TRUE(state.play_playlist(false));
    TEST_ASSERT_FALSE(state.snapshot().playlist);
    TEST_ASSERT_TRUE(state.snapshot().power_requested);
    TEST_ASSERT_TRUE(state.play_playlist(true));
    state.set_playlist(list, 0, 30);
    TEST_ASSERT_FALSE(state.snapshot().playlist);
}

static void test_only_a_scheduled_rise_rises() {
    State state;
    TEST_ASSERT_TRUE(state.apply_scheduled(0, 0, -1, kPowerRise));
    TEST_ASSERT_TRUE(state.snapshot().rise && state.snapshot().power_requested);
    state.set_power(false);
    state.set_power(true);
    TEST_ASSERT_FALSE(state.snapshot().rise);
    state.apply_action(0, 0, -1, kPowerOn);
    TEST_ASSERT_FALSE(state.snapshot().rise);
}

static void test_the_schedule_takes_a_rise() {
    ScheduleEntry entries[kMaxScheduleEntries];
    size_t count = 0;
    const char* error = nullptr;
    const JsonPtrForTest json(cJSON_Parse(R"([{"time":"07:00","days":127,"power":"rise","effect":"fireplace"}])"));
    TEST_ASSERT_TRUE(schedule_from_json(json.get(), entries, count, error));
    TEST_ASSERT_TRUE(entries[0].power == PowerStep::Rise);
    ResolvedAction action;
    merge_entry(action, entries[0]);
    TEST_ASSERT_TRUE(action.power == PowerStep::Rise);
    const JsonPtrForTest back(schedule_to_json(entries, count));
    TEST_ASSERT_EQUAL_STRING("rise", cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(back.get(), 0), "power")->valuestring);
    // A 1.0.0 list holds only the old steps, which read as they did.
    TEST_ASSERT_EQUAL_UINT8(1, static_cast<uint8_t>(PowerStep::On));
    TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(PowerStep::Off));
}

static void test_the_timeline_shows_the_next_day_and_where_actions_meet() {
    const ScheduleEntry entries[] = {
        make_entry(18, 0, PowerStep::On),
        make_entry(23, 0, PowerStep::Off),
        make_entry(18, 0, PowerStep::None, 40),
    };
    TimelineEvent events[kMaxTimelineEvents];
    // From 12:00 on a Thursday: 18:00 today, where two meet, then 23:00.
    const int64_t noon = 12 * 60;
    const size_t found = schedule_timeline(entries, 3, noon, utc_minute, events, kMaxTimelineEvents);
    TEST_ASSERT_EQUAL_UINT32(2, found);
    TEST_ASSERT_EQUAL_INT64(18 * 60, events[0].minute);
    TEST_ASSERT_EQUAL_UINT8(2, events[0].actions);
    TEST_ASSERT_EQUAL_UINT32(0b101, events[0].entries);
    TEST_ASSERT_TRUE(events[0].action.power == PowerStep::On);
    TEST_ASSERT_EQUAL_UINT8(40, events[0].action.brightness_percent);
    TEST_ASSERT_EQUAL_UINT8(1, events[1].actions);
    // A day ahead and no more: 18:00 tomorrow is past it from 19:00 today.
    TEST_ASSERT_EQUAL_UINT32(2, schedule_timeline(entries, 3, 19 * 60, utc_minute, events, kMaxTimelineEvents));
    TEST_ASSERT_EQUAL_INT64(23 * 60, events[0].minute);
    TEST_ASSERT_EQUAL_INT64(24 * 60 + 18 * 60, events[1].minute);
    const JsonPtrForTest json(timeline_to_json(events, 2));
    const cJSON* first = cJSON_GetArrayItem(json.get(), 0);
    TEST_ASSERT_EQUAL_STRING("23:00", cJSON_GetObjectItemCaseSensitive(first, "time")->valuestring);
    TEST_ASSERT_EQUAL_STRING("off", cJSON_GetObjectItemCaseSensitive(first, "power")->valuestring);
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(first, "entries"), 0)->valueint);
    TEST_ASSERT_EQUAL_UINT32(0, schedule_timeline(entries, 0, noon, utc_minute, events, kMaxTimelineEvents));
    // From the minute the scheduler has done, which may be behind the time asked, to a day from that time:
    // the 18:00 of a minute not done yet still comes, and so does tomorrow's, within the day of 18:01.
    TEST_ASSERT_EQUAL_UINT32(3, schedule_timeline(entries, 3, 18 * 60 - 1, utc_minute, events, kMaxTimelineEvents, true, 18 * 60 + 1 + kTimelineMinutes));
    TEST_ASSERT_EQUAL_INT64(18 * 60, events[0].minute);
    TEST_ASSERT_EQUAL_INT64(24 * 60 + 18 * 60, events[2].minute);
    // Done past the time asked, as after a clock stepped back: what was done does not come again.
    TEST_ASSERT_EQUAL_UINT32(1, schedule_timeline(entries, 3, 18 * 60 + 3, utc_minute, events, kMaxTimelineEvents, true, 17 * 60 + 59 + 6 * 60));
    TEST_ASSERT_EQUAL_INT64(23 * 60, events[0].minute);
}

static void test_the_playlist_reads_and_writes_its_text_and_json() {
    PlaylistSettings playlist;
    const char* error = nullptr;
    const JsonPtrForTest json(cJSON_Parse(R"({"effects":["fireplace","aurora"],"dwell":90,"cross_fade":2500})"));
    TEST_ASSERT_TRUE(playlist_from_json(json.get(), playlist, error));
    TEST_ASSERT_EQUAL_UINT32(2, playlist.count);
    char text[kPlaylistTextBytes];
    playlist_to_text(playlist, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("90;2500;fireplace,aurora", text);
    // An effect gone from the catalog is kept in a saved list by its id, and played once it is there;
    // bad numbers keep the defaults.
    const PlaylistSettings read = playlist_from_text("5;9000;aurora,gone-effect,fireplace");
    TEST_ASSERT_EQUAL_UINT32(3, read.count);
    TEST_ASSERT_EQUAL_STRING("gone-effect", read.effects[1]);
    TEST_ASSERT_EQUAL_UINT16(kPlaylistDwellDefaultSeconds, read.dwell_seconds);
    TEST_ASSERT_EQUAL_UINT16(kCrossFadeDefaultMs, read.cross_fade_ms);
    TEST_ASSERT_EQUAL_UINT32(0, playlist_from_text("").count);
    uint16_t positions[kMaxPlaylistEffects];
    TEST_ASSERT_EQUAL_UINT32(2, playlist_positions(read, positions));
    TEST_ASSERT_EQUAL_INT(catalog_find("aurora"), positions[0]);
    TEST_ASSERT_EQUAL_INT(catalog_find("fireplace"), positions[1]);
    // An editor that changes only the dwell sends the missing effect back: the list there now holds it,
    // so it stays. A list that never held it cannot bring it in.
    const JsonPtrForTest kept(cJSON_Parse(R"({"effects":["aurora","gone-effect","fireplace"],"dwell":120})"));
    TEST_ASSERT_FALSE(playlist_from_json(kept.get(), playlist, error));
    TEST_ASSERT_TRUE(playlist_from_json(kept.get(), playlist, error, &read));
    TEST_ASSERT_EQUAL_UINT32(3, playlist.count);
    TEST_ASSERT_EQUAL_UINT16(120, playlist.dwell_seconds);

    const char* const bad[] = {
        R"({"effects":["fireplace","fireplace"]})",
        R"({"effects":["no-such-effect"]})",
        R"({"effects":"fireplace"})",
        R"({"effects":[],"dwell":5})",
        R"({"effects":[],"cross_fade":6000})",
        R"({"effects":[],"dwell":60.5})",
    };
    for (const char* body : bad) {
        const JsonPtrForTest wrong(cJSON_Parse(body));
        TEST_ASSERT_FALSE_MESSAGE(playlist_from_json(wrong.get(), playlist, error), body);
    }
    const JsonPtrForTest back(playlist_to_json(read, true, 3));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(back.get(), "playing")));
    TEST_ASSERT_EQUAL_INT(3, cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(back.get(), "effects")));
    const cJSON* missing = cJSON_GetObjectItemCaseSensitive(back.get(), "missing");
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetArraySize(missing));
    TEST_ASSERT_EQUAL_STRING("gone-effect", cJSON_GetArrayItem(missing, 0)->valuestring);
    TEST_ASSERT_EQUAL_INT(3, cJSON_GetObjectItemCaseSensitive(back.get(), "revision")->valueint);
}

// What schedule_v1 holds reads in 1.0.0 as it meant there: a rise is kept as on, and marked apart.
static void test_a_saved_schedule_reads_the_same_in_1_0_0() {
    TEST_ASSERT_TRUE(schedule_power_as_saved(PowerStep::Rise) == PowerStep::On);
    TEST_ASSERT_TRUE(schedule_power_as_saved(PowerStep::Off) == PowerStep::Off);
    TEST_ASSERT_TRUE(schedule_power_as_read(PowerStep::On, true) == PowerStep::Rise);
    TEST_ASSERT_TRUE(schedule_power_as_read(PowerStep::On, false) == PowerStep::On);
    TEST_ASSERT_TRUE(schedule_power_as_read(PowerStep::Off, true) == PowerStep::Off);
    // 1.0.0 knew None, On and Off, as 0, 1 and 2: nothing saved now is any other.
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(2, static_cast<uint8_t>(schedule_power_as_saved(PowerStep::Rise)));

    ScheduleEntry entries[] = {make_entry(7, 0, PowerStep::On), make_entry(18, 0, PowerStep::Off)};
    TEST_ASSERT_FALSE(schedule_has_newer_steps(entries, 2));
    entries[0].power = PowerStep::Rise;
    TEST_ASSERT_TRUE(schedule_has_newer_steps(entries, 2));
    entries[0] = make_entry(7, 0, PowerStep::None, 0, 0, kPlaylistStep);
    TEST_ASSERT_TRUE(schedule_has_newer_steps(entries, 2));
}

// A scheduled start of an empty playlist turns nothing on; one with effects times its first from then.
static void test_an_empty_playlist_starts_nothing() {
    State state;
    state.set_effect_count(12);
    ResolvedAction action;
    merge_entry(action, make_entry(18, 0, PowerStep::None, 0, 0, kPlaylistStep));
    const ResolvedAction none = action.with_playlist(false);
    TEST_ASSERT_TRUE(none.power == PowerStep::None && !none.playlist && none.effect == -1);
    TEST_ASSERT_TRUE(state.apply_scheduled(0, 0, -1, kPowerKeep, true));
    TEST_ASSERT_FALSE(state.snapshot().power_requested);
    const uint8_t before = state.snapshot().playlist_mark;
    const uint16_t list[] = {5, 7};
    state.set_playlist(list, 2, 30);
    TEST_ASSERT_TRUE(state.apply_scheduled(0, 0, -1, kPowerOn, true));
    TEST_ASSERT_TRUE(state.snapshot().power_requested && state.snapshot().playlist);
    const uint8_t started = state.snapshot().playlist_mark;
    TEST_ASSERT_TRUE(started != before);
    // Started again on the effect already playing, it starts again: two starts show as well as one.
    TEST_ASSERT_TRUE(state.play_playlist(true));
    TEST_ASSERT_TRUE(state.snapshot().playlist_mark != started && state.snapshot().playlist_mark != before);
}

// Which entries rise goes by place, with the bytes it was saved with: two equal entries, one rising and one
// not, keep their own in either order, and a list saved since by 1.0.0 takes none of it.
static void test_rises_keep_to_their_own_entries() {
    for (const bool rise_first : {true, false}) {
        ScheduleEntry entries[] = {make_entry(7, 0, PowerStep::On, 0, 0, "aurora"), make_entry(7, 0, PowerStep::On, 0, 0, "aurora")};
        entries[rise_first ? 0 : 1].power = PowerStep::Rise;
        ScheduleEntry saved[2];
        for (size_t i = 0; i < 2; ++i) {
            saved[i] = entries[i];
            saved[i].power = schedule_power_as_saved(entries[i].power);
        }
        TEST_ASSERT_TRUE(saved[0] == saved[1]);
        uint8_t marks[kRiseMarksBytes];
        schedule_rise_marks(schedule_rise_mask(entries, 2), reinterpret_cast<const uint8_t*>(saved), sizeof(saved), 0, nullptr, 0, marks);
        const uint32_t read = schedule_rises_of(saved, 2, marks, sizeof(marks));
        for (size_t i = 0; i < 2; ++i) {
            TEST_ASSERT_TRUE(schedule_power_as_read(saved[i].power, (read >> i & 1) != 0) == entries[i].power);
        }
        // 1.0.0 saved the list again since: the marks name other bytes, and hold for none.
        saved[1].hour = 8;
        TEST_ASSERT_EQUAL_UINT32(0, schedule_rises_of(saved, 2, marks, sizeof(marks)));
    }
}

// The marks are written before the list: when the list's own write then fails, or is cut short, the list
// left is the one before, and keeps its own rises, and the list refused never comes into force.
static void test_a_list_refused_keeps_the_rises_before_it() {
    ScheduleEntry before[] = {make_entry(7, 0, PowerStep::On, 0, 0, "aurora")};
    ScheduleEntry wanted[] = {make_entry(8, 0, PowerStep::Rise, 0, 0, "aurora"), make_entry(9, 0, PowerStep::Off)};
    ScheduleEntry saved[2];
    for (size_t i = 0; i < 2; ++i) {
        saved[i] = wanted[i];
        saved[i].power = schedule_power_as_saved(wanted[i].power);
    }
    // The list before rose at 7:00.
    const uint32_t before_mask = 1;
    uint8_t marks[kRiseMarksBytes];
    schedule_rise_marks(schedule_rise_mask(wanted, 2), reinterpret_cast<const uint8_t*>(saved), sizeof(saved), before_mask,
                        reinterpret_cast<const uint8_t*>(before), sizeof(before), marks);
    TEST_ASSERT_EQUAL_UINT32(before_mask, schedule_rises_of(before, 1, marks, sizeof(marks)));
    TEST_ASSERT_EQUAL_UINT32(1, schedule_rises_of(saved, 2, marks, sizeof(marks)));
    // A build before the marks kept the rising entries whole: still read, by equality.
    TEST_ASSERT_EQUAL_UINT32(1, schedule_rises_of(before, 1, reinterpret_cast<const uint8_t*>(before), sizeof(before)));
}

// An action naming an effect the catalog lacks, as after an update of the effects, stays when the list
// that held it is saved again for a change of another action; it cannot be brought in new.
static void test_a_schedule_keeps_an_effect_its_catalog_lacks() {
    const ScheduleEntry held[] = {make_entry(7, 0, PowerStep::On, 0, 0, "gone-effect")};
    const JsonPtrForTest json(cJSON_Parse(R"([{"enabled":true,"time":"07:00","days":127,"power":"on","effect":"gone-effect"},)"
                                          R"({"enabled":true,"time":"23:00","days":127,"power":"off"}])"));
    ScheduleEntry out[kMaxScheduleEntries];
    size_t count = 0;
    const char* error = nullptr;
    TEST_ASSERT_FALSE(schedule_from_json(json.get(), out, count, error));
    TEST_ASSERT_TRUE(schedule_from_json(json.get(), out, count, error, held, 1));
    TEST_ASSERT_EQUAL_UINT32(2, count);
    TEST_ASSERT_EQUAL_STRING("gone-effect", out[0].effect);
    const JsonPtrForTest missing(schedule_missing_json(out, count));
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetArraySize(missing.get()));
    TEST_ASSERT_EQUAL_STRING("gone-effect", cJSON_GetArrayItem(missing.get(), 0)->valuestring);
}

// A save cut after its first write, or whose third failed, leaves two marks. The start after it keeps only
// the mark of the list in force, whichever that is, so no list 1.0.0 saves later can match the other.
static void test_a_start_keeps_only_the_mark_of_the_list_in_force() {
    ScheduleEntry before[] = {make_entry(7, 0, PowerStep::On, 0, 0, "aurora")};
    ScheduleEntry wanted[] = {make_entry(8, 0, PowerStep::On, 0, 0, "aurora")};
    uint8_t marks[kRiseMarksBytes];
    // The list being saved rises at 8:00, the one before at 7:00.
    schedule_rise_marks(1, reinterpret_cast<const uint8_t*>(wanted), sizeof(wanted), 1, reinterpret_cast<const uint8_t*>(before),
                        sizeof(before), marks);
    for (ScheduleEntry* in_force : {before, wanted}) {
        uint8_t tidy[kRiseMarksBytes];
        TEST_ASSERT_TRUE(schedule_rise_marks_tidy(in_force, 1, marks, sizeof(marks), tidy));
        TEST_ASSERT_EQUAL_UINT32(1, schedule_rises_of(in_force, 1, tidy, sizeof(tidy)));
        ScheduleEntry* other = in_force == before ? wanted : before;
        TEST_ASSERT_EQUAL_UINT32(0, schedule_rises_of(other, 1, tidy, sizeof(tidy)));
        // Tidy already: nothing to write.
        uint8_t again[kRiseMarksBytes];
        TEST_ASSERT_FALSE(schedule_rise_marks_tidy(in_force, 1, tidy, sizeof(tidy), again));
    }
    // Entries kept whole by a build before the marks stay until a save.
    uint8_t untouched[kRiseMarksBytes];
    TEST_ASSERT_FALSE(schedule_rise_marks_tidy(before, 1, reinterpret_cast<const uint8_t*>(before), sizeof(before), untouched));
}

// An empty playlist is saved as "60;1000;": text, and no effects, which takes no mark of playing on.
static void test_an_empty_list_names_no_effects() {
    PlaylistSettings empty;
    char text[kPlaylistTextBytes];
    playlist_to_text(empty, text, sizeof(text));
    TEST_ASSERT_TRUE(std::strlen(text) > 0);
    TEST_ASSERT_FALSE(playlist_text_has_effects(text));
    TEST_ASSERT_FALSE(playlist_text_has_effects(""));
    TEST_ASSERT_TRUE(playlist_text_has_effects("60;1000;aurora"));
}

// A next step that only starts an empty playlist does nothing, and the next that does is the one told.
static void test_the_next_step_passes_over_an_empty_start() {
    const ScheduleEntry entries[] = {
        make_entry(18, 0, PowerStep::None, 0, 0, kPlaylistStep),
        make_entry(23, 0, PowerStep::Off),
    };
    const int64_t noon = 12 * 60;
    const int64_t found = next_action_minute(entries, 2, noon, utc_minute);
    TEST_ASSERT_EQUAL_INT64(18 * 60, found);
    const int64_t week = noon + 7 * 1440;
    ResolvedAction action;
    TEST_ASSERT_EQUAL_INT64(23 * 60, next_doing_minute(entries, 2, found, week, utc_minute, false, action));
    TEST_ASSERT_TRUE(action.power == PowerStep::Off);
    TEST_ASSERT_EQUAL_INT64(18 * 60, next_doing_minute(entries, 2, found, week, utc_minute, true, action));
    TEST_ASSERT_TRUE(action.playlist);
    // Nothing but empty starts: nothing to tell.
    TEST_ASSERT_EQUAL_INT64(-1, next_doing_minute(entries, 1, found, week, utc_minute, false, action));
    // A walk goes no further than it is asked to: 18:00 tomorrow is past an hour after 23:00.
    TEST_ASSERT_EQUAL_INT64(-1, next_action_minute(entries, 2, 23 * 60, utc_minute, 24 * 60));
    TEST_ASSERT_EQUAL_INT64(1440 + 18 * 60, next_action_minute(entries, 2, 23 * 60, utc_minute));
    // However many empty starts come first, the step that does something is found.
    ScheduleEntry many[10];
    for (int i = 0; i < 9; ++i) {
        many[i] = make_entry(12, static_cast<uint8_t>(i + 1), PowerStep::None, 0, 0, kPlaylistStep);
    }
    many[9] = make_entry(12, 10, PowerStep::On);
    const int64_t first = next_action_minute(many, 10, noon, utc_minute);
    TEST_ASSERT_EQUAL_INT64(12 * 60 + 10, next_doing_minute(many, 10, first, week, utc_minute, false, action));
}

// A rise goes with an effect started in its own minute only: replayed, a later minute's effect turns on at
// the usual pace, as it would have running along.
static void test_a_rise_stays_in_its_minute() {
    const ScheduleEntry apart[] = {make_entry(8, 0, PowerStep::Rise), make_entry(8, 5, PowerStep::None, 0, 0, "aurora")};
    const ResolvedAction replayed = resolve_minutes(apart, 2, 7 * 60, 9 * 60, utc_minute);
    TEST_ASSERT_TRUE(replayed.power == PowerStep::On);
    TEST_ASSERT_EQUAL_INT(catalog_find("aurora"), replayed.effect);
    const ScheduleEntry together[] = {make_entry(8, 0, PowerStep::Rise), make_entry(8, 0, PowerStep::None, 0, 0, "aurora")};
    TEST_ASSERT_TRUE(resolve_minutes(together, 2, 7 * 60, 9 * 60, utc_minute).power == PowerStep::Rise);
    // A later minute naming only brightness leaves what came before it.
    const ScheduleEntry dimmed[] = {make_entry(8, 0, PowerStep::Rise), make_entry(8, 5, PowerStep::None, 30)};
    const ResolvedAction kept = resolve_minutes(dimmed, 2, 7 * 60, 9 * 60, utc_minute);
    TEST_ASSERT_TRUE(kept.power == PowerStep::Rise);
    TEST_ASSERT_EQUAL_UINT8(30, kept.brightness_percent);
}
// A frame is rendered, and held to the current limit, before one goes out over a chain of another length:
// otherwise a longer chain would send a tail from an older frame, which the limit never saw whole.
static void test_a_new_chain_length_is_rendered_before_it_is_sent() {
    TEST_ASSERT_FALSE(frame_render_due(10'000, 8'000, 16'000, 32, 32));
    TEST_ASSERT_TRUE(frame_render_due(10'000, 8'000, 16'000, 256, 32));
    TEST_ASSERT_TRUE(frame_render_due(10'000, 8'000, 16'000, 32, 256));
    TEST_ASSERT_TRUE(frame_render_due(24'000, 8'000, 16'000, 32, 32));
}

// The list and whether it was left playing are one record, written whole: a list emptied takes the mark
// with it, so the same list saved again without Play is not resumed, and a record saved with the list
// that replaced it carries the mark on. The mark counts only with remembered as it was, and a list that
// names an effect.
static void test_a_playlist_resumes_only_with_its_own_record() {
    StoredSettings remembered;
    std::snprintf(remembered.effect_id, sizeof(remembered.effect_id), "%s", "aurora");
    remembered.playing = 1;
    uint8_t record[kPlaylistRecordBytes];
    char text[kPlaylistTextBytes];
    bool resumes = false;
    size_t bytes = playlist_record_make("60;1000;aurora,fireplace", &remembered, 1, record, sizeof(record));
    TEST_ASSERT_TRUE(playlist_record_read(record, bytes, text, sizeof(text), &remembered, resumes));
    TEST_ASSERT_TRUE(resumes);
    TEST_ASSERT_EQUAL_STRING("60;1000;aurora,fireplace", text);
    // 1.0.0 remembered another effect since.
    StoredSettings changed = remembered;
    changed.brightness_percent = 40;
    TEST_ASSERT_TRUE(playlist_record_read(record, bytes, text, sizeof(text), &changed, resumes));
    TEST_ASSERT_FALSE(resumes);
    // Emptied: an empty list takes no mark, and the same list saved again after it has none to find.
    bytes = playlist_record_make("60;1000;", &remembered, 1, record, sizeof(record));
    TEST_ASSERT_TRUE(playlist_record_read(record, bytes, text, sizeof(text), &remembered, resumes));
    TEST_ASSERT_FALSE(resumes);
    bytes = playlist_record_make("60;1000;aurora,fireplace", nullptr, 0, record, sizeof(record));
    TEST_ASSERT_TRUE(playlist_record_read(record, bytes, text, sizeof(text), &remembered, resumes));
    TEST_ASSERT_FALSE(resumes);
    // The text alone, as builds before the record kept it, reads as a list not left playing.
    const char legacy[] = "60;1000;aurora";
    TEST_ASSERT_TRUE(playlist_record_read(reinterpret_cast<const uint8_t*>(legacy), sizeof(legacy), text, sizeof(text), &remembered, resumes));
    TEST_ASSERT_FALSE(resumes);
    TEST_ASSERT_EQUAL_STRING(legacy, text);
    // Marked for remembered as it was and as it is about to be: whichever a power cut leaves, it resumes,
    // and 1.0.0 changing it since leaves neither.
    StoredSettings marks[2] = {remembered, changed};
    bytes = playlist_record_make("60;1000;aurora,fireplace", marks, 2, record, sizeof(record));
    for (const StoredSettings* now : {&remembered, &changed}) {
        TEST_ASSERT_TRUE(playlist_record_read(record, bytes, text, sizeof(text), now, resumes));
        TEST_ASSERT_TRUE(resumes);
    }
    StoredSettings other = remembered;
    other.speed_x4 = 8;
    TEST_ASSERT_TRUE(playlist_record_read(record, bytes, text, sizeof(text), &other, resumes));
    TEST_ASSERT_FALSE(resumes);
    // Anything else is no record.
    TEST_ASSERT_FALSE(playlist_record_read(reinterpret_cast<const uint8_t*>("60;1000;"), 8, text, sizeof(text), &remembered, resumes));
}

// A revision given with a list is a whole number; none means the list is to be replaced whatever it is.
static void test_a_playlist_revision_is_read_from_its_body() {
    bool given = true;
    uint32_t revision = 0;
    const char* error = nullptr;
    const JsonPtrForTest none(cJSON_Parse(R"({"effects":[]})"));
    TEST_ASSERT_TRUE(playlist_revision_from_json(none.get(), given, revision, error));
    TEST_ASSERT_FALSE(given);
    const JsonPtrForTest seven(cJSON_Parse(R"({"effects":[],"revision":7})"));
    TEST_ASSERT_TRUE(playlist_revision_from_json(seven.get(), given, revision, error));
    TEST_ASSERT_TRUE(given);
    TEST_ASSERT_EQUAL_UINT32(7, revision);
    for (const char* bad : {R"({"revision":-1})", R"({"revision":1.5})", R"({"revision":"7"})"}) {
        const JsonPtrForTest wrong(cJSON_Parse(bad));
        TEST_ASSERT_FALSE_MESSAGE(playlist_revision_from_json(wrong.get(), given, revision, error), bad);
    }
}
// A start from the schedule and a list emptied at the same time: whichever comes second sees the other
// whole, so the playlist never plays an empty list, and an off with an empty start leaves the garland off.
static void test_an_emptied_list_and_a_start_never_cross() {
    State state;
    state.set_effect_count(12);
    const uint16_t list[] = {5, 7};
    for (int round = 0; round < 200; ++round) {
        state.set_playlist(list, 2, 30);
        state.take_owner_choice();
        std::thread emptying([&state, &list] { state.set_playlist(list, 0, 30); });
        state.apply_scheduled(0, 0, -1, kPowerOn, true, -1, kPowerOff);
        emptying.join();
        const Snapshot now = state.snapshot();
        TEST_ASSERT_FALSE(now.playlist);
        if (now.power_requested) {
            // Only the start, before the list was emptied, turns it on.
            TEST_ASSERT_EQUAL_UINT16(5, now.effect);
        }
    }
}
// The track a step takes is decided on the one reading of the list it starts from: an off and a start of
// the playlist in one minute leave a garland with an empty list off, and one with a list on.
static void test_a_step_takes_its_track_by_the_list_it_reads() {
    State state;
    state.set_effect_count(12);
    state.set_power(true);
    state.take_owner_choice();
    TEST_ASSERT_TRUE(state.apply_scheduled(0, 0, -1, kPowerOn, true, -1, kPowerOff));
    TEST_ASSERT_FALSE(state.snapshot().power_requested);
    TEST_ASSERT_FALSE(state.snapshot().playlist);
    const uint16_t list[] = {5, 7};
    state.set_playlist(list, 2, 30);
    TEST_ASSERT_TRUE(state.apply_scheduled(0, 0, -1, kPowerOn, true, -1, kPowerOff));
    TEST_ASSERT_TRUE(state.snapshot().power_requested && state.snapshot().playlist);
}

// A step the lighting task worked out before a start, though the start is at the same effect, is stale:
// the first effect of the new start plays whole.
static void test_a_step_from_before_a_start_is_stale() {
    State state;
    state.set_effect_count(12);
    const uint16_t list[] = {5, 7};
    state.set_playlist(list, 2, 30);
    TEST_ASSERT_TRUE(state.play_playlist(true));
    const Snapshot seen = state.snapshot();
    TEST_ASSERT_TRUE(state.play_playlist(true));
    TEST_ASSERT_EQUAL_UINT16(5, state.snapshot().effect);
    TEST_ASSERT_FALSE(state.advance_playlist(seen.effect, 7, seen.playlist_mark));
    TEST_ASSERT_EQUAL_UINT16(5, state.snapshot().effect);
    // Four starts come round to the same mark, more than come between two looks of the task.
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_TRUE(state.play_playlist(true));
    }
    TEST_ASSERT_EQUAL_UINT8(seen.playlist_mark, state.snapshot().playlist_mark);
}

// A scheduled step can start the playlist, kept in the effect's place of a 1.0.0 entry.
static void test_the_schedule_starts_the_playlist() {
    ScheduleEntry entries[kMaxScheduleEntries];
    size_t count = 0;
    const char* error = nullptr;
    const JsonPtrForTest json(cJSON_Parse(R"([{"time":"18:00","days":127,"power":"rise","playlist":true}])"));
    TEST_ASSERT_TRUE(schedule_from_json(json.get(), entries, count, error));
    TEST_ASSERT_EQUAL_STRING(kPlaylistStep, entries[0].effect);
    const JsonPtrForTest back(schedule_to_json(entries, count));
    const cJSON* item = cJSON_GetArrayItem(back.get(), 0);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "playlist")));
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(item, "effect"));
    for (const char* bad : {R"([{"time":"18:00","playlist":true,"effect":"aurora"}])", R"([{"time":"18:00","playlist":false}])",
                            R"([{"time":"18:00","power":"off","playlist":true}])"}) {
        const JsonPtrForTest wrong(cJSON_Parse(bad));
        TEST_ASSERT_FALSE_MESSAGE(schedule_from_json(wrong.get(), entries, count, error), bad);
    }

    // Of a playlist and an effect in one minute, the later in the list wins.
    ResolvedAction action;
    merge_entry(action, make_entry(18, 0, PowerStep::None, 0, 0, kPlaylistStep));
    TEST_ASSERT_TRUE(action.playlist && action.power == PowerStep::On && action.effect == -1);
    TEST_ASSERT_TRUE(action.power_without_playlist == PowerStep::None);
    merge_entry(action, make_entry(18, 0, PowerStep::None, 0, 0, "aurora"));
    TEST_ASSERT_TRUE(!action.playlist && action.effect == catalog_find("aurora"));

    State state;
    state.set_effect_count(12);
    TEST_ASSERT_TRUE(state.apply_scheduled(0, 0, -1, kPowerOn, true));
    TEST_ASSERT_FALSE(state.snapshot().playlist);
    const uint16_t list[] = {5, 7};
    state.set_playlist(list, 2, 30);
    TEST_ASSERT_TRUE(state.apply_scheduled(0, 0, -1, kPowerRise, true));
    TEST_ASSERT_TRUE(state.snapshot().playlist && state.snapshot().rise);
    TEST_ASSERT_EQUAL_UINT16(5, state.snapshot().effect);
}

// The screen says where the playlist stands, in the smaller font, and the longest fits its buffer.
static void test_the_screen_says_where_the_playlist_stands() {
    State state;
    state.set_effect_count(kMaxPlaylistEffects);
    uint16_t list[kMaxPlaylistEffects];
    for (uint16_t i = 0; i < kMaxPlaylistEffects; ++i) {
        list[i] = i;
    }
    state.set_playlist(list, kMaxPlaylistEffects, 30);
    state.set_probe(ProbeStatus::Present, 32);
    const ScreenInputs inputs = {"Aurora", "csiGarland-Setup", "PASS", "192.168.4.1"};
    state.set_power(true);
    TEST_ASSERT_EQUAL_STRING("Playing", build_screen_model(state.snapshot(), inputs).title);
    TEST_ASSERT_TRUE(state.play_playlist(true));
    ScreenModel model = build_screen_model(state.snapshot(), inputs);
    TEST_ASSERT_EQUAL_STRING("Playing 1/16", model.title);
    TEST_ASSERT_TRUE(model.title_small);
    TEST_ASSERT_TRUE(state.advance_playlist(0, kMaxPlaylistEffects - 1, state.snapshot().playlist_mark));
    TEST_ASSERT_EQUAL_STRING("Playing 16/16", build_screen_model(state.snapshot(), inputs).title);
}

// A push update is one Server-Sent Event, whole, or nothing: a line break in its data would cut it short.
static void test_an_event_is_framed_whole() {
    char frame[kEventFrameBytes];
    const size_t length = event_frame(frame, sizeof(frame), "state", R"({"power":true})");
    TEST_ASSERT_EQUAL_STRING("event: state\ndata: {\"power\":true}\n\n", frame);
    TEST_ASSERT_EQUAL_UINT32(std::strlen(frame), length);
    TEST_ASSERT_EQUAL_UINT32(0, event_frame(frame, sizeof(frame), "state", "{\n}"));
    char small[16];
    TEST_ASSERT_EQUAL_UINT32(0, event_frame(small, sizeof(small), "state", R"({"power":true})"));
    // The longest state the portal sends fits one frame.
    TEST_ASSERT_GREATER_THAN_UINT32(400 + 24, kEventFrameBytes);
}
// In one minute the steps go in their order, for a garland with a playlist and for one with none.
static void test_steps_meet_in_their_order() {
    ResolvedAction off_then_play;
    merge_entry(off_then_play, make_entry(18, 0, PowerStep::Off));
    merge_entry(off_then_play, make_entry(18, 0, PowerStep::None, 0, 0, kPlaylistStep));
    TEST_ASSERT_TRUE(off_then_play.with_playlist(true).power == PowerStep::On);
    TEST_ASSERT_TRUE(off_then_play.with_playlist(false).power == PowerStep::Off);

    ResolvedAction play_then_off;
    merge_entry(play_then_off, make_entry(18, 0, PowerStep::None, 0, 0, kPlaylistStep));
    merge_entry(play_then_off, make_entry(18, 0, PowerStep::Off));
    TEST_ASSERT_TRUE(play_then_off.with_playlist(true).power == PowerStep::Off);
    TEST_ASSERT_TRUE(play_then_off.with_playlist(false).power == PowerStep::Off);

    // An effect, then a playlist with none to start: the effect stands, and turns the garland on.
    ResolvedAction effect_then_play;
    merge_entry(effect_then_play, make_entry(18, 0, PowerStep::None, 0, 0, "aurora"));
    merge_entry(effect_then_play, make_entry(18, 0, PowerStep::None, 0, 0, kPlaylistStep));
    const ResolvedAction none = effect_then_play.with_playlist(false);
    TEST_ASSERT_TRUE(none.power == PowerStep::On && none.effect == catalog_find("aurora") && !none.playlist);
    const ResolvedAction ready = effect_then_play.with_playlist(true);
    TEST_ASSERT_TRUE(ready.playlist && ready.effect == -1);
}

// A rise goes with the whole entry it was saved with: moved by 1.0.0 it still rises, changed it does not.
static void test_a_rise_goes_with_its_entry() {
    const ScheduleEntry morning = make_entry(7, 0, PowerStep::On, 30);
    const ScheduleEntry evening = make_entry(18, 0, PowerStep::On);
    const ScheduleEntry rises[] = {morning};
    TEST_ASSERT_TRUE(schedule_entry_rises(morning, rises, 1));
    TEST_ASSERT_FALSE(schedule_entry_rises(evening, rises, 1));
    ScheduleEntry changed = morning;
    changed.brightness_percent = 40;
    TEST_ASSERT_FALSE(schedule_entry_rises(changed, rises, 1));
    ScheduleEntry off = morning;
    off.power = PowerStep::Off;
    TEST_ASSERT_FALSE(schedule_entry_rises(off, rises, 1));
}

// A playlist started or stopped is saved at once, as turning on or off is.
static void test_a_playlist_started_is_saved_at_once() {
    MemorySaveTimer timer(Remembered{1, 50, 4, true, false});
    TEST_ASSERT_TRUE(timer.should_save(Remembered{1, 50, 4, true, true}, 1000));
    timer.saved(Remembered{1, 50, 4, true, true});
    TEST_ASSERT_TRUE(timer.should_save(Remembered{1, 50, 4, true, false}, 2000));
}

static void run_light_tests() {
    RUN_TEST(test_a_rise_goes_with_its_entry);
    RUN_TEST(test_a_playlist_started_is_saved_at_once);
    RUN_TEST(test_steps_meet_in_their_order);
    RUN_TEST(test_an_event_is_framed_whole);
    RUN_TEST(test_the_schedule_starts_the_playlist);
    RUN_TEST(test_the_screen_says_where_the_playlist_stands);
    RUN_TEST(test_a_fade_rises_and_falls_at_its_pace);
    RUN_TEST(test_a_cross_fade_mixes_the_old_frame_into_the_new);
    RUN_TEST(test_the_playlist_moves_on_and_yields_to_a_choice);
    RUN_TEST(test_only_a_scheduled_rise_rises);
    RUN_TEST(test_the_schedule_takes_a_rise);
    RUN_TEST(test_the_timeline_shows_the_next_day_and_where_actions_meet);
    RUN_TEST(test_the_playlist_reads_and_writes_its_text_and_json);
    RUN_TEST(test_a_saved_schedule_reads_the_same_in_1_0_0);
    RUN_TEST(test_an_empty_playlist_starts_nothing);
    RUN_TEST(test_a_step_takes_its_track_by_the_list_it_reads);
    RUN_TEST(test_an_emptied_list_and_a_start_never_cross);
    RUN_TEST(test_a_step_from_before_a_start_is_stale);
    RUN_TEST(test_rises_keep_to_their_own_entries);
    RUN_TEST(test_a_list_refused_keeps_the_rises_before_it);
    RUN_TEST(test_a_start_keeps_only_the_mark_of_the_list_in_force);
    RUN_TEST(test_a_schedule_keeps_an_effect_its_catalog_lacks);
    RUN_TEST(test_an_empty_list_names_no_effects);
    RUN_TEST(test_the_next_step_passes_over_an_empty_start);
    RUN_TEST(test_a_new_chain_length_is_rendered_before_it_is_sent);
    RUN_TEST(test_a_rise_stays_in_its_minute);
    RUN_TEST(test_a_playlist_resumes_only_with_its_own_record);
    RUN_TEST(test_a_playlist_revision_is_read_from_its_body);
}
