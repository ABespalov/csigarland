/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Host tests for what the owner chooses against what the schedule wants
 * Contributors: None
 *
 * Included by test_main.cpp.
 */

#pragma once

static void test_a_choice_outranks_schedule_steps_from_before_it() {
    constexpr int64_t kNow = 29000000;
    // Turned off by hand at 20:00, and the power blinked half an hour later: the 18:00 step is older.
    TEST_ASSERT_EQUAL_INT64(kNow - 29, replay_first_minute((kNow - 30) * 60, kNow));
    // In the same minute as a step, the choice wins, and one just made leaves nothing to do.
    TEST_ASSERT_EQUAL_INT64(-1, replay_first_minute(kNow * 60 + 30, kNow));
    // Away for days: never further back than a day.
    TEST_ASSERT_EQUAL_INT64(kNow - kReplayMinutes + 1, replay_first_minute((kNow - 5000) * 60, kNow));
    // Chosen with no clock, which may be later than any step.
    TEST_ASSERT_EQUAL_INT64(-1, replay_first_minute(0, kNow));
    // A clock that has gone back since.
    TEST_ASSERT_EQUAL_INT64(-1, replay_first_minute((kNow + 100) * 60, kNow));
    // Nobody has chosen anything yet: the schedule decides.
    TEST_ASSERT_EQUAL_INT64(kNow - kReplayMinutes + 1, replay_first_minute(kNoChoiceYet, kNow));
}

static void test_a_choice_with_no_clock_takes_the_first_time_known() {
    constexpr int64_t kFirstSync = 29000000;
    // Chosen with no clock: the step at first sync may be older than it, so it stands.
    TEST_ASSERT_EQUAL_INT64(-1, replay_first_minute(0, kFirstSync));
    TEST_ASSERT_TRUE(choice_needs_a_time(0));
    // Saved as of that time, and the power goes. A day later a step came due with no power.
    const int64_t saved = kFirstSync * 60;
    TEST_ASSERT_FALSE(choice_needs_a_time(saved));
    TEST_ASSERT_EQUAL_INT64(kFirstSync + 1, replay_first_minute(saved, kFirstSync + 780));
    TEST_ASSERT_FALSE(choice_needs_a_time(kNoChoiceYet));
}

static void test_a_change_not_yet_saved_is_told_apart() {
    const Remembered saved{1, 50, 4, true};
    Remembered changed = saved;
    changed.brightness_percent = 60;
    MemorySaveTimer timer(saved);
    TEST_ASSERT_TRUE(timer.holds(saved));
    TEST_ASSERT_FALSE(timer.holds(changed));
    timer.saved(changed);
    TEST_ASSERT_TRUE(timer.holds(changed));
}

// The first time known comes while it waits: holds() false means no step is carried out for it.
static void test_choosing_the_same_again_counts_as_a_choice() {
    const Remembered saved{1, 50, 4, true};
    MemorySaveTimer timer(saved);
    TEST_ASSERT_TRUE(timer.holds(saved));
    timer.note_choice();
    TEST_ASSERT_FALSE(timer.holds(saved));
    TEST_ASSERT_FALSE(timer.should_save(saved, 1000));
    TEST_ASSERT_FALSE(timer.should_save(saved, 1000 + kMemorySaveDelayMs - 1));
    TEST_ASSERT_TRUE(timer.should_save(saved, 1000 + kMemorySaveDelayMs));
    timer.saved(saved);
    TEST_ASSERT_TRUE(timer.holds(saved));
    TEST_ASSERT_FALSE(timer.should_save(saved, 100000));
}

static void test_going_away_and_back_before_a_save_counts_as_a_choice() {
    const Remembered saved{1, 50, 4, true};
    Remembered other = saved;
    other.effect = 2;
    MemorySaveTimer timer(saved);
    timer.note_choice();
    TEST_ASSERT_FALSE(timer.should_save(other, 1000));
    timer.note_choice();
    TEST_ASSERT_FALSE(timer.should_save(saved, 5000));
    TEST_ASSERT_FALSE(timer.holds(saved));
    TEST_ASSERT_FALSE(timer.should_save(saved, 5000 + kMemorySaveDelayMs - 1));
    TEST_ASSERT_TRUE(timer.should_save(saved, 5000 + kMemorySaveDelayMs));
    // A failed write waits as any other.
    timer.retry_later(30000);
    TEST_ASSERT_FALSE(timer.should_save(saved, 30001));
    TEST_ASSERT_TRUE(timer.should_save(saved, 30000 + kMemorySaveDelayMs));
}

