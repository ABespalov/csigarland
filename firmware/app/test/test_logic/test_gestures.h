/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Host tests for button gestures, the off countdown and the screensaver
 * Contributors: None
 *
 * Included by test_main.cpp, which provides Unity and the garland namespace.
 */

#pragma once

#include "output_health.h"

static void light_up(State& state) {
    state.set_effect_count(3);
    state.set_favorites(0b110);
    state.set_power(true);
}

static void test_off_countdown_keeps_the_garland_lit_then_turns_off() {
    State state;
    state.set_power(true);
    state.long_press_power(1000);
    Snapshot s = state.snapshot();
    TEST_ASSERT_TRUE(s.power_requested);
    TEST_ASSERT_EQUAL_UINT8(5, s.off_seconds);

    state.tick_off_countdown(1001);
    TEST_ASSERT_EQUAL_UINT8(5, state.snapshot().off_seconds);
    state.tick_off_countdown(2500);
    TEST_ASSERT_EQUAL_UINT8(4, state.snapshot().off_seconds);
    state.tick_off_countdown(5999);
    TEST_ASSERT_EQUAL_UINT8(1, state.snapshot().off_seconds);
    TEST_ASSERT_TRUE(state.snapshot().power_requested);

    state.tick_off_countdown(6000);
    s = state.snapshot();
    TEST_ASSERT_FALSE(s.power_requested);
    TEST_ASSERT_EQUAL_UINT8(0, s.off_seconds);
}

static void test_off_countdown_survives_the_clock_wrapping() {
    State state;
    state.set_power(true);
    state.long_press_power(0xFFFFF000u);
    state.tick_off_countdown(0xFFFFF800u);
    TEST_ASSERT_TRUE(state.snapshot().power_requested);
    state.tick_off_countdown(0xFFFFF000u + kOffCountdownMs);
    TEST_ASSERT_FALSE(state.snapshot().power_requested);
}

static void test_any_button_cancels_the_countdown_and_does_nothing_else() {
    const auto cancel_with = [](ButtonId button) {
        State state;
        state.set_effect_count(3);
        state.set_favorites(0b111);
        state.set_power(true);
        const Snapshot before = state.snapshot();
        state.long_press_power(1000);

        state.button_down(button);
        state.click_power();
        state.click_effect();
        state.click_speed();
        state.tick_off_countdown(1000 + kOffCountdownMs);

        const Snapshot after = state.snapshot();
        TEST_ASSERT_TRUE(after.power_requested);
        TEST_ASSERT_EQUAL_UINT8(0, after.off_seconds);
        TEST_ASSERT_EQUAL_UINT8(button == ButtonId::Power ? before.brightness_percent : next_brightness(before.brightness_percent),
                                after.brightness_percent);
        TEST_ASSERT_EQUAL_UINT8(button == ButtonId::Effect ? before.effect : 1, after.effect);
        TEST_ASSERT_EQUAL_UINT8(button == ButtonId::Speed ? before.speed_x4 : next_speed(before.speed_x4), after.speed_x4);
    };
    cancel_with(ButtonId::Power);
    cancel_with(ButtonId::Effect);
    cancel_with(ButtonId::Speed);

    // The next press of the same button works again.
    State state;
    state.set_power(true);
    state.long_press_power(1000);
    state.button_down(ButtonId::Power);
    state.click_power();
    TEST_ASSERT_EQUAL_UINT8(50, state.snapshot().brightness_percent);
    state.button_down(ButtonId::Power);
    state.click_power();
    TEST_ASSERT_EQUAL_UINT8(75, state.snapshot().brightness_percent);
}

static void test_holding_the_cancelling_press_does_not_restart_the_countdown() {
    State state;
    state.set_power(true);
    state.long_press_power(1000);
    state.button_down(ButtonId::Power);
    state.long_press_power(1800);
    TEST_ASSERT_EQUAL_UINT8(0, state.snapshot().off_seconds);
}

static void test_a_deliberate_order_ends_the_countdown() {
    State state;
    state.set_effect_count(1);
    state.set_power(true);
    state.long_press_power(1000);
    state.activate_effect(0);
    state.tick_off_countdown(1000 + kOffCountdownMs);
    TEST_ASSERT_TRUE(state.snapshot().power_requested);

    state.long_press_power(9000);
    state.set_power(false);
    TEST_ASSERT_EQUAL_UINT8(0, state.snapshot().off_seconds);
}

static void test_long_press_power_in_standby_does_nothing() {
    State state;
    state.long_press_power(1000);
    TEST_ASSERT_EQUAL_UINT8(0, state.snapshot().off_seconds);
}

static void test_double_click_effect_goes_to_the_first_favorite_when_on() {
    State state;
    light_up(state);
    state.set_effect(2);
    state.double_click_effect();
    TEST_ASSERT_EQUAL_UINT8(1, state.snapshot().effect);

    state.set_effect(2);
    state.set_power(false);
    state.double_click_effect();
    TEST_ASSERT_EQUAL_UINT8(2, state.snapshot().effect);
}

