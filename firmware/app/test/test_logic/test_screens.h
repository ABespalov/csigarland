/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Host tests for the screen catalog: which screen is up and what it shows
 * Contributors: None
 */

#pragma once

static Snapshot standby_snapshot() {
    Snapshot s{};
    s.update_percent = kNoUpdate;
    s.led_setting = kLedCountAuto;
    s.probe = ProbeStatus::Present;
    s.led_estimate = 32;
    s.brightness_percent = 50;
    s.speed_x4 = 4;
    s.led_kept = 0;
    return s;
}

static const ScreenInputs kTestInputs = {"Twinkle", "csiGarland-setup", "secret", "192.168.4.1"};

static void test_each_state_has_its_screen() {
    Snapshot s = standby_snapshot();
    TEST_ASSERT_TRUE(choose_screen(s) == ScreenId::Standby);
    s.power_requested = true;
    TEST_ASSERT_TRUE(choose_screen(s) == ScreenId::Playing);

    // Each of these outranks everything below it.
    s.probe = ProbeStatus::Absent;
    TEST_ASSERT_TRUE(choose_screen(s) == ScreenId::NoLeds);
    s.setup_network = true;
    TEST_ASSERT_TRUE(choose_screen(s) == ScreenId::SetupNetwork);
    s.off_seconds = 3;
    TEST_ASSERT_TRUE(choose_screen(s) == ScreenId::PowerOff);
    s.update_percent = 40;
    TEST_ASSERT_TRUE(choose_screen(s) == ScreenId::UpdateReceiving);
    s.update_percent = kUpdateApplying;
    TEST_ASSERT_TRUE(choose_screen(s) == ScreenId::UpdateApplying);
    s.update_percent = kUpdateRestarting;
    TEST_ASSERT_TRUE(choose_screen(s) == ScreenId::UpdateRestarting);

    // A count the owner set does not depend on the probe.
    s = standby_snapshot();
    s.probe = ProbeStatus::Absent;
    s.led_setting = 32;
    TEST_ASSERT_TRUE(choose_screen(s) == ScreenId::Standby);
}

static void test_screen_colours_follow_the_policy() {
    TEST_ASSERT_TRUE(screen_row(ScreenId::Standby).tone == ScreenTone::Idle);
    TEST_ASSERT_TRUE(screen_row(ScreenId::Playing).tone == ScreenTone::Normal);
    TEST_ASSERT_TRUE(screen_row(ScreenId::NoLeds).tone == ScreenTone::Attention);
    TEST_ASSERT_TRUE(screen_row(ScreenId::SetupNetwork).tone == ScreenTone::Attention);
    TEST_ASSERT_TRUE(screen_row(ScreenId::PowerOff).tone == ScreenTone::Attention);
    TEST_ASSERT_TRUE(screen_row(ScreenId::UpdateReceiving).tone == ScreenTone::Attention);
    TEST_ASSERT_TRUE(screen_row(ScreenId::UpdateApplying).tone == ScreenTone::Attention);
    TEST_ASSERT_TRUE(screen_row(ScreenId::UpdateRestarting).tone == ScreenTone::Success);
}

static void test_screens_that_keep_the_saver_away() {
    for (const ScreenRow& row : kScreens) {
        const bool must_stay = row.id == ScreenId::MakingKey || row.id == ScreenId::Access || row.id == ScreenId::PowerOff || row.id == ScreenId::UpdateReceiving ||
                               row.id == ScreenId::UpdateApplying || row.id == ScreenId::UpdateRestarting;
        TEST_ASSERT_EQUAL(must_stay, row.keeps_awake);
        TEST_ASSERT_EQUAL(!must_stay, row.shows_stats);
    }
}

// The clock's glyph says an action is near.
static void test_the_clock_glyph_follows_the_state() {
    State state(32);
    state.set_effect_count(3);
    state.set_probe(ProbeStatus::Present, 32);
    const ScreenInputs inputs = {"Effect", "Ssid", "Pass", "192.168.4.1"};
    TEST_ASSERT_FALSE(build_screen_model(state.snapshot(), inputs).clock_glyph_lit);
    state.set_action_soon(true);
    TEST_ASSERT_TRUE(build_screen_model(state.snapshot(), inputs).clock_glyph_lit);
    state.set_action_soon(false);
    TEST_ASSERT_FALSE(build_screen_model(state.snapshot(), inputs).clock_glyph_lit);
}

