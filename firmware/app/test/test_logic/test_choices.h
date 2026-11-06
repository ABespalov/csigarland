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

static void test_a_save_is_dated_so_that_what_waits_still_comes() {
    constexpr int64_t kNow = 29000000LL * 60;
    constexpr int64_t kFirstKnown = kNow - 600;
    // What a step did, by the save.
    TEST_ASSERT_EQUAL_INT64(kNow, save_epoch(kNow, -1, kFirstKnown, kNow - 5, -1));
    // While a later step waits for the playlist, just before it, so that a start in between still does it.
    constexpr int64_t kWaits = kNow / 60 - 3;
    const int64_t saved = save_epoch(kNow, -1, kFirstKnown, kNow - 5, kWaits);
    TEST_ASSERT_EQUAL_INT64(kWaits * 60 - 1, saved);
    TEST_ASSERT_EQUAL_INT64(kWaits, replay_first_minute(saved, kNow / 60));
    // The owner's choice by its own moment, which waits for nothing.
    TEST_ASSERT_EQUAL_INT64(kNow - 2, save_epoch(kNow, kNow - 2, kFirstKnown, kNow - 5, kWaits));
    // Made with no clock, ten seconds before the first time known: as at that time, never before it.
    TEST_ASSERT_EQUAL_INT64(kFirstKnown, save_epoch(kNow, kFirstKnown - 10, kFirstKnown, 0, -1));
    // A step done since the choice is the newer.
    TEST_ASSERT_EQUAL_INT64(kNow, save_epoch(kNow, kNow - 20, kFirstKnown, kNow - 5, -1));
}

static void test_only_a_choice_of_what_plays_says_what_plays() {
    State state(32);
    state.set_effect_count(3);
    state.click_power();
    state.set_power(true);
    TEST_ASSERT_FALSE(state.owner_chose_play());
    state.click_speed();
    TEST_ASSERT_FALSE(state.owner_chose_play());
    state.activate_effect(1);
    TEST_ASSERT_TRUE(state.owner_chose_play());
}

static void test_a_change_of_only_the_levels_leaves_the_playlist_to_resume() {
    const uint16_t effects[] = {2, 1};
    State levels(32);
    levels.set_effect_count(3);
    levels.set_playlist(effects, 2, 60);
    levels.set_power(true);
    levels.click_speed();
    levels.click_power();
    TEST_ASSERT_FALSE(levels.owner_chose_play());
    TEST_ASSERT_TRUE(levels.resume_playlist_unless_chosen());
    TEST_ASSERT_TRUE(levels.snapshot().playlist);
    State chosen(32);
    chosen.set_effect_count(3);
    chosen.set_playlist(effects, 2, 60);
    chosen.activate_effect(1);
    TEST_ASSERT_FALSE(chosen.resume_playlist_unless_chosen());
    TEST_ASSERT_FALSE(chosen.snapshot().playlist);
}

static void test_a_resume_yields_to_a_choice_of_what_plays_made_with_the_mark_up_already() {
    const uint16_t effects[] = {2, 1};
    State state(32);
    state.set_effect_count(3);
    state.set_playlist(effects, 2, 60);
    state.set_power(true);
    // An earlier choice left the mark up, and choosing the first favourite again changes nothing else.
    state.click_speed();
    state.double_click_effect();
    TEST_ASSERT_TRUE(state.owner_chose_play());
    TEST_ASSERT_FALSE(state.resume_playlist_unless_chosen());
    TEST_ASSERT_FALSE(state.snapshot().playlist);
}

