/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Host tests for the LEDs' power budget: its ranges, the limit worked out from it, and its word
 * Contributors: None
 *
 * Included by test_main.cpp, after the limiter's test.
 */

#pragma once

static void test_power_defaults_keep_the_old_limit() {
    const PowerSettings power;
    TEST_ASSERT_TRUE(power_settings_are_valid(power));
    // 2000 mA less the board's 250 mA, and 20 % of what is left.
    TEST_ASSERT_EQUAL_UINT32(1400, power_limit_ma(power));
    TEST_ASSERT_EQUAL_UINT32(41, led_full_ma(power));
    TEST_ASSERT_EQUAL_UINT32(1312, peak_ma(power, 32));
    TEST_ASSERT_EQUAL_UINT32(34, undimmed_leds(power));
}

static void test_power_limit_follows_the_settings() {
    PowerSettings power;
    // The margin is a share of what the supply gives beyond the board: 95 % of 1750 mA.
    power.margin_percent = 5;
    TEST_ASSERT_EQUAL_UINT32(1662, power_limit_ma(power));
    power.supply_ma = 5000;
    power.margin_percent = 10;
    TEST_ASSERT_EQUAL_UINT32(4275, power_limit_ma(power));
    // As measured on the owner's garland: 36 mA lit, under 1 mA idle.
    power.white_ma = 36;
    TEST_ASSERT_EQUAL_UINT32(115, undimmed_leds(power));
    // The smallest supply with the largest margin still leaves the LEDs some current.
    const PowerSettings small{500, 40, 1, 20};
    TEST_ASSERT_EQUAL_UINT32(200, power_limit_ma(small));
    TEST_ASSERT_EQUAL_UINT32(4, undimmed_leds(small));
}

static void test_power_ranges() {
    TEST_ASSERT_FALSE(supply_ma_is_valid(499));
    TEST_ASSERT_TRUE(supply_ma_is_valid(500));
    TEST_ASSERT_TRUE(supply_ma_is_valid(20000));
    TEST_ASSERT_FALSE(supply_ma_is_valid(20001));
    TEST_ASSERT_FALSE(led_white_ma_is_valid(4));
    TEST_ASSERT_TRUE(led_white_ma_is_valid(100));
    TEST_ASSERT_FALSE(led_white_ma_is_valid(101));
    TEST_ASSERT_TRUE(led_idle_ma_is_valid(0));
    TEST_ASSERT_FALSE(led_idle_ma_is_valid(6));
    TEST_ASSERT_FALSE(led_idle_ma_is_valid(-1));
    TEST_ASSERT_FALSE(margin_percent_is_valid(4));
    TEST_ASSERT_TRUE(margin_percent_is_valid(5));
    TEST_ASSERT_TRUE(margin_percent_is_valid(20));
    TEST_ASSERT_FALSE(margin_percent_is_valid(21));
}

static void test_power_settings_go_whole_in_one_word() {
    const PowerSettings largest{static_cast<uint16_t>(kSupplyMaxMa), kLedWhiteMaxMa, kLedIdleMaxMa, kMarginMaxPercent};
    TEST_ASSERT_TRUE(unpack_power(pack_power(largest)) == largest);
    const PowerSettings smallest{static_cast<uint16_t>(kSupplyMinMa), kLedWhiteMinMa, 0, kMarginMinPercent};
    TEST_ASSERT_TRUE(unpack_power(pack_power(smallest)) == smallest);
    TEST_ASSERT_TRUE(unpack_power(pack_power(PowerSettings{})) == PowerSettings{});
}

static void test_state_holds_the_power_budget() {
    State state(32);
    TEST_ASSERT_TRUE(state.power() == PowerSettings{});
    const PowerSettings measured{2000, 36, 1, 10};
    state.set_power(measured);
    TEST_ASSERT_TRUE(state.power() == measured);
    TEST_ASSERT_EQUAL_UINT32(1575, current_model(state.power()).limit_ma);
}

static void run_power_tests() {
    RUN_TEST(test_power_defaults_keep_the_old_limit);
    RUN_TEST(test_power_limit_follows_the_settings);
    RUN_TEST(test_power_ranges);
    RUN_TEST(test_power_settings_go_whole_in_one_word);
    RUN_TEST(test_state_holds_the_power_budget);
}
