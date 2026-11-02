/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Host tests for the second group of animation algorithms
 * Contributors: None
 *
 * Included by test_main.cpp, which provides the helpers and the renderer.
 */

#pragma once

static void test_breathe_never_goes_below_ix() {
    const EffectSpec spec = parse_ok(R"({"fx":"breathe","ix":40,"col":["FFFFFF"]})");
    Frame frame{};
    int lowest = 255;
    int highest = 0;
    for (uint32_t ms = 0; ms < 5000; ms += 20) {
        s_renderer.render(spec, ms, frame);
        lowest = std::min<int>(lowest, frame[5].r);
        highest = std::max<int>(highest, frame[5].r);
    }
    TEST_ASSERT_INT_WITHIN(2, 40, lowest);
    TEST_ASSERT_GREATER_THAN_INT(240, highest);
}

static void test_wipe_spreads_then_takes_the_next_color() {
    const EffectSpec spec = parse_ok(R"({"fx":"wipe","ix":0,"col":["FF0000","0000FF"]})");
    Frame frame{};
    s_renderer.render(spec, 800, frame);  // 80 ms per LED: the head is on LED 10.
    TEST_ASSERT_EQUAL_UINT8(255, frame[9].r);
    TEST_ASSERT_EQUAL_UINT8(255, frame[11].b);
    s_renderer.render(spec, 2559, frame);
    TEST_ASSERT_EQUAL_UINT8(0, frame[0].b);
    s_renderer.render(spec, 2560 + 800, frame);  // The second pass: blue over red.
    TEST_ASSERT_EQUAL_UINT8(255, frame[9].b);
    TEST_ASSERT_EQUAL_UINT8(255, frame[11].r);
}

static void test_wipe_alternates_a_lone_color_with_dark() {
    const EffectSpec spec = parse_ok(R"({"fx":"wipe","ix":0,"col":["FFFFFF"]})");
    Frame frame{};
    s_renderer.render(spec, 2559, frame);
    TEST_ASSERT_GREATER_THAN_INT(30, lit_count(frame));
    s_renderer.render(spec, 2560 + 2559, frame);
    TEST_ASSERT_LESS_THAN_INT(2, lit_count(frame));
}