static void test_only_the_owner_makes_a_choice() {
    State state(32);
    state.set_effect_count(3);
    // What a start restores, and what the schedule does.
    state.set_effect(1);
    state.set_brightness(40);
    state.set_speed(8);
    state.set_power(true);
    state.select_first_favorite();
    state.apply_action(50, 0, -1, 1);
    TEST_ASSERT_FALSE(state.take_owner_choice());
    // The same effect again is a choice as well.
    state.activate_effect(0);
    TEST_ASSERT_TRUE(state.take_owner_choice());
    TEST_ASSERT_FALSE(state.take_owner_choice());
    state.activate_effect(0);
    TEST_ASSERT_TRUE(state.take_owner_choice());
    state.click_power();
    TEST_ASSERT_TRUE(state.take_owner_choice());
    state.click_speed();
    TEST_ASSERT_TRUE(state.take_owner_choice());
    // A press that only wakes the screen chooses nothing, and neither does its click.
    state.set_saver_active(true);
    state.button_down(ButtonId::Speed);
    state.click_speed();
    TEST_ASSERT_FALSE(state.take_owner_choice());
    // Asking for the countdown, its running out, and cancelling it are all the owner's doing.
    state.button_down(ButtonId::Power);
    state.long_press_power(1000);
    TEST_ASSERT_TRUE(state.take_owner_choice());
    state.tick_off_countdown(1000 + kOffCountdownMs);
    TEST_ASSERT_TRUE(state.take_owner_choice());
    state.click_power();
    TEST_ASSERT_TRUE(state.take_owner_choice());
    state.button_down(ButtonId::Power);
    state.long_press_power(2000);
    TEST_ASSERT_TRUE(state.take_owner_choice());
    state.button_down(ButtonId::Effect);
    TEST_ASSERT_TRUE(state.take_owner_choice());
}

// Worked out, then a press comes before it is applied: the press stays, whole.
static void test_the_schedule_yields_to_a_choice_since_it_looked() {
    State state(32);
    state.set_effect_count(3);
    state.set_power(true);
    state.set_speed(4);
    TEST_ASSERT_FALSE(state.take_owner_choice());
    state.long_press_speed();
    TEST_ASSERT_FALSE(state.apply_scheduled(30, 12, 2, 0));
    Snapshot now = state.snapshot();
    TEST_ASSERT_EQUAL_UINT8(kNormalSpeed, now.speed_x4);
    TEST_ASSERT_TRUE(now.power_chosen);
    TEST_ASSERT_NOT_EQUAL(30, now.brightness_percent);

    // With nothing since the next look, it applies, all of it, and is no choice itself.
    TEST_ASSERT_TRUE(state.take_owner_choice());
    TEST_ASSERT_TRUE(state.apply_scheduled(30, 12, 2, 0));
    now = state.snapshot();
    TEST_ASSERT_EQUAL_UINT8(30, now.brightness_percent);
    TEST_ASSERT_EQUAL_UINT8(12, now.speed_x4);
    TEST_ASSERT_EQUAL_UINT8(2, now.effect);
    TEST_ASSERT_FALSE(now.power_chosen);
    TEST_ASSERT_FALSE(state.take_owner_choice());
}

// However many, ending on the values they started from: nothing tells them apart but the mark.
static void test_no_number_of_choices_lets_an_old_step_through() {
    for (const int choices : {1, 63, 64, 65, 128, 256, 1000}) {
        State state(32);
        state.set_effect_count(3);
        state.set_power(true);
        state.set_speed(kNormalSpeed);
        TEST_ASSERT_FALSE(state.take_owner_choice());
        for (int i = 0; i < choices; ++i) {
            state.long_press_speed();
        }
        TEST_ASSERT_FALSE(state.apply_scheduled(0, 12, -1, -1));
        TEST_ASSERT_EQUAL_UINT8(kNormalSpeed, state.snapshot().speed_x4);
        // And the next look sees them.
        TEST_ASSERT_TRUE(state.take_owner_choice());
    }
}