// A long press starts the playlist, when there is one and the garland is on, and stops it, keeping
// what plays. With no playlist it does nothing, the first favourite being the double click's.
static void test_long_press_effect_starts_and_stops_the_playlist() {
    State state;
    light_up(state);
    state.set_effect(2);
    state.long_press_effect();
    TEST_ASSERT_EQUAL_UINT8(2, state.snapshot().effect);
    TEST_ASSERT_FALSE(state.snapshot().playlist);

    const uint16_t list[] = {0, 1};
    state.set_playlist(list, 2, 30);
    state.long_press_effect();
    TEST_ASSERT_TRUE(state.snapshot().playlist);
    TEST_ASSERT_EQUAL_UINT8(0, state.snapshot().effect);
    TEST_ASSERT_TRUE(state.advance_playlist(0, 1, state.snapshot().playlist_mark));
    state.long_press_effect();
    TEST_ASSERT_FALSE(state.snapshot().playlist);
    TEST_ASSERT_EQUAL_UINT8(1, state.snapshot().effect);

    state.set_power(false);
    state.long_press_effect();
    TEST_ASSERT_FALSE(state.snapshot().playlist);
}

static void test_long_press_speed_returns_to_normal_when_on() {
    State state;
    state.set_power(true);
    state.set_speed(8);
    state.long_press_speed();
    TEST_ASSERT_EQUAL_UINT8(4, state.snapshot().speed_x4);

    state.set_speed(2);
    state.set_power(false);
    state.long_press_speed();
    TEST_ASSERT_EQUAL_UINT8(2, state.snapshot().speed_x4);
}

static void test_a_press_that_wakes_the_screen_does_nothing_else() {
    State state;
    light_up(state);
    state.set_saver_active(true);
    const uint32_t before = state.activity();
    state.button_down(ButtonId::Speed);
    state.click_speed();
    state.long_press_speed();
    TEST_ASSERT_EQUAL_UINT8(4, state.snapshot().speed_x4);
    TEST_ASSERT_NOT_EQUAL(before, state.activity());

    state.button_down(ButtonId::Speed);
    state.click_speed();
    TEST_ASSERT_EQUAL_UINT8(6, state.snapshot().speed_x4);
}

static void test_schedule_accepts_half_speed() {
    ScheduleEntry entry{};
    entry.days = kAllDays;
    entry.speed_x4 = 2;
    const char* error = nullptr;
    TEST_ASSERT_TRUE(entry_is_valid(entry, error));
    entry.speed_x4 = 5;
    TEST_ASSERT_FALSE(entry_is_valid(entry, error));
}

/* Setup network password */

static void test_ap_password_uses_its_alphabet_evenly() {
    Rng rng(99);
    std::array<int, kApPasswordAlphabetSize> seen{};
    constexpr int kPasswords = 3100;
    for (int n = 0; n < kPasswords; ++n) {
        char password[kApPasswordLength + 1];
        make_ap_password(password, [&] { return static_cast<uint8_t>(rng.next() >> 8); });
        TEST_ASSERT_TRUE(ap_password_is_valid(password));
        for (size_t i = 0; i < kApPasswordLength; ++i) {
            ++seen[static_cast<size_t>(std::strchr(kApPasswordAlphabet, password[i]) - kApPasswordAlphabet)];
        }
    }
    const int expected = kPasswords * static_cast<int>(kApPasswordLength / kApPasswordAlphabetSize) +
                         kPasswords * static_cast<int>(kApPasswordLength % kApPasswordAlphabetSize) /
                             static_cast<int>(kApPasswordAlphabetSize);
    for (const int count : seen) {
        TEST_ASSERT_INT_WITHIN(expected / 5, expected, count);
    }
}

static void test_ap_password_skips_bytes_that_would_skew_it() {
    // 248 is the first byte past the last whole multiple of the alphabet: it must not
    // come out as the letter it would map to, only the valid bytes after it count.
    int calls = 0;
    char password[kApPasswordLength + 1];
    make_ap_password(password, [&] { return static_cast<uint8_t>(calls++ % 2 == 0 ? 248 : 1); });
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kApPasswordLength) * 2, calls);
    TEST_ASSERT_EQUAL_STRING("bbbbbbbbbbbb", password);
}

static void test_ap_password_check_rejects_what_was_not_made() {
    TEST_ASSERT_TRUE(ap_password_is_valid("abcdefghjkmn"));
    TEST_ASSERT_FALSE(ap_password_is_valid(""));
    TEST_ASSERT_FALSE(ap_password_is_valid("abcdefghjkm"));
    TEST_ASSERT_FALSE(ap_password_is_valid("abcdefghjkmnp"));
    TEST_ASSERT_FALSE(ap_password_is_valid("abcdefghjkm0"));
    TEST_ASSERT_FALSE(ap_password_is_valid("festive2026"));
}

/* Backlight */

static void test_backlight_duty_follows_the_eye() {
    TEST_ASSERT_EQUAL_UINT32(0, backlight_duty(0, 1023));
    TEST_ASSERT_EQUAL_UINT32(1023, backlight_duty(100, 1023));
    TEST_ASSERT_EQUAL_UINT32(255, backlight_duty(50, 1023));
    // The dimmest allowed level still lights, and each step up is brighter.
    TEST_ASSERT_GREATER_THAN_UINT32(0, backlight_duty(kBacklightMinPercent, 1023));
    TEST_ASSERT_GREATER_THAN_UINT32(backlight_duty(30, 1023), backlight_duty(31, 1023));
    TEST_ASSERT_EQUAL_UINT32(1023, backlight_duty(200, 1023));
}

