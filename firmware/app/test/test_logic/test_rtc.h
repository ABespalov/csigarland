/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Host tests for the DS3231 registers and clock rules
 * Contributors: None
 *
 * Included by test_main.cpp.
 */

#pragma once

static void test_rtc_registers_of_a_known_moment() {
    // 2026-10-31 18:05:33 UTC, a Saturday.
    constexpr int64_t kMoment = 1793469933;
    uint8_t registers[kRtcTimeRegisters];
    rtc_encode(kMoment, registers);
    const uint8_t expected[kRtcTimeRegisters] = {0x33, 0x05, 0x18, 7, 0x31, 0x10, 0x26};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, registers, kRtcTimeRegisters);
    int64_t back = 0;
    TEST_ASSERT_TRUE(rtc_decode(registers, back));
    TEST_ASSERT_TRUE(back == kMoment);
}

static void test_rtc_round_trip_across_years() {
    // Every 3 days and a bit, through leap years and to the part's last year.
    for (int64_t epoch = kRtcEarliestEpoch; epoch < kRtcLatestEpoch; epoch += 263000) {
        uint8_t registers[kRtcTimeRegisters];
        rtc_encode(epoch, registers);
        int64_t back = 0;
        TEST_ASSERT_TRUE(rtc_decode(registers, back));
        TEST_ASSERT_TRUE(back == epoch);
    }
    // Leap day.
    const uint8_t leap[kRtcTimeRegisters] = {0, 0, 0, 3, 0x29, 0x02, 0x28};
    int64_t epoch = 0;
    TEST_ASSERT_TRUE(rtc_decode(leap, epoch));
    TEST_ASSERT_TRUE(epoch == 1835395200);
}

static void test_rtc_reads_twelve_hour_mode() {
    const uint8_t afternoon[kRtcTimeRegisters] = {0, 0, 0x40 | 0x20 | 0x06, 7, 0x31, 0x10, 0x26};  // 6 PM.
    const uint8_t midnight[kRtcTimeRegisters] = {0, 0, 0x40 | 0x12, 7, 0x31, 0x10, 0x26};          // 12 AM.
    int64_t epoch = 0;
    TEST_ASSERT_TRUE(rtc_decode(afternoon, epoch));
    TEST_ASSERT_EQUAL_INT(18 * 3600, static_cast<int>(epoch % 86400));
    TEST_ASSERT_TRUE(rtc_decode(midnight, epoch));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(epoch % 86400));
}

static void test_rtc_refuses_registers_that_are_no_time() {
    int64_t epoch = 0;
    const uint8_t bad_seconds[kRtcTimeRegisters] = {0x6A, 0, 0, 1, 1, 1, 0x26};
    const uint8_t bad_month[kRtcTimeRegisters] = {0, 0, 0, 1, 1, 0x13, 0x26};
    const uint8_t zero_month[kRtcTimeRegisters] = {0, 0, 0, 1, 1, 0, 0x26};
    const uint8_t bad_day[kRtcTimeRegisters] = {0, 0, 0, 1, 0x30, 0x02, 0x26};
    const uint8_t bad_hour[kRtcTimeRegisters] = {0, 0, 0x24, 1, 1, 1, 0x26};
    TEST_ASSERT_FALSE(rtc_decode(bad_seconds, epoch));
    TEST_ASSERT_FALSE(rtc_decode(bad_month, epoch));
    TEST_ASSERT_FALSE(rtc_decode(zero_month, epoch));
    TEST_ASSERT_FALSE(rtc_decode(bad_day, epoch));
    TEST_ASSERT_FALSE(rtc_decode(bad_hour, epoch));
}

static void test_rtc_is_trusted_only_when_it_has_kept_time() {
    constexpr int64_t kGood = 1793469933;
    TEST_ASSERT_TRUE(rtc_is_trusted(false, kGood));
    // A good-looking date from a part that says it stopped.
    TEST_ASSERT_FALSE(rtc_is_trusted(true, kGood));
    // Fresh from the factory, or reset with its battery.
    TEST_ASSERT_FALSE(rtc_is_trusted(false, 946684800));
    TEST_ASSERT_FALSE(rtc_is_trusted(false, kRtcEarliestEpoch - 1));
    TEST_ASSERT_FALSE(rtc_is_trusted(false, kRtcLatestEpoch));
}

static void test_rtc_is_corrected_only_when_it_drifted_or_lost_time() {
    TEST_ASSERT_FALSE(rtc_needs_correction(true, 1000, 1001));
    TEST_ASSERT_TRUE(rtc_needs_correction(true, 1000, 1002));
    TEST_ASSERT_TRUE(rtc_needs_correction(true, 1002, 1000));
    TEST_ASSERT_TRUE(rtc_needs_correction(false, 1000, 1000));
}

static void test_rtc_local_time_field() {
    LocalDateTime parsed;
    TEST_ASSERT_TRUE(rtc_parse_local("2026-10-31T18:05", parsed));
    TEST_ASSERT_EQUAL_INT(2026, parsed.year);
    TEST_ASSERT_EQUAL_INT(5, parsed.minute);
    TEST_ASSERT_EQUAL_INT(0, parsed.second);
    TEST_ASSERT_TRUE(rtc_parse_local("2028-02-29T00:00:59", parsed));
    TEST_ASSERT_EQUAL_INT(59, parsed.second);
    TEST_ASSERT_FALSE(rtc_parse_local("2026-02-29T00:00", parsed));
    TEST_ASSERT_FALSE(rtc_parse_local("2026-13-01T00:00", parsed));
    TEST_ASSERT_FALSE(rtc_parse_local("2026-10-31T24:00", parsed));
    TEST_ASSERT_FALSE(rtc_parse_local("2026-10-31T18:05x", parsed));
    TEST_ASSERT_FALSE(rtc_parse_local("2026-10-31", parsed));
    TEST_ASSERT_FALSE(rtc_parse_local("", parsed));
}

static void run_rtc_tests() {
    RUN_TEST(test_rtc_registers_of_a_known_moment);
    RUN_TEST(test_rtc_round_trip_across_years);
    RUN_TEST(test_rtc_reads_twelve_hour_mode);
    RUN_TEST(test_rtc_refuses_registers_that_are_no_time);
    RUN_TEST(test_rtc_is_trusted_only_when_it_has_kept_time);
    RUN_TEST(test_rtc_is_corrected_only_when_it_drifted_or_lost_time);
    RUN_TEST(test_rtc_local_time_field);
}