// The countdown shares the word with what a step changes: it ends the one it finds, and no other.
static void test_a_step_ends_only_the_countdown_it_found() {
    State state(32);
    state.set_effect_count(3);
    state.set_power(true);
    state.button_down(ButtonId::Power);

    // Begun before the step: a step that turns on or off ends it.
    state.long_press_power(1000);
    TEST_ASSERT_EQUAL_UINT8(kOffCountdownSeconds, state.snapshot().off_seconds);
    TEST_ASSERT_TRUE(state.take_owner_choice());
    TEST_ASSERT_TRUE(state.apply_scheduled(0, 0, -1, 1));
    TEST_ASSERT_EQUAL_UINT8(0, state.snapshot().off_seconds);
    state.tick_off_countdown(1000 + kOffCountdownMs);
    TEST_ASSERT_TRUE(state.snapshot().power_chosen);

    // A step that names no power leaves it.
    state.long_press_power(2000);
    TEST_ASSERT_TRUE(state.take_owner_choice());
    TEST_ASSERT_TRUE(state.apply_scheduled(40, 0, -1, -1));
    TEST_ASSERT_EQUAL_UINT8(kOffCountdownSeconds, state.snapshot().off_seconds);
    state.button_down(ButtonId::Power);
    TEST_ASSERT_TRUE(state.take_owner_choice());

    // Begun after the step was applied: it runs, and turns the garland off.
    TEST_ASSERT_TRUE(state.apply_scheduled(0, 0, -1, 1));
    state.button_down(ButtonId::Power);
    state.long_press_power(3000);
    TEST_ASSERT_EQUAL_UINT8(kOffCountdownSeconds, state.snapshot().off_seconds);
    state.tick_off_countdown(3000 + kOffCountdownMs);
    TEST_ASSERT_FALSE(state.snapshot().power_chosen);

    // Begun before a step that then gives way to it: untouched, and it still turns the garland off.
    state.set_power(true);
    TEST_ASSERT_TRUE(state.take_owner_choice());
    state.button_down(ButtonId::Power);
    state.long_press_power(4000);
    TEST_ASSERT_FALSE(state.apply_scheduled(0, 0, -1, 1));
    TEST_ASSERT_EQUAL_UINT8(kOffCountdownSeconds, state.snapshot().off_seconds);
    state.tick_off_countdown(4000 + kOffCountdownMs - 1500);
    TEST_ASSERT_EQUAL_UINT8(2, state.snapshot().off_seconds);
    state.tick_off_countdown(4000 + kOffCountdownMs);
    TEST_ASSERT_FALSE(state.snapshot().power_chosen);
    TEST_ASSERT_EQUAL_UINT8(0, state.snapshot().off_seconds);
}

static void test_a_failed_save_of_a_choices_time_is_tried_again() {
    constexpr int64_t kNoClock = 0;
    // The first try, at once.
    TEST_ASSERT_TRUE(choice_time_is_due(kNoClock, true, false, 0));
    // Failed: not on every look round, and then again.
    TEST_ASSERT_FALSE(choice_time_is_due(kNoClock, true, true, kChoiceTimeRetryMs - 1));
    TEST_ASSERT_TRUE(choice_time_is_due(kNoClock, true, true, kChoiceTimeRetryMs));
    // Something newer waits to be saved, with a time of its own.
    TEST_ASSERT_FALSE(choice_time_is_due(kNoClock, false, true, kChoiceTimeRetryMs));
    // Done, or nothing to give a time to.
    TEST_ASSERT_FALSE(choice_time_is_due(1800000000, true, true, kChoiceTimeRetryMs));
    TEST_ASSERT_FALSE(choice_time_is_due(kNoChoiceYet, true, false, 0));
}

static uint32_t s_uptime_reads[8];
static size_t s_uptime_read = 0;
static int64_t s_wall_reads[4];
static size_t s_wall_read = 0;

static uint32_t next_uptime_read() {
    return s_uptime_reads[s_uptime_read++];
}

static int64_t next_wall_read() {
    return s_wall_reads[s_wall_read++];
}

static int64_t read_choice_time(uint32_t choice, std::initializer_list<int64_t> reads) {
    s_uptime_read = 0;
    s_wall_read = 0;
    size_t index = 0;
    for (const int64_t read : reads) {
        if (index % 3 == 1) {
            s_wall_reads[s_wall_read++] = read;
        } else {
            s_uptime_reads[s_uptime_read++] = static_cast<uint32_t>(read);
        }
        ++index;
    }
    s_uptime_read = 0;
    s_wall_read = 0;
    return latest_wall_ms_of_uptime(choice, next_uptime_read, next_wall_read);
}