static void test_backlight_settings_range_and_no_wake() {
    TEST_ASSERT_FALSE(backlight_percent_is_valid(kBacklightMinPercent - 1));
    TEST_ASSERT_TRUE(backlight_percent_is_valid(kBacklightMinPercent));
    TEST_ASSERT_TRUE(backlight_percent_is_valid(100));
    TEST_ASSERT_FALSE(backlight_percent_is_valid(101));

    State state;
    const uint32_t before = state.activity();
    state.set_backlight_levels(60, 10);
    TEST_ASSERT_EQUAL_UINT8(60, state.screen_percent());
    TEST_ASSERT_EQUAL_UINT8(10, state.saver_percent());
    TEST_ASSERT_EQUAL_UINT32(before, state.activity());
}

/* Remembered settings */

static void test_settings_are_saved_only_once_they_have_stayed() {
    const Remembered first{1, 50, 10};
    const Remembered second{2, 50, 10};
    const Remembered third{3, 50, 10};
    MemorySaveTimer timer(first);
    TEST_ASSERT_FALSE(timer.should_save(first, 0));
    TEST_ASSERT_FALSE(timer.should_save(second, 1000));
    TEST_ASSERT_FALSE(timer.should_save(second, 1000 + kMemorySaveDelayMs - 1));

    // Clicking on to another effect starts the wait again.
    TEST_ASSERT_FALSE(timer.should_save(third, 5000));
    TEST_ASSERT_FALSE(timer.should_save(third, 5000 + kMemorySaveDelayMs - 1));
    TEST_ASSERT_TRUE(timer.should_save(third, 5000 + kMemorySaveDelayMs));

    // After a failed write, the whole delay again.
    timer.retry_later(20000);
    TEST_ASSERT_FALSE(timer.should_save(third, 20000 + kMemorySaveDelayMs - 1));
    TEST_ASSERT_TRUE(timer.should_save(third, 20000 + kMemorySaveDelayMs));

    // Saved: nothing more to do until it changes.
    timer.saved(third);
    TEST_ASSERT_FALSE(timer.should_save(third, 100000));
    // Back to the saved one before the delay: no write at all.
    TEST_ASSERT_FALSE(timer.should_save(first, 110000));
    TEST_ASSERT_FALSE(timer.should_save(third, 111000));
    TEST_ASSERT_FALSE(timer.should_save(third, 200000));
}

static void test_brightness_and_speed_count_as_changes_too() {
    const Remembered saved{1, 50, 10};
    MemorySaveTimer timer(saved);
    TEST_ASSERT_FALSE(timer.should_save(Remembered{1, 75, 10}, 1000));
    TEST_ASSERT_TRUE(timer.should_save(Remembered{1, 75, 10}, 1000 + kMemorySaveDelayMs));
    timer.saved(Remembered{1, 75, 10});

    // A speed click during the wait starts it over, for all three together.
    TEST_ASSERT_FALSE(timer.should_save(Remembered{1, 25, 10}, 20000));
    TEST_ASSERT_FALSE(timer.should_save(Remembered{1, 25, 15}, 25000));
    TEST_ASSERT_FALSE(timer.should_save(Remembered{1, 25, 15}, 20000 + kMemorySaveDelayMs));
    TEST_ASSERT_TRUE(timer.should_save(Remembered{1, 25, 15}, 25000 + kMemorySaveDelayMs));
}

static void test_turning_on_or_off_is_saved_at_once() {
    const Remembered off{1, 50, 10, false};
    const Remembered on{1, 50, 10, true};
    MemorySaveTimer timer(off);
    TEST_ASSERT_TRUE(timer.should_save(on, 1000));

    // A failed write does not try again on every poll.
    timer.retry_later(1000);
    TEST_ASSERT_FALSE(timer.should_save(on, 1001));
    TEST_ASSERT_TRUE(timer.should_save(on, 1000 + kMemorySaveDelayMs));
    timer.saved(on);
    TEST_ASSERT_FALSE(timer.should_save(on, 20000));
    TEST_ASSERT_TRUE(timer.should_save(off, 20001));
}

static void test_a_refusal_does_not_change_the_owners_choice() {
    State state(32);
    state.set_power(true);
    TEST_ASSERT_TRUE(state.snapshot().power_chosen);
    state.refuse_power();
    TEST_ASSERT_FALSE(state.snapshot().power_requested);
    TEST_ASSERT_TRUE(state.snapshot().power_chosen);
    state.set_power(false);
    TEST_ASSERT_FALSE(state.snapshot().power_chosen);
}

static void test_remembered_values_are_checked_before_use() {
    TEST_ASSERT_FALSE(remembered_brightness_is_valid(0));
    TEST_ASSERT_TRUE(remembered_brightness_is_valid(1));
    TEST_ASSERT_TRUE(remembered_brightness_is_valid(100));
    TEST_ASSERT_FALSE(remembered_brightness_is_valid(101));
    TEST_ASSERT_TRUE(speed_x4_is_valid(1));
    TEST_ASSERT_TRUE(speed_x4_is_valid(12));
    TEST_ASSERT_FALSE(speed_x4_is_valid(0));
    TEST_ASSERT_FALSE(speed_x4_is_valid(5));
}

/* Garland presence */