static void test_a_late_restore_comes_whole_and_yields_to_any_choice_since_the_start() {
    State restored(32);
    restored.set_effect_count(3);
    const uint8_t brightness = restored.snapshot().brightness_percent;
    // Zero leaves a part as it is, as a value this firmware does not accept does.
    TEST_ASSERT_TRUE(restored.restore_unless_chosen(0, 8, 2, kPowerOn));
    TEST_ASSERT_EQUAL_UINT16(2, restored.snapshot().effect);
    TEST_ASSERT_EQUAL_UINT8(8, restored.snapshot().speed_x4);
    TEST_ASSERT_EQUAL_UINT8(brightness, restored.snapshot().brightness_percent);
    TEST_ASSERT_TRUE(restored.snapshot().power_chosen);

    State chosen(32);
    chosen.set_effect_count(3);
    chosen.set_power(true);
    chosen.click_speed();
    const uint8_t speed = chosen.snapshot().speed_x4;
    const uint16_t effect = chosen.snapshot().effect;
    // The schedule's look round has taken the mark since: the choice still outranks what was left.
    TEST_ASSERT_TRUE(chosen.take_owner_choice());
    TEST_ASSERT_TRUE(chosen.owner_chose());
    TEST_ASSERT_FALSE(chosen.restore_unless_chosen(40, 12, 2, kPowerOff));
    TEST_ASSERT_EQUAL_UINT16(effect, chosen.snapshot().effect);
    TEST_ASSERT_EQUAL_UINT8(speed, chosen.snapshot().speed_x4);
    TEST_ASSERT_TRUE(chosen.snapshot().power_chosen);
}

static void test_a_late_restore_stops_a_playlist_a_step_started_meanwhile() {
    const uint16_t effects[] = {2, 1};
    State state(32);
    state.set_effect_count(3);
    state.set_playlist(effects, 2, 60);
    // The step turned the garland on with the playlist while what was left could not be read. Left off,
    // with an effect gone from the catalog since: the playlist stops all the same, for the replay to start.
    TEST_ASSERT_TRUE(state.apply_scheduled(0, 0, -1, kPowerOn, true));
    TEST_ASSERT_TRUE(state.snapshot().playlist);
    TEST_ASSERT_TRUE(state.restore_unless_chosen(0, 0, -1, kPowerOff));
    TEST_ASSERT_FALSE(state.snapshot().playlist);
    TEST_ASSERT_FALSE(state.snapshot().power_chosen);
}

static void test_a_late_restore_after_a_power_choice_still_resumes_the_playlist() {
    const uint16_t effects[] = {2, 1};
    State state(32);
    state.set_effect_count(3);
    state.set_playlist(effects, 2, 60);
    // POWER pressed while what was left could not be read: the record is not put on, as the owner chose since.
    state.click_power();
    TEST_ASSERT_TRUE(state.owner_chose());
    TEST_ASSERT_FALSE(state.restore_unless_chosen(40, 12, 0, kPowerOff));
    // A choice of the power alone says nothing of what plays: the playlist left playing comes back, on.
    TEST_ASSERT_TRUE(state.resume_playlist_unless_chosen());
    TEST_ASSERT_TRUE(state.snapshot().playlist);
    TEST_ASSERT_TRUE(state.snapshot().power_chosen);
    // One of an effect stands.
    State chosen(32);
    chosen.set_effect_count(3);
    chosen.set_playlist(effects, 2, 60);
    chosen.activate_effect(1);
    TEST_ASSERT_FALSE(chosen.restore_unless_chosen(40, 12, 0, kPowerOff));
    TEST_ASSERT_FALSE(chosen.resume_playlist_unless_chosen());
    TEST_ASSERT_EQUAL_UINT16(1, chosen.snapshot().effect);
}

static void test_a_save_before_a_replay_not_begun_leaves_its_steps_to_do() {
    // Chosen an hour before the start; the schedule cannot be read yet, and a playlist resumed meanwhile is
    // saved: dated just before the replay's first minute, it leaves that minute, and the Off in it, to do.
    constexpr int64_t kNow = 29000000;
    constexpr int64_t kChosen = (kNow - 60) * 60;
    const int64_t first = replay_first_minute(kChosen, kNow);
    TEST_ASSERT_EQUAL_INT64(kNow - 59, first);
    const int64_t saved = save_epoch(kNow * 60 + 30, -1, kNow * 60, 0, first);
    TEST_ASSERT_EQUAL_INT64(first * 60 - 1, saved);
    TEST_ASSERT_EQUAL_INT64(first, replay_first_minute(saved, kNow));
    // Dated now, as before, it would have left none of them.
    TEST_ASSERT_EQUAL_INT64(-1, replay_first_minute(kNow * 60 + 30, kNow));
}