static void test_a_pause_between_clock_reads_does_not_move_a_choice() {
    constexpr uint32_t kChoice = 1000;
    constexpr int64_t kMinute = 60000 * 29000000LL;
    constexpr int64_t kNewMinute = kMinute / 60000;

    // Chosen 10 ms before the minute. A read 5 ms later, then a pause of 10 ms before the time of
    // day: taken as it stands, this would date the choice in the new minute. Tried again, none.
    int64_t found = read_choice_time(kChoice, {kChoice + 5, kMinute + 5, kChoice + 15, kChoice + 16, kMinute + 6, kChoice + 16});
    TEST_ASSERT_EQUAL_size_t(4, s_uptime_read);
    TEST_ASSERT_TRUE(found / 60000 < kNewMinute);

    // Chosen 10 ms before it, with a pair a whole millisecond wide: still the old minute.
    found = read_choice_time(kChoice, {kChoice + 5, kMinute - 5, kChoice + 6});
    TEST_ASSERT_EQUAL_size_t(2, s_uptime_read);
    TEST_ASSERT_TRUE(found / 60000 < kNewMinute);

    // Chosen just as the minute began, with the same pair: the new one, so its step does not run.
    found = read_choice_time(kChoice, {kChoice, kMinute, kChoice + 1});
    TEST_ASSERT_EQUAL_INT64(kNewMinute, found / 60000);
    found = read_choice_time(kChoice, {kChoice + 5, kMinute + 5, kChoice + 6});
    TEST_ASSERT_EQUAL_INT64(kNewMinute, found / 60000);

    // Too close to tell: the choice wins.
    found = read_choice_time(kChoice, {kChoice, kMinute - 1, kChoice + 1});
    TEST_ASSERT_EQUAL_INT64(kNewMinute, found / 60000);

    // Pauses every time: the narrowest stands, and a choice just past the minute stays in it.
    found = read_choice_time(kChoice, {kChoice, kMinute, kChoice + 40, kChoice + 50, kMinute + 50, kChoice + 60,
                                       kChoice + 70, kMinute + 70, kChoice + 100, kChoice + 110, kMinute + 110, kChoice + 150});
    TEST_ASSERT_EQUAL_size_t(kClockReadTries * 2, s_uptime_read);
    TEST_ASSERT_EQUAL_INT64(kMinute + kClockReadRoundingMs, found);

    // The uptime counter wrapping in between changes nothing.
    found = read_choice_time(UINT32_MAX - 5, {4, kMinute, 4});
    TEST_ASSERT_EQUAL_INT64(kMinute - 10 + kClockReadRoundingMs, found);
}

static void test_a_step_due_before_a_choice_does_not_override_it() {
    constexpr int64_t kNow = 29000000;
    constexpr int64_t kLast = kNow - 1;
    // No choice near: the minute that has come.
    TEST_ASSERT_EQUAL_INT64(kNow, catch_up_first_minute(kLast, kNow, -1));
    TEST_ASSERT_EQUAL_INT64(kNow, catch_up_first_minute(kLast, kNow, kNow - 100));
    // Chosen just before the minute turned: the step due at its start still runs.
    TEST_ASSERT_EQUAL_INT64(kNow, catch_up_first_minute(kLast, kNow, kLast));
    // Chosen after it fell due, before the look round came to it: nothing to do.
    TEST_ASSERT_TRUE(catch_up_first_minute(kLast, kNow, kNow) > kNow);
    // And the next minute's step comes as usual.
    TEST_ASSERT_EQUAL_INT64(kNow + 1, catch_up_first_minute(kNow, kNow + 1, kNow));
    // Several minutes at once: only what came after the choice.
    TEST_ASSERT_EQUAL_INT64(kNow - 1, catch_up_first_minute(kNow - 4, kNow, kNow - 2));
    // A big step of the clock still goes back no further than the limit.
    TEST_ASSERT_EQUAL_INT64(kNow - kMaxCatchUpMinutes + 1, catch_up_first_minute(kNow - 100, kNow, -1));
    // Dated by a clock since corrected: it holds nothing back.
    TEST_ASSERT_EQUAL_INT64(kNow, catch_up_first_minute(kLast, kNow, kNow + 500));
}

static uint32_t s_fake_choice_ms = 0;

static void test_a_choice_keeps_its_own_moment() {
    State state(32);
    state.set_effect_count(3);
    state.activate_effect(1);
    TEST_ASSERT_EQUAL_UINT32(0, state.owner_choice_ms());
    state.set_choice_clock([] { return s_fake_choice_ms; });
    s_fake_choice_ms = 1234;
    state.activate_effect(1);
    TEST_ASSERT_EQUAL_UINT32(1234, state.owner_choice_ms());
    // The schedule leaves it.
    s_fake_choice_ms = 5678;
    state.apply_action(50, 0, -1, 1);
    TEST_ASSERT_EQUAL_UINT32(1234, state.owner_choice_ms());
}

static void run_choice_tests() {
    RUN_TEST(test_a_choice_outranks_schedule_steps_from_before_it);
    RUN_TEST(test_a_choice_with_no_clock_takes_the_first_time_known);
    RUN_TEST(test_a_change_not_yet_saved_is_told_apart);
    RUN_TEST(test_choosing_the_same_again_counts_as_a_choice);
    RUN_TEST(test_going_away_and_back_before_a_save_counts_as_a_choice);
    RUN_TEST(test_only_the_owner_makes_a_choice);
    RUN_TEST(test_the_schedule_yields_to_a_choice_since_it_looked);
    RUN_TEST(test_no_number_of_choices_lets_an_old_step_through);
    RUN_TEST(test_a_step_ends_only_the_countdown_it_found);
    RUN_TEST(test_a_failed_save_of_a_choices_time_is_tried_again);
    RUN_TEST(test_a_pause_between_clock_reads_does_not_move_a_choice);
    RUN_TEST(test_a_step_due_before_a_choice_does_not_override_it);
    RUN_TEST(test_a_choice_keeps_its_own_moment);
}