static void test_presence_readings_from_this_board() {
    TEST_ASSERT_TRUE(probe_says_present(1901));   // A strip on a live rail.
    TEST_ASSERT_TRUE(probe_says_present(1937));
    TEST_ASSERT_FALSE(probe_says_present(2655));  // Nothing connected.
    TEST_ASSERT_FALSE(probe_says_present(2660));
}

static void test_presence_check_keeps_the_estimate_only_while_present() {
    State state;
    state.set_probe(ProbeStatus::Present, 40);
    state.set_presence(ProbeStatus::Present);
    TEST_ASSERT_EQUAL_UINT16(40, state.snapshot().led_estimate);

    state.set_presence(ProbeStatus::Absent);
    TEST_ASSERT_TRUE(state.snapshot().probe == ProbeStatus::Absent);
    TEST_ASSERT_EQUAL_UINT16(0, state.snapshot().led_estimate);

    // Back while lit, as after one reading that found none: the count it had still stands.
    state.set_presence(ProbeStatus::Present);
    TEST_ASSERT_TRUE(state.snapshot().probe == ProbeStatus::Present);
    TEST_ASSERT_EQUAL_UINT16(40, state.snapshot().led_estimate);
    TEST_ASSERT_EQUAL_UINT16(40, state.snapshot().led_count);

    // A full count finding none does replace it.
    state.set_probe(ProbeStatus::Present, 0);
    TEST_ASSERT_EQUAL_UINT16(32, state.snapshot().led_count);
}

/* Network search */

static void test_scan_retries_before_saying_there_is_nothing() {
    TEST_ASSERT_TRUE(scan_step_after_done(true, 5, 1) == ScanStep::Accept);
    // An empty or cut-short scan may only mean the radio was busy.
    TEST_ASSERT_TRUE(scan_step_after_done(true, 0, 1) == ScanStep::Retry);
    TEST_ASSERT_TRUE(scan_step_after_done(false, 0, 1) == ScanStep::Retry);
    TEST_ASSERT_TRUE(scan_step_after_done(false, 3, 2) == ScanStep::Retry);
    // After the last try: truly no networks, or a failure, and they are told apart.
    TEST_ASSERT_TRUE(scan_step_after_done(true, 0, 3) == ScanStep::Accept);
    TEST_ASSERT_TRUE(scan_step_after_done(false, 0, 3) == ScanStep::Fail);
}

static void test_scan_list_is_one_entry_per_name_strongest_first() {
    std::array<ScanEntry, 5> entries = {{{"Home", -70, false},
                                         {"", -30, false},
                                         {"Guest", -50, true},
                                         {"Home", -40, false},
                                         {"Home", -80, false}}};
    TEST_ASSERT_EQUAL_size_t(2, tidy_scan_entries(entries.data(), entries.size()));
    TEST_ASSERT_EQUAL_STRING("Home", entries[0].ssid);
    TEST_ASSERT_EQUAL_INT8(-40, entries[0].rssi);
    TEST_ASSERT_EQUAL_STRING("Guest", entries[1].ssid);
    TEST_ASSERT_TRUE(entries[1].open);
    TEST_ASSERT_EQUAL_size_t(0, tidy_scan_entries(entries.data(), 0));
}

/* Update file */

constexpr uint32_t kTestCertificateBytes = 500;

constexpr size_t kTestSlot = 0x1E0000;
constexpr size_t kTestPackRoom = 0x7000;

static std::array<uint8_t, kOtaHeaderBytes> ota_header(uint32_t pack_bytes, uint32_t image_bytes,
                                                       uint32_t certificate_bytes = kTestCertificateBytes) {
    std::array<uint8_t, kOtaHeaderBytes> header{};
    std::memcpy(header.data(), kOtaMagic.data(), kOtaMagic.size());
    pack_write_u32(header.data() + 4, kOtaFormat);
    pack_write_u32(header.data() + 8, certificate_bytes);
    pack_write_u32(header.data() + 12, pack_bytes);
    pack_write_u32(header.data() + 16, image_bytes);
    header[20] = 0x11;
    header[20 + kOtaDigestBytes] = 0x22;
    header[kOtaSignedBytes] = 0xAB;
    header[kOtaHeaderBytes - 1] = 0xCD;
    return header;
}

static const char* parse_ota(const std::array<uint8_t, kOtaHeaderBytes>& header, size_t file_bytes, OtaHeader& parsed) {
    return ota_parse_header(header.data(), file_bytes, kTestSlot, kTestPackRoom, parsed);
}

static void test_ota_header_reads_a_good_file() {
    constexpr size_t kPrefix = kOtaHeaderBytes + kTestCertificateBytes;
    const auto header = ota_header(0x4000, 0x123456);
    OtaHeader parsed;
    TEST_ASSERT_NULL(parse_ota(header, kPrefix + 0x4000 + 0x123456, parsed));
    TEST_ASSERT_EQUAL_UINT32(kTestCertificateBytes, parsed.certificate_bytes);
    TEST_ASSERT_EQUAL_UINT32(0x4000, parsed.pack_bytes);
    TEST_ASSERT_EQUAL_UINT32(0x123456, parsed.image_bytes);
    TEST_ASSERT_EQUAL_UINT8(0x11, parsed.pack_digest[0]);
    TEST_ASSERT_EQUAL_UINT8(0x22, parsed.image_digest[0]);
    TEST_ASSERT_EQUAL_UINT8(0xAB, parsed.signature[0]);
    TEST_ASSERT_EQUAL_UINT8(0xCD, parsed.signature[kOtaSignatureBytes - 1]);
}