static void test_an_action_is_soon_within_a_day() {
    constexpr int64_t kNow = 29000000;
    TEST_ASSERT_TRUE(action_is_soon(kNow + 1, kNow));
    TEST_ASSERT_TRUE(action_is_soon(kNow + kActionSoonMinutes, kNow));
    TEST_ASSERT_FALSE(action_is_soon(kNow + kActionSoonMinutes + 1, kNow));
    TEST_ASSERT_FALSE(action_is_soon(-1, kNow));
    TEST_ASSERT_FALSE(action_is_soon(kNow, kNow));
}

static void test_the_next_action_is_found_again_only_when_it_must_be() {
    constexpr int64_t kNow = 29000000;
    // Nothing found yet, and a clock gone back.
    TEST_ASSERT_FALSE(next_action_still_holds(-1, -1, kNow));
    TEST_ASSERT_FALSE(next_action_still_holds(kNow + 5, kNow + 10, kNow));
    // An action still to come, however far.
    TEST_ASSERT_TRUE(next_action_still_holds(kNow - 500, kNow + 1, kNow));
    // That has come: it must be found anew, or the next would never be seen.
    TEST_ASSERT_FALSE(next_action_still_holds(kNow - 500, kNow, kNow));
    // None within the week stands for a while, as finding that is the longest walk.
    TEST_ASSERT_TRUE(next_action_still_holds(kNow - kNoActionStandsMinutes + 1, -1, kNow));
    TEST_ASSERT_FALSE(next_action_still_holds(kNow - kNoActionStandsMinutes, -1, kNow));
}

static void test_screen_model_holds_what_is_drawn() {
    Snapshot s = standby_snapshot();
    s.station_ip = 192u | 168u << 8 | 101u << 16 | 88u << 24;
    ScreenModel model = build_screen_model(s, kTestInputs);
    TEST_ASSERT_EQUAL_STRING("Standby", model.title);
    TEST_ASSERT_EQUAL_STRING("192.168.101.88", model.footer);
    TEST_ASSERT_EQUAL_STRING("50%", model.stats[0]);
    TEST_ASSERT_EQUAL_STRING("1x", model.stats[1]);
    TEST_ASSERT_EQUAL_STRING("~32", model.stats[2]);
    TEST_ASSERT_FALSE(model.shows_clock_glyph);

    // A length the output refused: the number the LEDs are getting, marked.
    Snapshot kept = s;
    kept.led_count = 100;
    kept.led_kept = 32;
    TEST_ASSERT_EQUAL_STRING("!32", build_screen_model(kept, kTestInputs).stats[2]);

    s.clock_module = true;
    s.power_requested = true;
    s.station_ip = 0;
    model = build_screen_model(s, kTestInputs);
    TEST_ASSERT_EQUAL_STRING("Twinkle", model.text);
    TEST_ASSERT_EQUAL_STRING("192.168.4.1", model.footer);
    TEST_ASSERT_TRUE(model.shows_clock_glyph);

    s.setup_network = true;
    model = build_screen_model(s, kTestInputs);
    TEST_ASSERT_EQUAL_STRING("csiGarland-setup\nPass secret", model.text);

    s.off_seconds = 4;
    model = build_screen_model(s, kTestInputs);
    TEST_ASSERT_EQUAL_STRING("Off in 4 s\nPress a button\nto stay on", model.text);
    TEST_ASSERT_EQUAL_STRING("PRESS TO CANCEL", model.footer);

    s.update_percent = 37;
    model = build_screen_model(s, kTestInputs);
    TEST_ASSERT_EQUAL_UINT8(37, model.percent);
    TEST_ASSERT_TRUE(model.shows_progress);
    TEST_ASSERT_EQUAL_STRING("DO NOT TURN OFF", model.footer);

    s.update_percent = kUpdateRestarting;
    model = build_screen_model(s, kTestInputs);
    TEST_ASSERT_EQUAL_UINT8(100, model.percent);
    TEST_ASSERT_EQUAL_STRING("Update installed\nRestarting", model.text);
}

static void run_screen_tests() {
    RUN_TEST(test_each_state_has_its_screen);
    RUN_TEST(test_screen_colours_follow_the_policy);
    RUN_TEST(test_screens_that_keep_the_saver_away);
    RUN_TEST(test_the_clock_glyph_follows_the_state);
    RUN_TEST(test_an_action_is_soon_within_a_day);
    RUN_TEST(test_the_next_action_is_found_again_only_when_it_must_be);
    RUN_TEST(test_screen_model_holds_what_is_drawn);
}