static void test_theater_steps_along_the_chain() {
    const EffectSpec spec = parse_ok(R"({"fx":"theater","ix":64,"col":["FFFFFF","000010"]})");  // Every third.
    Frame frame{};
    s_renderer.render(spec, 0, frame);
    TEST_ASSERT_EQUAL_INT(11, std::ranges::count_if(frame, [](const Rgb& led) { return led.r != 0; }));
    TEST_ASSERT_EQUAL_UINT8(255, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(0, frame[1].r);
    TEST_ASSERT_EQUAL_UINT8(0x10, frame[1].b);
    s_renderer.render(spec, 180, frame);
    TEST_ASSERT_EQUAL_UINT8(0, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(255, frame[1].r);
}

static void test_meteor_tail_leaves_the_chain() {
    const EffectSpec spec = parse_ok(R"({"fx":"meteor","ix":32,"col":["FFFFFF"]})");  // A tail of 4 LEDs.
    Frame frame{};
    s_renderer.render(spec, 600, frame);  // 60 ms per LED: the head is on LED 10.
    TEST_ASSERT_EQUAL_UINT8(255, frame[10].r);
    TEST_ASSERT_LESS_THAN_UINT8(frame[10].r, frame[9].r);
    TEST_ASSERT_NOT_EQUAL(0, frame[9].r);
    TEST_ASSERT_EQUAL_UINT8(0, frame[11].r);
    TEST_ASSERT_EQUAL_UINT8(0, frame[6].r);
    s_renderer.render(spec, 60 * 34, frame);  // Past the end, the last of the tail remains.
    TEST_ASSERT_NOT_EQUAL(0, frame[31].r);
    TEST_ASSERT_EQUAL_INT(1, lit_count(frame));
    s_renderer.render(spec, 60 * 36, frame);  // Started over.
    TEST_ASSERT_EQUAL_UINT8(255, frame[0].r);
    TEST_ASSERT_EQUAL_INT(1, lit_count(frame));
}

static void test_scanner_turns_at_the_ends() {
    const EffectSpec spec = parse_ok(R"({"fx":"scanner","ix":32,"col":["FFFFFF"]})");  // A tail of 4 steps.
    Frame frame{};
    s_renderer.render(spec, 70 * 10, frame);
    TEST_ASSERT_EQUAL_UINT8(255, frame[10].r);
    TEST_ASSERT_LESS_THAN_UINT8(frame[10].r, frame[9].r);
    TEST_ASSERT_EQUAL_UINT8(0, frame[11].r);
    s_renderer.render(spec, 70 * 33, frame);  // The light went past LED 31 and is on LED 29.
    TEST_ASSERT_EQUAL_UINT8(255, frame[29].r);
    TEST_ASSERT_LESS_THAN_UINT8(frame[29].r, frame[30].r);
    TEST_ASSERT_EQUAL_UINT8(0, frame[28].r);
    s_renderer.render(spec, 70 * 62, frame);  // Back at the first LED: the cycle is 62 steps.
    TEST_ASSERT_EQUAL_UINT8(255, frame[0].r);

    std::array<Rgb, 1> one{};
    s_renderer.render(spec, 500, one);
    TEST_ASSERT_EQUAL_UINT8(255, one[0].r);
}

static void test_colorloop_moves_the_whole_chain_together() {
    const EffectSpec spec = parse_ok(R"({"fx":"colorloop","ix":0,"pal":"Rainbow"})");
    Frame frame{};
    s_renderer.render(spec, 0, frame);
    const Rgb first = frame[0];
    TEST_ASSERT_TRUE(std::ranges::all_of(frame, [&](const Rgb& led) { return led == first; }));
    s_renderer.render(spec, 8000, frame);
    TEST_ASSERT_FALSE(frame[0] == first);
    TEST_ASSERT_TRUE(std::ranges::all_of(frame, [&](const Rgb& led) { return led == frame[0]; }));
}

static void test_drift_changes_color_gradually() {
    const EffectSpec spec = parse_ok(R"({"fx":"drift","ix":255,"pal":[0,"000000",128,"FF0000",255,"000000"]})");
    s_renderer.reset();
    Frame frame{};
    Frame previous{};
    s_renderer.render(spec, 0, previous);
    const Frame start = previous;
    bool changed = false;
    for (uint32_t ms = 16; ms < 6000; ms += 16) {
        s_renderer.render(spec, ms, frame);
        for (size_t i = 0; i < frame.size(); ++i) {
            // Half the palette spans 255 levels in 128 places: at most about 2 per step.
            TEST_ASSERT_INT_WITHIN(4, previous[i].r, frame[i].r);
            changed = changed || frame[i].r != start[i].r;
        }
        previous = frame;
    }
    TEST_ASSERT_TRUE(changed);
}

static void test_fireworks_burst_and_die_out() {
    const EffectSpec spec = parse_ok(R"({"fx":"fireworks","ix":0,"pal":"Rainbow"})");  // One burst at a time.
    Frame frame{};
    int most_lit = 0;
    int dark_frames = 0;
    for (uint32_t ms = 0; ms < 12000; ms += 16) {
        s_renderer.render(spec, ms, frame);
        most_lit = std::max(most_lit, lit_count(frame));
        dark_frames += lit_count(frame) == 0 ? 1 : 0;
    }
    TEST_ASSERT_GREATER_THAN_INT(2, most_lit);
    TEST_ASSERT_LESS_OR_EQUAL_INT(7, most_lit);
    TEST_ASSERT_GREATER_THAN_INT(0, dark_frames);

    std::array<Rgb, 1> one{};
    s_renderer.render(spec, 500, one);  // Must not fail on the shortest chain.
}

static void test_heartbeat_beats_twice_then_rests() {
    const EffectSpec spec = parse_ok(R"({"fx":"heartbeat","ix":10,"col":["FFFFFF"]})");
    Frame frame{};
    const auto level_at = [&](uint32_t ms) {
        s_renderer.render(spec, ms, frame);
        return static_cast<int>(frame[0].r);
    };
    TEST_ASSERT_EQUAL_INT(255, level_at(0));
    TEST_ASSERT_EQUAL_INT(10, level_at(290));
    TEST_ASSERT_INT_WITHIN(2, 191, level_at(300));
    TEST_ASSERT_LESS_THAN_INT(level_at(0), level_at(300));
    TEST_ASSERT_EQUAL_INT(10, level_at(1000));
    TEST_ASSERT_EQUAL_INT(255, level_at(1400));
}

/* Classic garland modes: two groups (ix 0), and white bulbs so levels read directly. */

static EffectSpec classic_spec(const std::string& fx, int ix = 0) {
    return parse_ok(R"({"fx":")" + fx + R"(","ix":)" + std::to_string(ix) + R"(,"col":["FFFFFF"]})");
}

static void test_classic_bulbs_keep_their_own_colors() {
    const EffectSpec spec = parse_ok(R"({"fx":"steady","pal":[0,"FF0000",85,"00FF00",170,"0000FF",255,"FFFF00"]})");
    Frame frame{};
    s_renderer.render(spec, 12345, frame);
    TEST_ASSERT_TRUE((frame[0] == Rgb{255, 0, 0}));
    TEST_ASSERT_TRUE((frame[1] == Rgb{0, 255, 0}));
    TEST_ASSERT_TRUE((frame[2] == Rgb{0, 0, 255}));
    TEST_ASSERT_TRUE((frame[3] == Rgb{255, 255, 0}));
    TEST_ASSERT_TRUE((frame[4] == Rgb{255, 0, 0}));
}

static void test_flash_alternates_the_two_groups() {
    const EffectSpec spec = classic_spec("flash");
    Frame frame{};
    s_renderer.render(spec, 0, frame);
    TEST_ASSERT_EQUAL_UINT8(255, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(0, frame[1].r);
    s_renderer.render(spec, 500, frame);
    TEST_ASSERT_EQUAL_UINT8(0, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(255, frame[1].r);
}

static void test_slow_fade_never_goes_dark() {
    const EffectSpec spec = classic_spec("slow-fade");
    Frame frame{};
    for (uint32_t ms = 0; ms < 10000; ms += 50) {
        s_renderer.render(spec, ms, frame);
        TEST_ASSERT_EQUAL_INT(255, frame[0].r + frame[1].r);
    }
}

static void test_waves_take_turns() {
    const EffectSpec spec = classic_spec("waves");
    Frame frame{};
    s_renderer.render(spec, 0, frame);
    TEST_ASSERT_EQUAL_UINT8(0, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(255, frame[1].r);
    s_renderer.render(spec, 2000, frame);
    TEST_ASSERT_EQUAL_UINT8(255, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(0, frame[1].r);
}

static void test_sequential_turns_on_then_off_in_order() {
    const EffectSpec spec = classic_spec("sequential", 64);  // Three groups.
    Frame frame{};
    const auto lit_groups = [&](uint32_t ms) {
        s_renderer.render(spec, ms, frame);
        return (frame[0].r != 0 ? 1 : 0) + (frame[1].r != 0 ? 2 : 0) + (frame[2].r != 0 ? 4 : 0);
    };
    TEST_ASSERT_EQUAL_INT(0b001, lit_groups(0));
    TEST_ASSERT_EQUAL_INT(0b011, lit_groups(500));
    TEST_ASSERT_EQUAL_INT(0b111, lit_groups(1000));
    TEST_ASSERT_EQUAL_INT(0b110, lit_groups(1500));
    TEST_ASSERT_EQUAL_INT(0b100, lit_groups(2000));
    TEST_ASSERT_EQUAL_INT(0b000, lit_groups(2500));
    TEST_ASSERT_EQUAL_INT(0b001, lit_groups(3000));
}

static void test_chasing_lights_one_group_at_a_time() {
    const EffectSpec spec = classic_spec("chasing", 64);  // Three groups.
    Frame frame{};
    s_renderer.render(spec, 200, frame);
    TEST_ASSERT_EQUAL_UINT8(0, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(255, frame[1].r);
    TEST_ASSERT_EQUAL_UINT8(0, frame[2].r);
    TEST_ASSERT_EQUAL_UINT8(255, frame[4].r);
}

static void test_slo_glo_moves_the_whole_chain_together() {
    const EffectSpec spec = classic_spec("slo-glo");
    Frame frame{};
    s_renderer.render(spec, 0, frame);
    TEST_ASSERT_EQUAL_INT(0, lit_count(frame));
    s_renderer.render(spec, 3000, frame);
    TEST_ASSERT_EQUAL_UINT8(255, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(255, frame[31].r);
}

static void test_combination_goes_through_the_moving_modes() {
    const EffectSpec combination = classic_spec("combination");
    Frame frame{};
    Frame expected{};
    const char* const modes[] = {"waves", "sequential", "slo-glo", "chasing", "slow-fade", "flash"};
    for (uint32_t i = 0; i < 6; ++i) {
        s_renderer.render(combination, i * 10000 + 700, frame);
        s_renderer.render(classic_spec(modes[i]), 700, expected);
        TEST_ASSERT_TRUE_MESSAGE(frame == expected, modes[i]);
    }
    s_renderer.render(combination, 60000 + 700, frame);
    s_renderer.render(classic_spec("waves"), 700, expected);
    TEST_ASSERT_TRUE(frame == expected);
}

static void test_catalog_fits_the_favourites_mask() {
    load_shipped_catalog();
    TEST_ASSERT_LESS_OR_EQUAL_size_t(kMaxCatalogEffects, kShippedEffectCount);
    TEST_ASSERT_EQUAL_size_t(kShippedEffectCount, catalog_size());
    // Past bit 63, where one word of mask would have lost the effect.
    TEST_ASSERT_GREATER_THAN_size_t(64, catalog_size());
    const size_t last = catalog_size() - 1;
    char text[kFavoritesTextBytes];
    EffectMask only_last;
    only_last.set(last, true);
    favorites_to_text(only_last, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING(catalog_effect(last).id, text);
    TEST_ASSERT_TRUE(only_last == favorites_from_text(text));
    TEST_ASSERT_EQUAL_UINT16(last, next_favorite(0, only_last, static_cast<uint16_t>(catalog_size())));
    // Every effect ticked still fits the buffer, which is what would cut the list short.
    favorites_to_text(EffectMask::all(catalog_size()), text, sizeof(text));
    TEST_ASSERT_TRUE(EffectMask::all(catalog_size()) == favorites_from_text(text));
}

static void test_default_favourites_are_the_classic_garland_and_fireworks() {
    load_shipped_catalog();
    const EffectMask favorites = catalog_default_favorites();
    // The eight modes of the classic controller, and no more: every one is a press of a button.
    const std::set<std::string> kExpected = {"garland-combination", "garland-waves",     "garland-sequential",
                                             "garland-slo-glo",     "garland-chasing",   "garland-slow-fade",
                                             "garland-flash",       "garland-steady",    "fireworks"};
    for (size_t i = 0; i < catalog_size(); ++i) {
        const std::string id = catalog_effect(i).id;
        const bool expected = kExpected.count(id) != 0;
        TEST_ASSERT_EQUAL_MESSAGE(expected, favorites.test(i), id.c_str());
    }
    // The button's cycle begins with the catalog's first favourite.
    TEST_ASSERT_EQUAL_STRING("garland-combination", catalog_effect(0).id);
}

static void test_every_effect_has_a_group_and_groups_stay_together() {
    load_shipped_catalog();
    std::set<std::string> finished;
    std::string current;
    for (size_t i = 0; i < catalog_size(); ++i) {
        const std::string group = catalog_group(i);
        TEST_ASSERT_FALSE(group.empty());
        if (group != current) {
            TEST_ASSERT_TRUE_MESSAGE(finished.insert(group).second, group.c_str());
            current = group;
        }
    }
    TEST_ASSERT_GREATER_THAN_size_t(3, finished.size());
    // A group may hold planned effects only.
    for (size_t i = 0; i < catalog_planned_size(); ++i) {
        TEST_ASSERT_GREATER_THAN_size_t(0, std::strlen(catalog_planned_group(i)));
    }
}

/* The second batch of effects */

static void test_running_light_moves_along_the_chain() {
    const EffectSpec spec = parse_ok(R"({"fx":"running","ix":64,"col":["FFFFFF"]})");
    Frame frame{};
    s_renderer.render(spec, 0, frame);
    const Frame start = frame;
    TEST_ASSERT_GREATER_THAN_INT(5, lit_count(frame));
    s_renderer.render(spec, 200, frame);
    TEST_ASSERT_FALSE(frame == start);
}

static void test_random_colors_fade_then_hold() {
    const EffectSpec spec = parse_ok(R"({"fx":"random-colors","pal":"Rainbow"})");
    Frame frame{};
    s_renderer.render(spec, 2000, frame);
    const Frame held = frame;
    TEST_ASSERT_TRUE(std::ranges::all_of(frame, [&](const Rgb& led) { return led == frame[0]; }));
    s_renderer.render(spec, 3900, frame);
    TEST_ASSERT_TRUE(frame == held);
    s_renderer.render(spec, 4000 + 2000, frame);
    TEST_ASSERT_FALSE(frame == held);
}

static void test_dissolve_fills_and_empties() {
    const EffectSpec spec = parse_ok(R"({"fx":"dissolve","col":["FFFFFF"]})");
    Frame frame{};
    s_renderer.render(spec, 2999, frame);
    TEST_ASSERT_EQUAL_INT(32, lit_count(frame));
    s_renderer.render(spec, 1500, frame);
    TEST_ASSERT_TRUE(lit_count(frame) > 0 && lit_count(frame) < 32);
    s_renderer.render(spec, 5999, frame);
    TEST_ASSERT_LESS_THAN_INT(2, lit_count(frame));
}

static void test_balls_rise_and_stay_on_the_chain() {
    const EffectSpec spec = parse_ok(R"({"fx":"bouncing-balls","ix":255,"col":["FFFFFF"]})");
    Frame frame{};
    bool reached_top = false;
    for (uint32_t ms = 0; ms < 20000; ms += 16) {
        s_renderer.render(spec, ms, frame);
        TEST_ASSERT_GREATER_THAN_INT(0, lit_count(frame));
        reached_top = reached_top || frame[31].r != 0;
    }
    TEST_ASSERT_TRUE(reached_top);
    std::array<Rgb, 1> one{};
    s_renderer.render(spec, 700, one);
}

static void test_juggle_dots_stay_on_the_chain() {
    const EffectSpec spec = parse_ok(R"({"fx":"juggle","ix":255,"pal":"Rainbow"})");
    Frame frame{};
    for (uint32_t ms = 0; ms < 20000; ms += 64) {
        s_renderer.render(spec, ms, frame);
        TEST_ASSERT_TRUE(lit_count(frame) >= 1 && lit_count(frame) <= 14);
    }
    std::array<Rgb, 1> one{};
    s_renderer.render(spec, 700, one);
}

static void test_ripple_spreads_and_dies_out() {
    const EffectSpec spec = parse_ok(R"({"fx":"ripple","ix":0,"col":["FFFFFF"]})");
    Frame frame{};
    int dark_frames = 0;
    int most_lit = 0;
    for (uint32_t ms = 0; ms < 20000; ms += 16) {
        s_renderer.render(spec, ms, frame);
        dark_frames += lit_count(frame) == 0 ? 1 : 0;
        most_lit = std::max(most_lit, lit_count(frame));
    }
    TEST_ASSERT_GREATER_THAN_INT(0, dark_frames);
    TEST_ASSERT_GREATER_THAN_INT(2, most_lit);
}

static void test_sunrise_climbs_once_and_stays() {
    const EffectSpec spec = parse_ok(R"({"fx":"sunrise","ix":9,"pal":[0,"000000",255,"FFFFFF"]})");  // 10 s.
    Frame frame{};
    s_renderer.render(spec, 0, frame);
    TEST_ASSERT_EQUAL_INT(0, lit_count(frame));
    s_renderer.render(spec, 5000, frame);
    TEST_ASSERT_UINT8_WITHIN(3, 127, frame[0].r);
    s_renderer.render(spec, 10000, frame);
    TEST_ASSERT_EQUAL_UINT8(255, frame[0].r);
    s_renderer.render(spec, 1000000, frame);
    TEST_ASSERT_EQUAL_UINT8(255, frame[31].r);
}

static void test_drip_swells_then_falls_to_the_end() {
    const EffectSpec spec = parse_ok(R"({"fx":"drip","ix":0,"col":["FFFFFF"]})");
    Frame frame{};
    bool swelled = false;
    bool reached_end = false;
    for (uint32_t ms = 0; ms < 10000; ms += 16) {
        s_renderer.render(spec, ms, frame);
        swelled = swelled || (frame[0].r > 200 && lit_count(frame) == 1);
        reached_end = reached_end || frame[31].r != 0;
        TEST_ASSERT_LESS_OR_EQUAL_INT(kDropTail, lit_count(frame));
    }
    TEST_ASSERT_TRUE(swelled);
    TEST_ASSERT_TRUE(reached_end);
}

static void test_rain_keeps_falling() {
    const EffectSpec spec = parse_ok(R"({"fx":"rain","ix":128,"col":["FFFFFF"]})");
    Frame frame{};
    for (uint32_t ms = 0; ms < 10000; ms += 16) {
        s_renderer.render(spec, ms, frame);
    }
    int lit = 0;
    for (uint32_t ms = 10000; ms < 13000; ms += 100) {
        s_renderer.render(spec, ms, frame);
        lit += lit_count(frame);
    }
    TEST_ASSERT_GREATER_THAN_INT(0, lit);
    std::array<Rgb, 1> one{};
    s_renderer.render(spec, 700, one);
}

// A late frame, or a fast SPEED, takes the steps that came since one by one: one frame after three
// steps ends where three frames a step apart do, rather than one step on.
static void test_a_stateful_effect_catches_up_on_the_steps_it_missed() {
    // Each over a stretch of several of its steps, and no more than it catches up in one frame.
    struct Case {
        const char* json;
        uint32_t stretch_ms;
    };
    for (const Case& c : {Case{R"({"fx":"drift","ix":255})", 210}, Case{R"({"fx":"fire","sx":0,"ix":255})", 168},
                          Case{R"({"fx":"candle","ix":0,"col":["FFFFFF"]})", 280}, Case{R"({"fx":"dynamic","sx":255,"ix":255})", 875}}) {
        const char* const json = c.json;
        const EffectSpec spec = parse_ok(json);
        EffectRenderer stepwise(7);
        EffectRenderer late(7);
        Frame by_steps{};
        Frame at_once{};
        for (uint32_t ms = 0; ms <= c.stretch_ms; ms += 5) {
            stepwise.render(spec, ms, by_steps);
        }
        late.render(spec, 0, at_once);
        late.render(spec, c.stretch_ms, at_once);
        for (size_t i = 0; i < by_steps.size(); ++i) {
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(by_steps[i].r, at_once[i].r, json);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(by_steps[i].g, at_once[i].g, json);
        }
    }
}
