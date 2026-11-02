/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Host tests for the algorithms of the full catalog
 * Contributors: None
 *
 * Included by test_main.cpp, which provides the helpers and the renderer.
 */

#pragma once

// Every effect on every chain length, for a stretch of its life: nothing may draw outside
// its LEDs or divide by zero, and every effect shows something.
static void test_every_effect_runs_on_any_chain_length() {
    load_shipped_catalog();
    TEST_ASSERT_EQUAL_size_t(kShippedEffectCount, catalog_size());
    static std::array<Rgb, CSIGARLAND_MAX_LEDS> s_frame;
    for (size_t index = 0; index < catalog_size(); ++index) {
        const EffectSpec& spec = catalog_effect(index);
        for (const size_t length : {size_t{1}, size_t{2}, size_t{3}, size_t{7}, size_t{32}, size_t{CSIGARLAND_MAX_LEDS}}) {
            s_renderer.reset();
            const std::span<Rgb> frame(s_frame.data(), length);
            bool lit = false;
            for (uint32_t ms = 0; ms < 40000; ms += 97) {
                s_renderer.render(spec, ms, frame);
                lit = lit || std::ranges::any_of(frame, [](const Rgb& c) { return c.r != 0 || c.g != 0 || c.b != 0; });
            }
            TEST_ASSERT_TRUE_MESSAGE(lit, spec.id);
        }
    }
}

// Far into a long run, where a time product would overflow a narrower type.
static void test_every_effect_survives_a_long_uptime() {
    load_shipped_catalog();
    Frame frame{};
    for (size_t index = 0; index < catalog_size(); ++index) {
        s_renderer.reset();
        for (const uint32_t ms : {uint32_t{86400000}, uint32_t{2000000000}, uint32_t{4294967000}}) {
            s_renderer.render(catalog_effect(index), ms, frame);
        }
    }
}