static void test_ota_header_takes_either_part_alone() {
    constexpr size_t kPrefix = kOtaHeaderBytes + kTestCertificateBytes;
    OtaHeader parsed;
    TEST_ASSERT_NULL(parse_ota(ota_header(0x4000, 0), kPrefix + 0x4000, parsed));
    TEST_ASSERT_EQUAL_UINT32(0, parsed.image_bytes);
    TEST_ASSERT_NULL(parse_ota(ota_header(0, 1000), kPrefix + 1000, parsed));
    TEST_ASSERT_EQUAL_UINT32(0, parsed.pack_bytes);
}

static void test_ota_header_rejects_what_it_should() {
    OtaHeader parsed;
    constexpr size_t kPrefix = kOtaHeaderBytes + kTestCertificateBytes;
    auto header = ota_header(0, 1000);
    // Truncated, padded, too short to hold the header at all.
    TEST_ASSERT_NOT_NULL(parse_ota(header, kPrefix + 999, parsed));
    TEST_ASSERT_NOT_NULL(parse_ota(header, kPrefix + 1001, parsed));
    TEST_ASSERT_NOT_NULL(parse_ota(header, kOtaHeaderBytes - 1, parsed));
    TEST_ASSERT_NOT_NULL(parse_ota(header, kOtaHeaderBytes + 10, parsed));
    // A plain firmware.bin is not an update file, nor is one larger than the slot.
    header[0] = 0xE9;
    TEST_ASSERT_NOT_NULL(parse_ota(header, kPrefix + 1000, parsed));
    TEST_ASSERT_NOT_NULL(parse_ota(ota_header(0, kTestSlot + 1), kPrefix + kTestSlot + 1, parsed));
    TEST_ASSERT_NOT_NULL(parse_ota(ota_header(kTestPackRoom + 1, 0), kPrefix + kTestPackRoom + 1, parsed));
    // A later format, and an earlier one that signed only the image.
    header = ota_header(0, 1000);
    header[4] = kOtaFormat + 1;
    TEST_ASSERT_NOT_NULL(parse_ota(header, kPrefix + 1000, parsed));
    header[4] = kOtaFormat - 1;
    TEST_ASSERT_NOT_NULL(parse_ota(header, kPrefix + 1000, parsed));
    // Neither part.
    TEST_ASSERT_NOT_NULL(parse_ota(ota_header(0, 0), kPrefix, parsed));
    // No certificate, and one larger than the buffer it has to fit.
    TEST_ASSERT_NOT_NULL(parse_ota(ota_header(0, 1000, 0), kOtaHeaderBytes + 1000, parsed));
    TEST_ASSERT_NOT_NULL(parse_ota(ota_header(0, 1000, kOtaMaxCertificateBytes + 1), kOtaHeaderBytes + kOtaMaxCertificateBytes + 1 + 1000, parsed));
    // Sizes that only add up by wrapping around.
    TEST_ASSERT_NOT_NULL(parse_ota(ota_header(0xFFFFFFFFu, 2), kPrefix + 1, parsed));
}

static void test_ota_percent() {
    TEST_ASSERT_EQUAL_UINT8(0, ota_percent(0, 0));
    TEST_ASSERT_EQUAL_UINT8(0, ota_percent(0, 2000000));
    TEST_ASSERT_EQUAL_UINT8(50, ota_percent(1000000, 2000000));
    TEST_ASSERT_EQUAL_UINT8(100, ota_percent(2000000, 2000000));
    TEST_ASSERT_NOT_EQUAL(kNoUpdate, ota_percent(2000000, 2000000));
}

static void test_update_progress_shown() {
    TEST_ASSERT_EQUAL_UINT8(37, update_shown_percent(37));
    TEST_ASSERT_EQUAL_UINT8(100, update_shown_percent(kUpdateApplying));
    TEST_ASSERT_EQUAL_UINT8(100, update_shown_percent(kUpdateRestarting));
    TEST_ASSERT_EQUAL_INT(0, update_bar_pixels(0, 208));
    TEST_ASSERT_EQUAL_INT(104, update_bar_pixels(50, 208));
    TEST_ASSERT_EQUAL_INT(208, update_bar_pixels(100, 208));
    TEST_ASSERT_EQUAL_INT(208, update_bar_pixels(kUpdateApplying, 208));
    TEST_ASSERT_EQUAL_INT(208, update_bar_pixels(kUpdateRestarting, 208));
    TEST_ASSERT_NOT_EQUAL(kNoUpdate, kUpdateRestarting);
}

/* Screensaver */

static Screensaver running_saver(SaverKind kind) {
    Screensaver saver;
    saver.configure(240, 240);
    saver.set_sprite_size(100, 40);
    saver.set_kind(kind);
    saver.set_idle_delay(60000);
    saver.note_activity(0);
    return saver;
}