static void test_a_scheduled_step_that_gave_way_chose_nothing_of_what_plays() {
    const uint16_t effects[] = {2, 1};
    State state(32);
    state.set_effect_count(3);
    state.set_playlist(effects, 2, 60);
    state.set_power(true);
    // The owner changed the brightness alone while the step choosing an effect was worked out.
    state.note_owner_choice();
    state.apply_action(40, 0, -1, -1);
    TEST_ASSERT_FALSE(state.apply_scheduled(0, 0, 1, kPowerKeep));
    TEST_ASSERT_FALSE(state.scheduled_play());
    // So the playlist left playing still comes back once its mark is read.
    TEST_ASSERT_TRUE(state.resume_playlist_unless_chosen());
    TEST_ASSERT_TRUE(state.snapshot().playlist);
    // One that took counts, and a resume after it yields.
    State stepped(32);
    stepped.set_effect_count(3);
    stepped.set_playlist(effects, 2, 60);
    TEST_ASSERT_TRUE(stepped.apply_scheduled(0, 0, 1, kPowerKeep));
    TEST_ASSERT_TRUE(stepped.scheduled_play());
    TEST_ASSERT_FALSE(stepped.resume_playlist_unless_chosen());
}

static void test_a_choice_against_what_flash_holds_is_saved_at_once() {
    // Flash holds on, and the owner turned off before it could be read, or before the scheduler began:
    // against what flash holds, that is a turn off, saved at once, whatever the state held before.
    const Remembered in_flash{1, 50, 4, true};
    Remembered chosen = in_flash;
    chosen.playing = false;
    MemorySaveTimer timer(in_flash);
    timer.note_choice();
    TEST_ASSERT_TRUE(timer.should_save(chosen, 1000));
    // The other way, as a garland left off and turned on by a button during the start.
    MemorySaveTimer off(chosen);
    off.note_choice();
    TEST_ASSERT_TRUE(off.should_save(in_flash, 1000));
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

// A late restore builds its timer from what flash holds: a turn off made while the playlist's mark was read
// is a turn off to save at once, which a timer built from the state after those reads would take as saved.
static void test_a_turn_off_during_a_late_restore_is_saved_at_once() {
    const Remembered in_flash{1, 50, 4, true, false};
    Remembered turned_off = in_flash;
    turned_off.playing = false;
    MemorySaveTimer from_flash(in_flash);
    from_flash.note_choice();
    TEST_ASSERT_TRUE(from_flash.should_save(turned_off, 1000));
    MemorySaveTimer from_state(turned_off);
    from_state.note_choice();
    TEST_ASSERT_FALSE(from_state.should_save(turned_off, 1000));
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
    RUN_TEST(test_a_late_restore_comes_whole_and_yields_to_any_choice_since_the_start);
    RUN_TEST(test_a_late_restore_stops_a_playlist_a_step_started_meanwhile);
    RUN_TEST(test_a_late_restore_after_a_power_choice_still_resumes_the_playlist);
    RUN_TEST(test_a_save_before_a_replay_not_begun_leaves_its_steps_to_do);
    RUN_TEST(test_a_scheduled_step_that_gave_way_chose_nothing_of_what_plays);
    RUN_TEST(test_a_choice_against_what_flash_holds_is_saved_at_once);
    RUN_TEST(test_a_turn_off_during_a_late_restore_is_saved_at_once);
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
    RUN_TEST(test_a_save_is_dated_so_that_what_waits_still_comes);
    RUN_TEST(test_only_a_choice_of_what_plays_says_what_plays);
    RUN_TEST(test_a_change_of_only_the_levels_leaves_the_playlist_to_resume);
    RUN_TEST(test_a_resume_yields_to_a_choice_of_what_plays_made_with_the_mark_up_already);
}