static void test_bands_repeat_the_stops_and_stand_still_at_speed_zero() {
    const EffectSpec spec = parse_ok(R"({"fx":"bands","sx":0,"ix":32,"col":["FF0000","00FF00","0000FF"]})");  // Runs of 2.
    Frame frame{};
    s_renderer.render(spec, 5000, frame);
    TEST_ASSERT_EQUAL_UINT8(255, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(255, frame[1].r);
    TEST_ASSERT_EQUAL_UINT8(255, frame[2].g);
    TEST_ASSERT_EQUAL_UINT8(255, frame[4].b);
    TEST_ASSERT_EQUAL_UINT8(255, frame[6].r);
    const Frame still = frame;
    s_renderer.render(spec, 60000, frame);
    TEST_ASSERT_TRUE(frame == still);
}

static void test_bands_move_with_speed() {
    const EffectSpec spec = parse_ok(R"({"fx":"bands","ix":0,"col":["FF0000","00FF00"]})");
    Frame frame{};
    s_renderer.render(spec, 0, frame);
    TEST_ASSERT_EQUAL_UINT8(255, frame[0].r);
    s_renderer.render(spec, 150, frame);
    TEST_ASSERT_EQUAL_UINT8(255, frame[0].g);
    TEST_ASSERT_EQUAL_UINT8(255, frame[1].r);
}

static void test_sweep_comes_back_from_the_far_end() {
    const EffectSpec spec = parse_ok(R"({"fx":"sweep","ix":0,"col":["FF0000","0000FF"]})");
    Frame frame{};
    s_renderer.render(spec, 600, frame);  // 60 ms per LED: head on LED 10, from the first.
    TEST_ASSERT_EQUAL_UINT8(255, frame[5].r);
    TEST_ASSERT_EQUAL_UINT8(255, frame[20].b);
    s_renderer.render(spec, 32 * 60 + 600, frame);  // The second pass starts at the last.
    TEST_ASSERT_EQUAL_UINT8(255, frame[31].b);
    TEST_ASSERT_EQUAL_UINT8(255, frame[5].r);
}

static void test_percent_fills_the_share_of_the_chain() {
    const EffectSpec half = parse_ok(R"({"fx":"percent","ix":128,"col":["00FF00","100000"]})");
    Frame frame{};
    s_renderer.render(half, 0, frame);
    TEST_ASSERT_EQUAL_UINT8(255, frame[0].g);
    TEST_ASSERT_EQUAL_UINT8(255, frame[15].g);
    TEST_ASSERT_EQUAL_UINT8(0, frame[17].g);
    TEST_ASSERT_EQUAL_UINT8(0x10, frame[17].r);
    s_renderer.render(parse_ok(R"({"fx":"percent","ix":255,"col":["00FF00"]})"), 0, frame);
    TEST_ASSERT_EQUAL_UINT8(255, frame[31].g);
    s_renderer.render(parse_ok(R"({"fx":"percent","ix":0,"col":["00FF00"]})"), 0, frame);
    TEST_ASSERT_EQUAL_INT(0, lit_count(frame));
}

static void test_traffic_light_shows_one_phase_at_a_time() {
    const EffectSpec spec = parse_ok(R"({"fx":"traffic-light","ix":0,"col":["FF0000","FFA000","00C000"]})");
    Frame frame{};
    s_renderer.render(spec, 1000, frame);  // Stop.
    TEST_ASSERT_EQUAL_UINT8(0xFF, frame[0].r);
    TEST_ASSERT_EQUAL_INT(0, lit_count(Frame{frame[1], frame[2]}));
    s_renderer.render(spec, 2700, frame);  // Stop and ready together.
    TEST_ASSERT_EQUAL_UINT8(0xFF, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(0xFF, frame[1].r);
    s_renderer.render(spec, 4000, frame);  // Go.
    TEST_ASSERT_EQUAL_UINT8(0, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(0xC0, frame[2].g);
    s_renderer.render(spec, 5700, frame);  // Ready alone.
    TEST_ASSERT_EQUAL_UINT8(0, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(0xFF, frame[1].r);
    TEST_ASSERT_EQUAL_UINT8(0, frame[2].g);
}

static void test_blink_keeps_its_duty() {
    const EffectSpec spec = parse_ok(R"({"fx":"blink","ix":128,"col":["FFFFFF"]})");
    Frame frame{};
    s_renderer.render(spec, 100, frame);
    TEST_ASSERT_EQUAL_INT(32, lit_count(frame));
    s_renderer.render(spec, 700, frame);
    TEST_ASSERT_EQUAL_INT(0, lit_count(frame));
}

static void test_strobe_burst_rattles_and_rests() {
    const EffectSpec spec = parse_ok(R"({"fx":"strobe-burst","ix":0,"col":["FFFFFF"]})");  // Three flashes.
    Frame frame{};
    int flashes = 0;
    bool was_lit = false;
    for (uint32_t ms = 0; ms < 1070; ms += 5) {
        s_renderer.render(spec, ms, frame);
        const bool lit = lit_count(frame) > 0;
        flashes += lit && !was_lit ? 1 : 0;
        was_lit = lit;
    }
    TEST_ASSERT_EQUAL_INT(3, flashes);
}

static void test_alternate_flashes_one_side_at_a_time() {
    const EffectSpec spec = parse_ok(R"({"fx":"alternate","ix":0,"col":["FF0000","0000FF"]})");
    Frame frame{};
    s_renderer.render(spec, 0, frame);
    TEST_ASSERT_EQUAL_UINT8(255, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(0, frame[31].b);
    s_renderer.render(spec, 110, frame);
    TEST_ASSERT_EQUAL_INT(0, lit_count(frame));
    s_renderer.render(spec, 440, frame);
    TEST_ASSERT_EQUAL_UINT8(0, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(255, frame[31].b);
}

static void test_tetrix_fills_the_chain_and_clears_it() {
    const EffectSpec spec = parse_ok(R"({"fx":"tetrix","ix":0,"col":["FFFFFF"]})");  // Blocks of one.
    Frame frame{};
    s_renderer.render(spec, 10 * 500, frame);
    TEST_ASSERT_EQUAL_UINT8(255, frame[9].r);
    s_renderer.render(spec, 32 * 500 - 1, frame);
    TEST_ASSERT_EQUAL_UINT8(255, frame[30].r);
    s_renderer.render(spec, 32 * 500 + 999, frame);
    TEST_ASSERT_LESS_THAN_INT(32, std::ranges::count_if(frame, [](const Rgb& c) { return c.r > 200; }));
    s_renderer.render(spec, 32 * 500 + 1000, frame);
    TEST_ASSERT_LESS_OR_EQUAL_INT(1, lit_count(frame));
}

static void test_sinelon_leaves_a_trail_that_fades() {
    const EffectSpec spec = parse_ok(R"({"fx":"sinelon","ix":128,"col":["FFFFFF"]})");
    Frame frame{};
    int most_lit = 0;
    for (uint32_t ms = 0; ms < 8000; ms += 16) {
        s_renderer.render(spec, ms, frame);
        most_lit = std::max(most_lit, lit_count(frame));
    }
    TEST_ASSERT_GREATER_THAN_INT(2, most_lit);
    TEST_ASSERT_LESS_THAN_INT(32, most_lit);
}

static void test_noise_changes_smoothly() {
    int biggest_step_in_time = 0;
    int biggest_step_along = 0;
    for (uint32_t t = 0; t < 4096; ++t) {
        biggest_step_in_time = std::max(biggest_step_in_time, std::abs(noise_field(100, t + 1) - noise_field(100, t)));
        biggest_step_along = std::max(biggest_step_along, std::abs(noise_field(t + 1, 300) - noise_field(t, 300)));
    }
    TEST_ASSERT_LESS_THAN_INT(8, biggest_step_in_time);
    TEST_ASSERT_LESS_THAN_INT(8, biggest_step_along);
}