static void test_saver_starts_after_its_delay_and_a_press_ends_it() {
    Screensaver saver = running_saver(SaverKind::Starfield);
    TEST_ASSERT_FALSE(saver.poll(59999, false).active);
    const ScreensaverView view = saver.poll(60000, false);
    TEST_ASSERT_TRUE(view.active);
    TEST_ASSERT_TRUE(view.changed);

    saver.note_activity(61000);
    TEST_ASSERT_FALSE(saver.poll(61001, false).active);
    TEST_ASSERT_FALSE(saver.poll(120999, false).active);
    TEST_ASSERT_TRUE(saver.poll(121000, false).active);
}

// A sprite that grows near an edge is kept within the panel at once, not at the next step: Scheduler24's words grow
// as the time counts, from "59s" to "1m 00s".
static void test_a_sprite_that_grows_stays_on_the_panel() {
    Screensaver saver = running_saver(SaverKind::FlyingClock);
    TEST_ASSERT_TRUE(saver.poll(60000, false).active);
    for (uint64_t now = 60060; saver.x() < 240 - 100 - 2; now += 60) {
        saver.poll(now, false);
    }
    saver.set_sprite_size(160, 60);
    TEST_ASSERT_TRUE(saver.x() <= 240 - 160);
    TEST_ASSERT_TRUE(saver.y() <= 240 - 60);
    // Shrunk, it stays where it was.
    const int16_t x = saver.x();
    saver.set_sprite_size(40, 20);
    TEST_ASSERT_EQUAL_INT(x, saver.x());
}

static void test_scheduler_words_start_in_the_centre() {
    Screensaver saver = running_saver(SaverKind::Scheduler24);
    saver.set_sprite_size(80, 40);
    saver.recenter();
    TEST_ASSERT_EQUAL_INT((240 - 80) / 2, saver.x());
    TEST_ASSERT_EQUAL_INT((240 - 40) / 2, saver.y());
}

static void test_saver_none_never_starts() {
    Screensaver saver = running_saver(SaverKind::None);
    TEST_ASSERT_FALSE(saver.poll(10000000, false).active);
}

static void test_saver_stays_off_the_countdown() {
    Screensaver saver = running_saver(SaverKind::Starfield);
    TEST_ASSERT_FALSE(saver.poll(70000, true).active);
    // The veto holds the idle count too, so the delay is whole afterwards.
    TEST_ASSERT_FALSE(saver.poll(70001, false).active);
    TEST_ASSERT_TRUE(saver.poll(130000, false).active);
    TEST_ASSERT_FALSE(saver.poll(130001, true).active);
}

static void test_starfield_stays_on_the_panel() {
    Screensaver saver = running_saver(SaverKind::Starfield);
    uint32_t moved = 0;
    saver.poll(60000, false);
    for (uint64_t ms = 60050; ms < 90000; ms += 50) {
        const int16_t before = saver.stars()[0].x;
        TEST_ASSERT_TRUE(saver.poll(ms, false).changed);
        moved += saver.stars()[0].x != before ? 1 : 0;
        for (size_t i = 0; i < Screensaver::kStarCount; ++i) {
            const ScreensaverStar& star = saver.stars()[i];
            TEST_ASSERT_TRUE(star.x >= 0 && star.x < 240 && star.y >= 0 && star.y < 240);
            const int size = star_pixels(star_approach_q8(star, 3840));
            TEST_ASSERT_TRUE(size >= 3 && size <= 6);
        }
    }
    TEST_ASSERT_GREATER_THAN_UINT32(0, moved);
}

static void test_flying_clock_bounces_inside_the_panel() {
    Screensaver saver = running_saver(SaverKind::FlyingClock);
    saver.poll(60000, false);
    bool reached_right = false;
    for (uint64_t ms = 60060; ms < 200000; ms += 60) {
        const ScreensaverView view = saver.poll(ms, false);
        TEST_ASSERT_TRUE(view.x >= 0 && view.x <= 140 && view.y >= 0 && view.y <= 200);
        reached_right = reached_right || view.x == 140;
    }
    TEST_ASSERT_TRUE(reached_right);
}

// Scheduler24 flies as the clock does, its two lines a sprite of the size the display gives it.
static void test_scheduler24_bounces_as_the_clock_does() {
    Screensaver saver = running_saver(SaverKind::Scheduler24);
    saver.set_sprite_size(180, 52);
    TEST_ASSERT_TRUE(saver.poll(60000, false).active);
    bool moved = false;
    int16_t first_x = -1;
    for (uint64_t ms = 60060; ms < 120000; ms += 60) {
        const ScreensaverView view = saver.poll(ms, false);
        TEST_ASSERT_TRUE(view.x >= 0 && view.x <= 60 && view.y >= 0 && view.y <= 188);
        first_x = first_x < 0 ? view.x : first_x;
        moved = moved || view.x != first_x;
    }
    TEST_ASSERT_TRUE(moved);
    // It keeps no clock of its own: its words are the display's.
    TEST_ASSERT_EQUAL_STRING("", saver.clock_time());
}

static void test_flying_clock_shows_uptime_until_the_time_is_known() {
    Screensaver saver = running_saver(SaverKind::FlyingClock);
    saver.poll(3723000, false);
    TEST_ASSERT_EQUAL_STRING("uptime", saver.clock_label());
    TEST_ASSERT_EQUAL_STRING("01:02:03", saver.clock_time());

    saver.set_wall_clock("18:05:33", true);
    saver.poll(3723060, false);
    TEST_ASSERT_EQUAL_STRING("", saver.clock_label());
    TEST_ASSERT_EQUAL_STRING("18:05:33", saver.clock_time());

    saver.set_wall_clock("", false);
    TEST_ASSERT_EQUAL_STRING("uptime", saver.clock_label());
}

