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

static void test_the_rail_waits_for_settings_known() {
    // A start begins with the defaults, not known: a reading that failed is no leave for their 1400 mA, as the
    // owner may have saved a supply of 500 mA. Known once read, or found to be none, or saved.
    State state(32);
    TEST_ASSERT_FALSE(state.power_now().known);
    TEST_ASSERT_FALSE(rail_may_be_on(state.power_now().known, state.power_now().settings, 32));
    state.set_power(PowerSettings{});
    TEST_ASSERT_TRUE(state.power_now().known);
    TEST_ASSERT_TRUE(state.power_now().settings == PowerSettings{});
    TEST_ASSERT_TRUE(rail_may_be_on(true, state.power_now().settings, 32));
    const PowerSettings small{500, 40, 1, 20};
    state.set_power(small);
    TEST_ASSERT_TRUE(state.power_now().known && state.power_now().settings == small);
}

static void test_the_rail_stays_off_when_idle_draw_alone_is_over_the_limit() {
    // 500 mA, 20 %: 200 mA. A dark LED's draw no brightness lowers: 200 of them at 1 mA are within it, 201 not,
    // and at 5 mA, 40 and 41.
    const PowerSettings small{500, 40, 1, 20};
    TEST_ASSERT_TRUE(rail_may_be_on(true, small, 200));
    TEST_ASSERT_FALSE(rail_may_be_on(true, small, 201));
    const PowerSettings hungry{500, 40, 5, 20};
    TEST_ASSERT_TRUE(rail_may_be_on(true, hungry, 40));
    TEST_ASSERT_FALSE(rail_may_be_on(true, hungry, 41));
    // The longest chain at the defaults, and an LED that draws nothing dark.
    TEST_ASSERT_TRUE(rail_may_be_on(true, PowerSettings{}, CSIGARLAND_MAX_LEDS));
    TEST_ASSERT_TRUE(rail_may_be_on(true, PowerSettings{500, 40, 0, 20}, CSIGARLAND_MAX_LEDS));
}

static void test_a_power_record_is_taken_whole_or_not_at_all() {
    const PowerSettings saved{2200, 40, 1, 20};
    TEST_ASSERT_TRUE(power_word_is_valid(pack_power(saved)));
    // A word with bits above the settings, or one whose settings are out of their ranges, is no record.
    TEST_ASSERT_FALSE(power_word_is_valid(pack_power(saved) | (1U << 30)));
    TEST_ASSERT_FALSE(power_word_is_valid(0));
    PowerSettings wide = saved;
    wide.margin_percent = 4;
    TEST_ASSERT_FALSE(power_word_is_valid(pack_power(wide)));
}

static void run_power_tests() {
    RUN_TEST(test_power_defaults_keep_the_old_limit);
    RUN_TEST(test_power_limit_follows_the_settings);
    RUN_TEST(test_power_ranges);
    RUN_TEST(test_power_settings_go_whole_in_one_word);
    RUN_TEST(test_state_holds_the_power_budget);
    RUN_TEST(test_the_rail_waits_for_settings_known);
    RUN_TEST(test_the_rail_stays_off_when_idle_draw_alone_is_over_the_limit);
    RUN_TEST(test_a_power_record_is_taken_whole_or_not_at_all);
}
