/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Host tests for what the Lab build measures and how it writes it
 * Contributors: None
 *
 * Included by test_main.cpp.
 */

#pragma once

static void test_lab_is_off_outside_its_build() {
    // The host tests build as every build but esp32c6-lab does.
    TEST_ASSERT_FALSE(kLabBuild);
}

static void test_lab_rtc_temperature() {
    TEST_ASSERT_EQUAL_INT32(2500, rtc_temperature_centi(0x19, 0x00));
    TEST_ASSERT_EQUAL_INT32(2575, rtc_temperature_centi(0x19, 0xC0));
    TEST_ASSERT_EQUAL_INT32(-25, rtc_temperature_centi(0xFF, 0xC0));
    TEST_ASSERT_EQUAL_INT32(-1800, rtc_temperature_centi(0xEE, 0x00));
    // The low six bits are not part of the reading.
    TEST_ASSERT_EQUAL_INT32(2525, rtc_temperature_centi(0x19, 0x7F));
}

static void test_lab_range_and_centi() {
    LabRange range;
    TEST_ASSERT_FALSE(range.seen);
    range.note(3100);
    range.note(2950);
    range.note(3300);
    TEST_ASSERT_EQUAL_INT32(2950, range.low);
    TEST_ASSERT_EQUAL_INT32(3300, range.high);
    char text[13];
    lab_format_centi(4120, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("41.20", text);
    lab_format_centi(-25, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("-0.25", text);
    lab_format_centi(5, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("0.05", text);
}

static void test_lab_health_line() {
    LabHealth health;
    health.up_s = 3600;
    health.heap = 123456;
    health.heap_low = 98765;
    health.chip_read = true;
    health.chip_centi = 4120;
    health.chip.note(3910);
    health.chip.note(4350);
    health.box_read = true;
    health.box_centi = 3125;
    health.box.note(3000);
    health.box.note(3300);
    health.model_ma = 1012;
    health.model_top_ma = 1312;
    health.rail_on = true;
    health.leds = 32;
    char line[200];
    lab_format_health(health, line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("up 3600 s, heap 123456 (low 98765), chip 41.20 C (39.10..43.50), box 31.25 C (30.00..33.00), "
                             "model 1012 mA (top 1312), rail on, 32 LEDs",
                             line);
    health.box_read = false;
    health.rail_on = false;
    lab_format_health(health, line, sizeof(line));
    TEST_ASSERT_NOT_NULL(std::strstr(line, "box -, "));
    TEST_ASSERT_NOT_NULL(std::strstr(line, "rail off"));
    // The longest it can be on this board still fits the kept log's line with ESP-IDF's prefix: the most
    // heap the chip has, the DS3231's coldest reading, 256 LEDs at 105 mA under a model of five digits.
    health.up_s = 4294967295U;
    health.heap = health.heap_low = 524288;
    health.box_read = true;
    health.chip_centi = health.box_centi = -12800;
    health.chip.low = health.chip.high = health.box.low = health.box.high = -12800;
    health.model_ma = health.model_top_ma = 99999;
    health.leds = 256;
    TEST_ASSERT_TRUE(lab_format_health(health, line, sizeof(line)) < 170);
}

static void test_lab_tls_line() {
    LabTls tls;
    tls.live = 5120;
    tls.peak = 46080;
    tls.largest_ask = 16749;
    tls.allocs = 812;
    tls.heap_block = 23456;
    tls.pool_size = 16384;
    tls.pool_peak = 11800;
    tls.pool_largest = 9728;
    char line[200];
    lab_format_tls(tls, line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("tls: live 5120, peak 46080, ask 16749, 812 allocs, 0 failed; pool 11800/16384 peak, 9728 block, slot 0 "
                             "missed, 0 spilled; heap 23456",
                             line);
    tls.failed = 1;
    tls.failed_ask = 16749;
    tls.big_seen = true;
    tls.tightest_block = 15872;
    tls.tightest_ask = 16749;
    tls.slot_misses = 2;
    tls.spilled = 3;
    lab_format_tls(tls, line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("tls: live 5120, peak 46080, ask 16749, 812 allocs, 1 failed (16749); pool 11800/16384 peak, 9728 block, "
                             "slot 2 missed, 3 spilled; heap 23456, 15872 before a 16749",
                             line);
    // Long figures for this board fit the kept log's line with ESP-IDF's prefix: no block or ask past 64 KB, as the
    // internal heap holds no more free, and counts of a long run.
    tls.live = tls.peak = tls.heap_block = tls.tightest_block = 65535;
    tls.largest_ask = tls.failed_ask = tls.tightest_ask = 65535;
    tls.allocs = 9999999;
    tls.failed = 999;
    tls.pool_size = tls.pool_peak = tls.pool_largest = 16384;
    tls.slot_misses = tls.spilled = 9999;
    TEST_ASSERT_TRUE(lab_format_tls(tls, line, sizeof(line)) < 180);
}

static void run_lab_tests() {
    RUN_TEST(test_lab_is_off_outside_its_build);
    RUN_TEST(test_lab_rtc_temperature);
    RUN_TEST(test_lab_range_and_centi);
    RUN_TEST(test_lab_health_line);
    RUN_TEST(test_lab_tls_line);
}