static void test_breath_rises_and_falls() {
    TEST_ASSERT_EQUAL_UINT8(0, breath_wave(0));
    TEST_ASSERT_EQUAL_UINT8(255, breath_wave(kBreathPeriodMs / 2));
    TEST_ASSERT_EQUAL_UINT8(0, breath_wave(kBreathPeriodMs));
    TEST_ASSERT_EQUAL_UINT8(breath_wave(1000), breath_wave(3000));
}

static void test_saver_delay_range() {
    TEST_ASSERT_FALSE(saver_delay_is_valid(9));
    TEST_ASSERT_TRUE(saver_delay_is_valid(10));
    TEST_ASSERT_TRUE(saver_delay_is_valid(600));
    TEST_ASSERT_FALSE(saver_delay_is_valid(601));
    TEST_ASSERT_TRUE(saver_kind_is_valid(4));
    // Scheduler24, new after 1.1.1, which a firmware before it reads as none valid.
    TEST_ASSERT_TRUE(saver_kind_is_valid(5));
    TEST_ASSERT_FALSE(saver_kind_is_valid(6));
}

/* What the portal shows */

static void test_next_action_is_the_first_one_due() {
    constexpr int64_t kThursdayNoon = 12 * 60;  // Minute 0 of the epoch was a Thursday.
    const std::array<ScheduleEntry, 2> entries = {make_entry(7, 30, PowerStep::Off), make_entry(18, 0, PowerStep::On)};
    TEST_ASSERT_EQUAL_INT64(18 * 60, next_action_minute(entries.data(), 2, kThursdayNoon, utc_minute));
    // Not the one due this very minute, and the next day's after the last of today.
    TEST_ASSERT_EQUAL_INT64(1440 + 7 * 60 + 30, next_action_minute(entries.data(), 2, 18 * 60, utc_minute));
}

static void test_next_action_respects_days_and_enabled() {
    ScheduleEntry sunday = make_entry(9, 0, PowerStep::On);
    sunday.days = 1;
    // From Thursday noon, three days on.
    TEST_ASSERT_EQUAL_INT64(3 * 1440 + 9 * 60, next_action_minute(&sunday, 1, 12 * 60, utc_minute));
    // Exactly a week on, for the same minute of the same day.
    TEST_ASSERT_EQUAL_INT64(3 * 1440 + 9 * 60 + 7 * 1440, next_action_minute(&sunday, 1, 3 * 1440 + 9 * 60, utc_minute));
    sunday.enabled = false;
    TEST_ASSERT_EQUAL_INT64(-1, next_action_minute(&sunday, 1, 12 * 60, utc_minute));
    TEST_ASSERT_EQUAL_INT64(-1, next_action_minute(&sunday, 0, 12 * 60, utc_minute));
}

static void test_hostname_rules() {
    TEST_ASSERT_TRUE(hostname_is_valid("csigarland"));
    TEST_ASSERT_TRUE(hostname_is_valid("a"));
    TEST_ASSERT_TRUE(hostname_is_valid("my-garland-2"));
    TEST_ASSERT_TRUE(hostname_is_valid("01234567890123456789012345678901"));
    TEST_ASSERT_FALSE(hostname_is_valid("012345678901234567890123456789012"));
    TEST_ASSERT_FALSE(hostname_is_valid(""));
    TEST_ASSERT_FALSE(hostname_is_valid("-garland"));
    TEST_ASSERT_FALSE(hostname_is_valid("garland-"));
    TEST_ASSERT_FALSE(hostname_is_valid("Garland"));
    TEST_ASSERT_FALSE(hostname_is_valid("my garland"));
    TEST_ASSERT_FALSE(hostname_is_valid("garland.local"));
}

// A power cut before new ones are saved leaves the old ones in flash, and the next start may have no
// clock to rule them out again: the mark has to be what does it.
static void test_a_certificate_ruled_out_stays_so_until_replaced() {
    static TlsIdentity identity;
    identity = TlsIdentity{};
    std::snprintf(identity.root_certificate, sizeof(identity.root_certificate), "root");
    std::snprintf(identity.certificate, sizeof(identity.certificate), "leaf");
    TlsStarts starts{};
    starts.root_not_before = 1000;
    starts.not_before = 5000;

    tls_drop_ruled_out(identity, starts, 2000);
    TEST_ASSERT_EQUAL_STRING("leaf", identity.certificate);

    starts.ruled_out = 1;
    tls_drop_ruled_out(identity, starts, 2000);
    TEST_ASSERT_EQUAL_STRING("", identity.certificate);
    TEST_ASSERT_EQUAL_STRING("root", identity.root_certificate);
    TEST_ASSERT_EQUAL_UINT8(1, starts.ruled_out);

    // Loaded again from flash after the cut, the same happens.
    std::snprintf(identity.certificate, sizeof(identity.certificate), "leaf");
    tls_drop_ruled_out(identity, starts, 2000);
    TEST_ASSERT_EQUAL_STRING("", identity.certificate);

    // The root goes only when it starts later as well.
    starts.root_not_before = 3000;
    tls_drop_ruled_out(identity, starts, 2000);
    TEST_ASSERT_EQUAL_STRING("", identity.root_certificate);
}

