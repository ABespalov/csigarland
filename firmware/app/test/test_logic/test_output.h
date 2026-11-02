/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Host tests for LED brightness and temporal dithering
 * Contributors: None
 *
 * Included by test_main.cpp, which provides Unity and the garland namespace.
 */

#pragma once

static void test_brightness_keeps_fractions_of_a_step() {
    Frame frame{};
    std::ranges::fill(frame, Rgb{200, 100, 40});
    std::array<RgbQ8, 32> out{};
    apply_brightness(frame, kFullBrightnessGain, out);
    TEST_ASSERT_TRUE((out[0] == RgbQ8{200 * 256, 100 * 256, 40 * 256}));
    // A gain that an 8-bit result could not hold: 200 at a hundredth is 2.0 and 40 is 0.4.
    apply_brightness(frame, kFullBrightnessGain / 100, out);
    TEST_ASSERT_UINT16_WITHIN(1, 512, out[0].r);
    TEST_ASSERT_UINT16_WITHIN(1, 102, out[0].b);
    // Under a quarter of a step: dark, not a twinkle.
    const std::array<Rgb, 1> faint{{{1, 1, 1}}};
    std::array<RgbQ8, 1> faint_out{};
    apply_brightness(faint, kFullBrightnessGain / 100, faint_out);
    TEST_ASSERT_EQUAL_UINT16(0, faint_out[0].r);
}

static void test_brightness_percent_is_perceptual() {
    TEST_ASSERT_EQUAL_UINT32(0, brightness_gain(0));
    TEST_ASSERT_EQUAL_UINT32(kFullBrightnessGain, brightness_gain(100));
    // 1 % keeps a full channel on one whole step.
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(256, (255 * brightness_gain(1)) >> 8);
    uint32_t previous = 0;
    for (int percent = 1; percent <= 100; ++percent) {
        TEST_ASSERT_GREATER_THAN_UINT32(previous, brightness_gain(static_cast<uint8_t>(percent)));
        previous = brightness_gain(static_cast<uint8_t>(percent));
    }
    TEST_ASSERT_UINT32_WITHIN(1500, 14400, brightness_gain(50));  // About 22 % of the light.
}

static void test_dither_averages_to_the_target() {
    static FrameDither dither;
    dither.restart();
    const std::array<RgbQ8, 3> targets{{{128, 300, 65280}, {640, 0, 10000}, {256, 1, 32768}}};
    std::array<uint32_t, 9> totals{};
    std::array<Rgb, 3> out{};
    constexpr uint32_t kFrames = 2560;
    for (uint32_t frame = 0; frame < kFrames; ++frame) {
        dither.step(targets, out);
        for (size_t i = 0; i < out.size(); ++i) {
            totals[i * 3] += out[i].r;
            totals[i * 3 + 1] += out[i].g;
            totals[i * 3 + 2] += out[i].b;
            // Only ever the two steps either side of the target.
            TEST_ASSERT_LESS_OR_EQUAL_UINT8(targets[i].r / 256 + 1, out[i].r);
            TEST_ASSERT_GREATER_OR_EQUAL_UINT8(targets[i].r / 256, out[i].r);
        }
    }
    for (size_t i = 0; i < out.size(); ++i) {
        TEST_ASSERT_UINT32_WITHIN(1, targets[i].r * kFrames / 256, totals[i * 3]);
        TEST_ASSERT_UINT32_WITHIN(1, targets[i].g * kFrames / 256, totals[i * 3 + 1]);
        TEST_ASSERT_UINT32_WITHIN(1, targets[i].b * kFrames / 256, totals[i * 3 + 2]);
    }
}

static void test_dither_sends_nothing_for_whole_steps() {
    static FrameDither dither;
    dither.restart();
    const std::array<RgbQ8, 2> targets{{{256, 512, 65280}, {0, 25600, 0}}};
    std::array<Rgb, 2> out{};
    TEST_ASSERT_TRUE(dither.step(targets, out));
    TEST_ASSERT_TRUE((out[0] == Rgb{1, 2, 255}));
    TEST_ASSERT_FALSE(dither.step(targets, out));
    dither.resend();
    TEST_ASSERT_TRUE(dither.step(targets, out));
    TEST_ASSERT_FALSE(dither.step(targets, out));
}

static void test_dither_spreads_leds_of_one_level() {
    static FrameDither dither;
    dither.restart();
    std::array<RgbQ8, 32> targets{};
    std::ranges::fill(targets, RgbQ8{128, 128, 128});  // Half a step: on every other frame.
    std::array<Rgb, 32> out{};
    dither.step(targets, out);
    const auto lit = std::ranges::count_if(out, [](const Rgb& led) { return led.r != 0; });
    TEST_ASSERT_TRUE(lit > 0 && lit < 32);
}