static void test_a_certificate_starting_in_the_future_is_told() {
    TlsStarts starts{};
    // Made before this was kept: nothing is assumed.
    TEST_ASSERT_FALSE(tls_starts_after(starts, 2000000000));
    starts.root_not_before = 1000;
    starts.not_before = 2000;
    TEST_ASSERT_FALSE(tls_starts_after(starts, 2000));
    TEST_ASSERT_TRUE(tls_starts_after(starts, 1999));
    starts.not_before = 500;
    TEST_ASSERT_TRUE(tls_starts_after(starts, 999));
    TEST_ASSERT_FALSE(tls_starts_after(starts, 1000));
}

static void test_a_frame_that_failed_is_sent_again() {
    static FrameDither dither;
    dither.restart();
    std::array<RgbQ8, 2> targets{};
    targets.fill(RgbQ8{100 * 256, 0, 0});
    std::array<Rgb, 2> out{};
    TEST_ASSERT_TRUE(dither.step(targets, out));
    // A whole level needs no dithering, so the same frame reports no change.
    TEST_ASSERT_FALSE(dither.step(targets, out));
    dither.resend();
    TEST_ASSERT_TRUE(dither.step(targets, out));
}

static void test_failed_frames_are_told_seldom_and_the_output_restarted() {
    OutputHealth health;
    // The first failure is told at once, and alone it changes nothing.
    OutputHealth::Verdict verdict = health.on_frame(false, 1000);
    TEST_ASSERT_TRUE(verdict.report);
    TEST_ASSERT_FALSE(verdict.recreate);
    TEST_ASSERT_FALSE(health.failed());
    // A frame that goes out clears the count.
    verdict = health.on_frame(true, 1001);
    TEST_ASSERT_FALSE(verdict.report);
    for (uint32_t i = 0; i + 1 < kOutputFailedAfterFrames; ++i) {
        verdict = health.on_frame(false, 1002);
        TEST_ASSERT_FALSE(verdict.report);
        TEST_ASSERT_FALSE(verdict.recreate);
    }
    TEST_ASSERT_FALSE(health.failed());

    // Enough in a row is a failed output, restarted once for each wait.
    verdict = health.on_frame(false, 1003);
    TEST_ASSERT_TRUE(health.failed());
    TEST_ASSERT_TRUE(verdict.recreate);
    TEST_ASSERT_FALSE(health.on_frame(false, 1004 + kOutputRetryMs - 2).recreate);
    verdict = health.on_frame(false, 1003 + kOutputRetryMs);
    TEST_ASSERT_TRUE(verdict.recreate);

    // Across the wrap of the millisecond clock too, and well again with one good frame.
    TEST_ASSERT_TRUE(health.on_frame(false, 2000 + kOutputRetryMs * 3).recreate);
    TEST_ASSERT_FALSE(health.on_frame(true, 2001 + kOutputRetryMs * 3).recreate);
    TEST_ASSERT_FALSE(health.failed());

    OutputHealth near_wrap;
    near_wrap.on_frame(false, 0xFFFFFFFFu - 10);
    TEST_ASSERT_FALSE(near_wrap.on_frame(false, 0xFFFFFFFFu).report);
    TEST_ASSERT_TRUE(near_wrap.on_frame(false, kOutputRetryMs).report);
}

static void test_the_state_says_whether_frames_go_out() {
    State state(32);
    TEST_ASSERT_FALSE(state.snapshot().output_failed);
    state.set_output_failed(true);
    TEST_ASSERT_TRUE(state.snapshot().output_failed);
    state.set_output_failed(false);
    TEST_ASSERT_FALSE(state.snapshot().output_failed);
}

static void run_device_tests() {
    RUN_TEST(test_a_frame_that_failed_is_sent_again);
    RUN_TEST(test_failed_frames_are_told_seldom_and_the_output_restarted);
    RUN_TEST(test_the_state_says_whether_frames_go_out);
    RUN_TEST(test_next_action_is_the_first_one_due);
    RUN_TEST(test_next_action_respects_days_and_enabled);
    RUN_TEST(test_hostname_rules);
    RUN_TEST(test_settings_are_saved_only_once_they_have_stayed);
    RUN_TEST(test_brightness_and_speed_count_as_changes_too);
    RUN_TEST(test_turning_on_or_off_is_saved_at_once);
    RUN_TEST(test_a_refusal_does_not_change_the_owners_choice);
    RUN_TEST(test_remembered_values_are_checked_before_use);
    RUN_TEST(test_a_certificate_ruled_out_stays_so_until_replaced);
    RUN_TEST(test_a_certificate_starting_in_the_future_is_told);
    RUN_TEST(test_presence_readings_from_this_board);
    RUN_TEST(test_presence_check_keeps_the_estimate_only_while_present);
    RUN_TEST(test_scan_retries_before_saying_there_is_nothing);
    RUN_TEST(test_scan_list_is_one_entry_per_name_strongest_first);
    RUN_TEST(test_ota_header_reads_a_good_file);
    RUN_TEST(test_ota_header_takes_either_part_alone);
    RUN_TEST(test_ota_header_rejects_what_it_should);
    RUN_TEST(test_ota_percent);
    RUN_TEST(test_update_progress_shown);
}
