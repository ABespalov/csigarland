/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Host tests for the effect engine, catalog, limiter, state and network logic
 * Contributors: None
 */

#include <unity.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <thread>

#include "auth_logic.h"
#include "effect_algorithms.cpp"
#include "effect_burst.cpp"
#include "effect_catalog.cpp"
#include "effect_color.cpp"
#include "effect_memory.h"
#include "effect_motion.cpp"
#include "effect_pack.h"
#include "effect_parser.cpp"
#include "effect_renderer.cpp"
#include "effects.cpp"
#include "effects_data.h"
#include "event_stream.h"
#include "garland_probe_math.h"
#include "garland_state.h"
#include "light_fade.h"
#include "network_logic.h"
#include "ota_logic.h"
#include "playlist_logic.h"
#include "rtc_logic.h"
#include "schedule.cpp"
#include "screen_model.h"
#include "screensaver.cpp"
#include "tls_logic.h"
#include "wordmark.h"

using namespace garland;

using Frame = std::array<Rgb, 32>;

struct JsonDeleterForTest {
    void operator()(cJSON* json) const { cJSON_Delete(json); }
};
using JsonPtrForTest = std::unique_ptr<cJSON, JsonDeleterForTest>;

static EffectRenderer s_renderer(12345);

void setUp() {
    s_renderer.reset();
}
void tearDown() {}

static EffectSpec parse_ok(const std::string& layers) {
    const std::string json =
        R"({"format":"csigarland.effect/1","name":"Test","layers":[)" + layers + "]}";
    EffectSpec spec;
    const char* error = nullptr;
    const bool parsed = parse_effect(json.c_str(), "test", spec, error);
    TEST_ASSERT_TRUE_MESSAGE(parsed, error != nullptr ? error : "no error text");
    return spec;
}

static void assert_rejected(const std::string& json) {
    EffectSpec spec;
    const char* error = nullptr;
    TEST_ASSERT_FALSE_MESSAGE(parse_effect(json.c_str(), "test", spec, error), json.c_str());
    TEST_ASSERT_NOT_NULL(error);
}

static int lit_count(const Frame& frame) {
    return static_cast<int>(std::ranges::count_if(frame, [](const Rgb& c) { return c.r != 0 || c.g != 0 || c.b != 0; }));
}

static uint32_t total_light(const Frame& frame) {
    uint32_t total = 0;
    for (const Rgb& led : frame) {
        total += uint32_t{led.r} + led.g + led.b;
    }
    return total;
}

/* Buttons and favourites */

static void test_brightness_cycle() {
    TEST_ASSERT_EQUAL_UINT8(25, next_brightness(10));
    TEST_ASSERT_EQUAL_UINT8(50, next_brightness(25));
    TEST_ASSERT_EQUAL_UINT8(75, next_brightness(50));
    TEST_ASSERT_EQUAL_UINT8(100, next_brightness(75));
    TEST_ASSERT_EQUAL_UINT8(10, next_brightness(100));
}

static void test_speed_cycle() {
    const uint8_t cycle[] = {1, 2, 3, 4, 6, 8, 12, 1};
    for (size_t i = 0; i + 1 < sizeof(cycle); ++i) {
        TEST_ASSERT_EQUAL_UINT8(cycle[i + 1], next_speed(cycle[i]));
    }
    char text[kSpeedTextBytes];
    speed_to_text(1, text);
    TEST_ASSERT_EQUAL_STRING("0.25", text);
    speed_to_text(4, text);
    TEST_ASSERT_EQUAL_STRING("1", text);
    speed_to_text(6, text);
    TEST_ASSERT_EQUAL_STRING("1.5", text);
    speed_to_text(12, text);
    TEST_ASSERT_EQUAL_STRING("3", text);
}

static void test_next_favorite() {
    constexpr uint32_t kFirstThird = 0b00101;  // Effects 0 and 2 of 5.
    TEST_ASSERT_EQUAL_UINT8(2, next_favorite(0, kFirstThird, 5));
    TEST_ASSERT_EQUAL_UINT8(0, next_favorite(2, kFirstThird, 5));
    // Started from the portal on a non-favourite: the cycle rejoins the list.
    TEST_ASSERT_EQUAL_UINT8(2, next_favorite(1, kFirstThird, 5));
    TEST_ASSERT_EQUAL_UINT8(0, next_favorite(4, kFirstThird, 5));
    // A single favourite stays put, and so does an empty list.
    TEST_ASSERT_EQUAL_UINT8(3, next_favorite(3, 0b01000, 5));
    TEST_ASSERT_EQUAL_UINT8(3, next_favorite(3, 0, 5));
}

static void test_power_button() {
    State state;
    state.set_effect_count(3);
    state.set_favorites(0b111);
    TEST_ASSERT_FALSE(state.snapshot().power_requested);

    // Settings stay put in standby; the first click only turns on.
    state.click_effect();
    state.click_speed();
    state.click_power();
    Snapshot s = state.snapshot();
    TEST_ASSERT_TRUE(s.power_requested);
    TEST_ASSERT_EQUAL_UINT8(50, s.brightness_percent);
    TEST_ASSERT_EQUAL_UINT8(0, s.effect);
    TEST_ASSERT_EQUAL_UINT8(4, s.speed_x4);

    state.click_power();
    TEST_ASSERT_EQUAL_UINT8(75, state.snapshot().brightness_percent);

    state.long_press_power(1000);
    state.tick_off_countdown(1000 + kOffCountdownMs);
    TEST_ASSERT_FALSE(state.snapshot().power_requested);
    TEST_ASSERT_EQUAL_UINT8(75, state.snapshot().brightness_percent);
}

static void test_effect_button_follows_favorites() {
    State state;
    state.set_effect_count(4);
    state.set_favorites(0b1010);
    state.select_first_favorite();
    TEST_ASSERT_EQUAL_UINT8(1, state.snapshot().effect);
    state.click_power();
    state.click_effect();
    TEST_ASSERT_EQUAL_UINT8(3, state.snapshot().effect);
    state.click_effect();
    TEST_ASSERT_EQUAL_UINT8(1, state.snapshot().effect);
}

static void test_portal_activates_any_effect_and_turns_on() {
    State state;
    state.set_effect_count(4);
    state.set_favorites(0b0001);
    state.activate_effect(2);
    const Snapshot s = state.snapshot();
    TEST_ASSERT_EQUAL_UINT8(2, s.effect);
    TEST_ASSERT_TRUE(s.power_requested);
    state.click_effect();
    TEST_ASSERT_EQUAL_UINT8(0, state.snapshot().effect);
}

static void test_portal_mode_values() {
    TEST_ASSERT_EQUAL_UINT8(1, speed_x4_from_value(0.25));
    TEST_ASSERT_EQUAL_UINT8(6, speed_x4_from_value(1.5));
    TEST_ASSERT_EQUAL_UINT8(12, speed_x4_from_value(3));
    TEST_ASSERT_EQUAL_UINT8(0, speed_x4_from_value(0));
    TEST_ASSERT_EQUAL_UINT8(0, speed_x4_from_value(1.49));
    TEST_ASSERT_EQUAL_UINT8(0, speed_x4_from_value(4));
    // The slider takes every quarter up to 5x, and nothing between them.
    TEST_ASSERT_EQUAL_UINT8(1, manual_speed_x4_from_value(0.25));
    TEST_ASSERT_EQUAL_UINT8(5, manual_speed_x4_from_value(1.25));
    TEST_ASSERT_EQUAL_UINT8(20, manual_speed_x4_from_value(5));
    TEST_ASSERT_EQUAL_UINT8(0, manual_speed_x4_from_value(0));
    TEST_ASSERT_EQUAL_UINT8(0, manual_speed_x4_from_value(0.1));
    TEST_ASSERT_EQUAL_UINT8(0, manual_speed_x4_from_value(1.3));
    TEST_ASSERT_EQUAL_UINT8(0, manual_speed_x4_from_value(5.25));
    TEST_ASSERT_TRUE(manual_speed_x4_is_valid(20));
    TEST_ASSERT_FALSE(manual_speed_x4_is_valid(21));
    TEST_ASSERT_FALSE(manual_speed_x4_is_valid(0));
    // The button goes on from any of them to its next step, and from 3x or more to the first.
    TEST_ASSERT_EQUAL_UINT8(6, next_speed(5));
    TEST_ASSERT_EQUAL_UINT8(4, next_speed(3));
    TEST_ASSERT_EQUAL_UINT8(12, next_speed(9));
    TEST_ASSERT_EQUAL_UINT8(1, next_speed(12));
    TEST_ASSERT_EQUAL_UINT8(1, next_speed(13));
    TEST_ASSERT_EQUAL_UINT8(1, next_speed(20));
    TEST_ASSERT_TRUE(brightness_percent_is_valid(1));
    TEST_ASSERT_TRUE(brightness_percent_is_valid(100));
    TEST_ASSERT_FALSE(brightness_percent_is_valid(0));
    TEST_ASSERT_FALSE(brightness_percent_is_valid(101));
}

static void test_portal_mode_changes_only_what_it_names() {
    State state;
    state.set_effect_count(3);
    state.set_brightness(40);
    state.set_speed(8);
    state.set_effect(2);

    // In standby it stays there, and the next turn on shows the new level.
    state.apply_action(30, 0, -1, -1);
    Snapshot s = state.snapshot();
    TEST_ASSERT_FALSE(s.power_requested);
    TEST_ASSERT_EQUAL_UINT8(30, s.brightness_percent);
    TEST_ASSERT_EQUAL_UINT8(8, s.speed_x4);
    TEST_ASSERT_EQUAL_UINT8(2, s.effect);

    state.click_power();
    state.apply_action(0, 12, -1, -1);
    s = state.snapshot();
    TEST_ASSERT_TRUE(s.power_requested);
    TEST_ASSERT_EQUAL_UINT8(30, s.brightness_percent);
    TEST_ASSERT_EQUAL_UINT8(12, s.speed_x4);

    // A countdown to off is not an order it may overrule.
    state.long_press_power(1000);
    state.apply_action(60, 0, -1, -1);
    state.tick_off_countdown(1000 + kOffCountdownMs);
    TEST_ASSERT_FALSE(state.snapshot().power_requested);
}

/* LED count */

static void test_led_count_priority() {
    // The owner's number beats everything, including a probe that found nothing.
    TEST_ASSERT_EQUAL_UINT16(32, effective_led_count(32, ProbeStatus::Absent, 0, 50));
    TEST_ASSERT_EQUAL_UINT16(7, effective_led_count(7, ProbeStatus::Present, 30, 50));
    // Automatic: the estimate, then the configured length.
    TEST_ASSERT_EQUAL_UINT16(30, effective_led_count(kLedCountAuto, ProbeStatus::Present, 30, 50));
    TEST_ASSERT_EQUAL_UINT16(50, effective_led_count(kLedCountAuto, ProbeStatus::Present, 0, 50));
    TEST_ASSERT_EQUAL_UINT16(50, effective_led_count(kLedCountAuto, ProbeStatus::Absent, 0, 50));
    TEST_ASSERT_EQUAL_UINT16(50, effective_led_count(kLedCountAuto, ProbeStatus::NotChecked, 0, 50));
}

static void test_led_setting_range() {
    TEST_ASSERT_TRUE(led_setting_is_valid(0, 256));
    TEST_ASSERT_TRUE(led_setting_is_valid(256, 256));
    TEST_ASSERT_FALSE(led_setting_is_valid(257, 256));
    TEST_ASSERT_FALSE(led_setting_is_valid(-1, 256));
}

static void test_state_applies_led_setting() {
    State state(32);
    TEST_ASSERT_EQUAL_UINT16(32, state.snapshot().led_count);
    state.set_led_setting(12);
    TEST_ASSERT_EQUAL_UINT16(12, state.snapshot().led_count);
    state.set_probe(ProbeStatus::Present, 30);
    TEST_ASSERT_EQUAL_UINT16(12, state.snapshot().led_count);
    state.set_led_setting(kLedCountAuto);
    TEST_ASSERT_EQUAL_UINT16(30, state.snapshot().led_count);
}

/* Limiter and brightness */

static void test_limiter_clamps_full_white() {
    std::array<RgbQ8, 32> frame{};
    std::ranges::fill(frame, RgbQ8{255 * 256, 255 * 256, 255 * 256});
    TEST_ASSERT_EQUAL_UINT32(1792, estimate_current_ma(frame));
    TEST_ASSERT_EQUAL_UINT32(1792, limit_current(frame, 1400));
    const uint32_t after = estimate_current_ma(frame);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(1400, after);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1380, after);

    // A limit below the idle draw: a dark frame has nothing to scale, and a lit one goes dark.
    std::array<RgbQ8, 32> dark{};
    TEST_ASSERT_EQUAL_UINT32(32, limit_current(dark, 10));
    TEST_ASSERT_EQUAL_UINT16(0, dark[0].r);
    limit_current(frame, 10);
    TEST_ASSERT_EQUAL_UINT16(0, frame[0].r);
}

#include "test_catalog_access.h"

#include "test_output.h"

/* Parser */

static void test_parser_reads_a_layer() {
    const EffectSpec spec = parse_ok(
        R"({"fx":"sparkle","sx":10,"ix":20,"col":["FF0000",[0,255,0]],"bri":100,"rev":true,"blend":"add",)"
        R"("zone":["star","pinecone"],"start":2,"stop":9,"grp":2,"spc":1,"of":-3})");
    const Layer& layer = spec.layers[0];
    TEST_ASSERT_TRUE(layer.algorithm == Algorithm::Sparkle);
    TEST_ASSERT_EQUAL_UINT8(10, layer.sx);
    TEST_ASSERT_EQUAL_UINT8(20, layer.ix);
    TEST_ASSERT_TRUE((layer.col[0] == Rgb{255, 0, 0}));
    TEST_ASSERT_TRUE((layer.col[1] == Rgb{0, 255, 0}));
    TEST_ASSERT_EQUAL_UINT8(100, layer.bri);
    TEST_ASSERT_TRUE(layer.rev);
    TEST_ASSERT_TRUE(layer.blend == Blend::Add);
    TEST_ASSERT_EQUAL_UINT8(0b00001010, layer.figurine_mask);
    TEST_ASSERT_EQUAL_INT16(-3, layer.of);
}

static void test_parser_ignores_unknown_fields() {
    const EffectSpec spec = parse_ok(R"({"fx":"solid","from_the_future":1})");
    TEST_ASSERT_EQUAL_UINT8(1, spec.layer_count);
}

static void test_parser_rejects_bad_files() {
    assert_rejected("not json");
    assert_rejected(R"({"format":"other/1","name":"T","layers":[{"fx":"solid"}]})");
    assert_rejected(R"({"format":"csigarland.effect/1","layers":[{"fx":"solid"}]})");
    assert_rejected(R"({"format":"csigarland.effect/1","name":"bad\"name","layers":[{"fx":"solid"}]})");
    assert_rejected(R"({"format":"csigarland.effect/1","name":"T","layers":[]})");
    const std::string head = R"({"format":"csigarland.effect/1","name":"T","layers":[)";
    assert_rejected(head + R"({"fx":"nope"}]})");
    assert_rejected(head + R"({"fx":"solid","sx":300}]})");
    assert_rejected(head + R"({"fx":"solid","col":["12345"]}]})");
    assert_rejected(head + R"({"fx":"solid","pal":[0,"000000"]}]})");
    assert_rejected(head + R"({"fx":"solid","pal":[100,"000000",50,"FFFFFF"]}]})");
    assert_rejected(head + R"({"fx":"solid","pal":"Unknown"}]})");
    assert_rejected(head + R"({"fx":"solid","pal":[0.5,"000000",255,"FFFFFF"]}]})");
    assert_rejected(head + R"({"fx":"solid","col":[[1.5,0,0]]}]})");
    assert_rejected(head + R"({"fx":"solid","zone":["dragon"]}]})");
    assert_rejected(head + R"({"fx":"solid","zone":{"every":2,"offset":2}}]})");
    assert_rejected(head + R"({"fx":"solid","blend":"xor"}]})");
    assert_rejected(head + R"({"fx":"solid"},{"fx":"solid"},{"fx":"solid"},{"fx":"solid"},{"fx":"solid"}]})");
}

/* Catalog */

static void test_catalog_loads_every_shipped_effect() {
    const auto report = [](const char* id, const char* error) {
        const std::string message = std::string(id) + ": " + error;
        TEST_FAIL_MESSAGE(message.c_str());
    };
    TEST_ASSERT_EQUAL_size_t(kShippedEffectCount, load_shipped_catalog(report));
    std::set<std::string> ids;
    for (size_t i = 0; i < catalog_size(); ++i) {
        ids.insert(catalog_effect(i).id);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(i), catalog_find(catalog_effect(i).id));
    }
    TEST_ASSERT_EQUAL_size_t(catalog_size(), ids.size());
    TEST_ASSERT_EQUAL_INT(-1, catalog_find("no-such-effect"));
}

static void* refuse_memory(size_t) {
    return nullptr;
}

// The Lighting task goes on drawing what it holds, so a failed parse must not empty it.
static void test_a_parse_out_of_memory_leaves_the_effect_held() {
    load_shipped_catalog();
    EffectSpec held;
    TEST_ASSERT_TRUE(catalog_copy_spec(0, held));
    const std::string name = held.name;
    const int layers = held.layer_count;
    cJSON_Hooks hooks = {refuse_memory, free};
    cJSON_InitHooks(&hooks);
    const bool copied = catalog_copy_spec(1, held);
    cJSON_InitHooks(nullptr);
    TEST_ASSERT_FALSE(copied);
    TEST_ASSERT_EQUAL_STRING(name.c_str(), held.name);
    TEST_ASSERT_EQUAL_INT(layers, held.layer_count);
    TEST_ASSERT_TRUE(catalog_copy_spec(1, held));
    TEST_ASSERT_EQUAL_STRING(catalog_id(1).c_str(), held.id);
}

static void test_planned_effects_are_not_playable() {
    load_shipped_catalog();
    TEST_ASSERT_EQUAL_size_t(kShippedPlannedCount, catalog_planned_size());
    for (size_t i = 0; i < catalog_planned_size(); ++i) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(-1, catalog_find(catalog_planned_id(i)), catalog_planned_id(i));
        TEST_ASSERT_GREATER_THAN_size_t(0, std::strlen(catalog_planned_name(i)));
    }
}

static void test_every_shipped_effect_lights_up_within_limits() {
    load_shipped_catalog();
    for (size_t i = 0; i < catalog_size(); ++i) {
        s_renderer.reset();
        Frame frame{};
        uint32_t brightest = 0;
        for (uint32_t ms = 0; ms < 6000; ms += 16) {
            s_renderer.render(catalog_effect(i), ms, frame);
            brightest = std::max(brightest, total_light(frame));
        }
        TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, brightest, catalog_effect(i).id);
    }
}

static void test_favorites_text_round_trip() {
    load_shipped_catalog();
    char text[kFavoritesTextBytes];
    const uint32_t mask = 0b101;
    TEST_ASSERT_TRUE(favorites_to_text(mask, text, sizeof(text)));
    TEST_ASSERT_NOT_NULL(std::strchr(text, ','));
    TEST_ASSERT_TRUE(EffectMask(mask) == favorites_from_text(text));
    // An effect removed from a later catalog does not disturb the others.
    const std::string with_stale = std::string("old-effect,") + text;
    TEST_ASSERT_TRUE(EffectMask(mask) == favorites_from_text(with_stale.c_str()));
    TEST_ASSERT_FALSE(favorites_from_text("").any());
    TEST_ASSERT_TRUE(favorites_to_text(EffectMask::all(catalog_size()), text, sizeof(text)));
    TEST_ASSERT_TRUE(EffectMask::all(catalog_size()) == favorites_from_text(text));
    // A list that does not fit whole is not cut short: nothing is written, and the caller says so.
    char small[20];
    TEST_ASSERT_FALSE(favorites_to_text(EffectMask::all(catalog_size()), small, sizeof(small)));
    TEST_ASSERT_EQUAL_STRING("", small);
}

/* Algorithms */

static void test_fade_starts_at_zero_and_peaks() {
    const EffectSpec spec = parse_ok(R"({"fx":"fade","col":["FF8040"]})");
    Frame frame{};
    s_renderer.render(spec, 0, frame);
    TEST_ASSERT_EQUAL_INT(0, lit_count(frame));
    s_renderer.render(spec, 2000, frame);
    TEST_ASSERT_EQUAL_UINT8(255, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(255, frame[31].r);
    s_renderer.render(spec, 4000, frame);
    TEST_ASSERT_EQUAL_INT(0, lit_count(frame));
}

static void test_fade_rises_evenly() {
    const EffectSpec spec = parse_ok(R"({"fx":"fade"})");
    Frame frame{};
    int previous = -1;
    for (uint32_t ms = 0; ms <= 2000; ms += 20) {
        s_renderer.render(spec, ms, frame);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(previous, frame[0].r);
        previous = frame[0].r;
    }
}

static void test_chase_runs_first_to_last() {
    const EffectSpec spec = parse_ok(R"({"fx":"chase","col":["FFFFFF"]})");
    Frame frame{};
    s_renderer.render(spec, 0, frame);
    TEST_ASSERT_NOT_EQUAL(0, frame[0].r);
    TEST_ASSERT_EQUAL_INT(1, lit_count(frame));

    s_renderer.render(spec, 1500, frame);  // 100 ms per LED: the head is on LED 15.
    TEST_ASSERT_EQUAL_UINT8(0, frame[16].r);
    TEST_ASSERT_NOT_EQUAL(0, frame[15].r);
    TEST_ASSERT_LESS_THAN_UINT8(frame[15].r, frame[14].r);

    s_renderer.render(spec, 3199, frame);
    TEST_ASSERT_NOT_EQUAL(0, frame[31].r);
    TEST_ASSERT_EQUAL_UINT8(0, frame[0].r);
}

static void test_chase_keeps_its_pace_on_any_length() {
    const EffectSpec spec = parse_ok(R"({"fx":"chase"})");
    std::array<Rgb, 7> frame{};
    s_renderer.render(spec, 699, frame);
    TEST_ASSERT_NOT_EQUAL(0, frame[6].r);
    s_renderer.render(spec, 700, frame);  // Wrapped to the start.
    TEST_ASSERT_NOT_EQUAL(0, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(0, frame[6].r);
}

static void test_sx_scales_the_pace() {
    const EffectSpec spec = parse_ok(R"({"fx":"fade","sx":64})");  // About half speed.
    Frame frame{};
    s_renderer.render(spec, 4000, frame);
    TEST_ASSERT_GREATER_THAN_UINT8(240, frame[0].r);
}

static void test_palette_interpolates() {
    Palette palette;
    palette.stops[0] = GradientStop{0, Rgb{0, 0, 0}};
    palette.stops[1] = GradientStop{100, Rgb{200, 0, 0}};
    palette.stops[2] = GradientStop{200, Rgb{200, 200, 0}};
    palette.count = 3;
    TEST_ASSERT_TRUE((palette_color(palette, 0) == Rgb{0, 0, 0}));
    TEST_ASSERT_UINT8_WITHIN(1, 100, palette_color(palette, 50).r);
    TEST_ASSERT_TRUE((palette_color(palette, 100) == Rgb{200, 0, 0}));
    TEST_ASSERT_UINT8_WITHIN(1, 100, palette_color(palette, 150).g);
    TEST_ASSERT_TRUE((palette_color(palette, 255) == Rgb{200, 200, 0}));
}

static void test_stripes_show_both_colors() {
    const EffectSpec spec = parse_ok(R"({"fx":"stripes","ix":32,"col":["FF0000","0000FF"]})");
    Frame frame{};
    s_renderer.render(spec, 500, frame);
    const auto has = [&](auto test) { return std::ranges::any_of(frame, test); };
    TEST_ASSERT_TRUE(has([](const Rgb& c) { return c.r > 200 && c.b < 50; }));
    TEST_ASSERT_TRUE(has([](const Rgb& c) { return c.b > 200 && c.r < 50; }));

    // Equal bands: over a moment, each colour takes about half the light.
    uint32_t red = 0;
    uint32_t blue = 0;
    for (uint32_t ms = 0; ms < 2000; ms += 7) {
        s_renderer.render(spec, ms, frame);
        for (const Rgb& led : frame) {
            red += led.r;
            blue += led.b;
        }
    }
    TEST_ASSERT_UINT32_WITHIN(red / 10, red, blue);
}

static void test_twinkle_share_follows_ix() {
    const auto lit_over_time = [](uint8_t ix) {
        const EffectSpec spec = parse_ok(R"({"fx":"twinkle","ix":)" + std::to_string(ix) + R"(,"col":["FFFFFF"]})");
        Frame frame{};
        int lit = 0;
        for (uint32_t ms = 0; ms < 30000; ms += 250) {
            s_renderer.render(spec, ms, frame);
            lit += lit_count(frame);
        }
        return lit;
    };
    TEST_ASSERT_EQUAL_INT(0, lit_over_time(0));
    TEST_ASSERT_GREATER_THAN_INT(lit_over_time(40), lit_over_time(200));
}

static void test_fire_and_candle_stay_alive() {
    const EffectSpec fire = parse_ok(R"({"fx":"fire","sx":64,"ix":160,"pal":[0,"000000",255,"FFFFFF"]})");
    const EffectSpec candle = parse_ok(R"({"fx":"candle","ix":200,"col":["FFFFFF"]})");
    Frame frame{};
    uint32_t light = 0;
    for (uint32_t ms = 0; ms < 8000; ms += 16) {
        s_renderer.render(fire, ms, frame);
        light += total_light(frame);
    }
    TEST_ASSERT_GREATER_THAN_UINT32(0, light);

    s_renderer.reset();
    for (uint32_t ms = 0; ms < 8000; ms += 16) {
        s_renderer.render(candle, ms, frame);
        // ix is the floor; integer steps may undershoot by a little.
        TEST_ASSERT_GREATER_OR_EQUAL_UINT8(190, frame[3].r);
    }
}

#include "test_animations.h"
#include "test_effects_full.h"
#include "test_rtc.h"

/* Layer composition */

static void test_zone_masks_by_figurine() {
    const EffectSpec spec = parse_ok(R"({"fx":"solid","col":["FFFFFF"],"zone":["icicle"]})");
    Frame frame{};
    s_renderer.render(spec, 0, frame);
    for (int i = 0; i < 32; ++i) {
        TEST_ASSERT_EQUAL_UINT8(i % 8 == 6 ? 255 : 0, frame[i].r);
    }
}

static void test_zone_every_and_offset() {
    const EffectSpec spec = parse_ok(R"({"fx":"solid","col":["FFFFFF"],"zone":{"every":3,"offset":1}})");
    Frame frame{};
    s_renderer.render(spec, 0, frame);
    TEST_ASSERT_EQUAL_UINT8(0, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(255, frame[1].r);
    TEST_ASSERT_EQUAL_UINT8(255, frame[4].r);
    TEST_ASSERT_EQUAL_UINT8(0, frame[5].r);
}

static void test_blend_modes() {
    const EffectSpec added = parse_ok(R"({"fx":"solid","col":["C80000"]},{"fx":"solid","col":["C80010"],"blend":"add"})");
    Frame frame{};
    s_renderer.render(added, 0, frame);
    TEST_ASSERT_EQUAL_UINT8(255, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(16, frame[0].b);

    const EffectSpec maxed = parse_ok(R"({"fx":"solid","col":["C80000"]},{"fx":"solid","col":["100010"],"blend":"max"})");
    s_renderer.render(maxed, 0, frame);
    TEST_ASSERT_EQUAL_UINT8(200, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(16, frame[0].b);

    const EffectSpec replaced = parse_ok(R"({"fx":"solid","col":["C80000"]},{"fx":"solid","col":["100010"]})");
    s_renderer.render(replaced, 0, frame);
    TEST_ASSERT_EQUAL_UINT8(16, frame[0].r);
}

static void test_range_group_spacing() {
    const EffectSpec spec = parse_ok(R"({"fx":"solid","col":["FFFFFF"],"start":4,"stop":12,"grp":2,"spc":1})");
    Frame frame{};
    s_renderer.render(spec, 0, frame);
    const std::array<int, 12> expected = {0, 0, 0, 0, 255, 255, 0, 255, 255, 0, 255, 255};
    for (size_t i = 0; i < expected.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(expected[i], frame[i].r);
    }
    TEST_ASSERT_EQUAL_UINT8(0, frame[12].r);
}

static void test_reverse_mirror_and_bri() {
    Frame frame{};
    s_renderer.render(parse_ok(R"({"fx":"chase","col":["FFFFFF"],"rev":true})"), 0, frame);
    TEST_ASSERT_NOT_EQUAL(0, frame[31].r);
    TEST_ASSERT_EQUAL_UINT8(0, frame[0].r);

    s_renderer.reset();
    s_renderer.render(parse_ok(R"({"fx":"chase","col":["FFFFFF"],"mi":true})"), 0, frame);
    TEST_ASSERT_NOT_EQUAL(0, frame[0].r);
    TEST_ASSERT_NOT_EQUAL(0, frame[31].r);
    TEST_ASSERT_EQUAL_UINT8(0, frame[15].r);

    s_renderer.render(parse_ok(R"({"fx":"solid","col":["FFFFFF"],"bri":128})"), 0, frame);
    TEST_ASSERT_EQUAL_UINT8(128, frame[0].r);
}

static void test_out_of_range_layer_is_harmless() {
    Frame frame{};
    s_renderer.render(parse_ok(R"({"fx":"solid","col":["FFFFFF"],"start":100,"stop":200})"), 0, frame);
    TEST_ASSERT_EQUAL_INT(0, lit_count(frame));
    s_renderer.render(parse_ok(R"({"fx":"solid","col":["FFFFFF"],"stop":200})"), 0, frame);
    TEST_ASSERT_EQUAL_INT(32, lit_count(frame));
}

/* Probe */

static void test_probe_presence() {
    TEST_ASSERT_FALSE(probe_says_present(3300));
    TEST_ASSERT_FALSE(probe_says_present(2600));
    TEST_ASSERT_TRUE(probe_says_present(500));
    // Measured on an LED strip with its 5 V rail always on.
    TEST_ASSERT_TRUE(probe_says_present(1937));
}

static void test_probe_estimate() {
    // Ideal 32 x 100 nF: (3300 - 650) mV / 45 kOhm = 58 uA across 300 mV takes 16.6 ms.
    TEST_ASSERT_UINT8_WITHIN(1, 32, probe_estimate_leds(500, 800, 16'600));
    TEST_ASSERT_UINT8_WITHIN(1, 1, probe_estimate_leds(500, 800, 480));
    TEST_ASSERT_EQUAL_UINT8(0, probe_estimate_leds(500, 500, 1000));
    TEST_ASSERT_EQUAL_UINT8(0, probe_estimate_leds(500, 800, 0));
    // Up to the longest supported chain, not a two-digit ceiling.
    TEST_ASSERT_UINT16_WITHIN(5, 150, probe_estimate_leds(500, 800, 77'800));
    TEST_ASSERT_EQUAL_UINT16(CSIGARLAND_MAX_LEDS, probe_estimate_leds(500, 800, 1'000'000));
}

#include "test_schedule.h"

static void test_brightness_button_from_any_level() {
    TEST_ASSERT_EQUAL_UINT8(25, next_brightness(10));
    TEST_ASSERT_EQUAL_UINT8(50, next_brightness(40));
    TEST_ASSERT_EQUAL_UINT8(75, next_brightness(60));
    TEST_ASSERT_EQUAL_UINT8(100, next_brightness(90));
}

/* Wordmark */

static void test_wordmark_colours_and_size() {
    constexpr uint16_t kBack = 1;
    constexpr uint16_t kCsi = 2;
    constexpr uint16_t kGarland = 3;
    std::array<uint16_t, kWordmarkPixels> pixels{};
    render_wordmark(pixels, kBack, kCsi, kGarland);

    TEST_ASSERT_EQUAL_INT(177, kWordmarkWidth);
    TEST_ASSERT_EQUAL_INT(21, kWordmarkHeight);

    // "csi" ends and "Garland" starts at the fourth letter's pitch.
    const int split_x = kWordmarkWhiteChars * kWordmarkPitchColumns * kWordmarkScale;
    int csi = 0;
    int garland = 0;
    for (int y = 0; y < kWordmarkHeight; ++y) {
        for (int x = 0; x < kWordmarkWidth; ++x) {
            const uint16_t pixel = pixels[y * kWordmarkWidth + x];
            if (x < split_x) {
                TEST_ASSERT_NOT_EQUAL(kGarland, pixel);
                csi += pixel == kCsi;
            } else {
                TEST_ASSERT_NOT_EQUAL(kCsi, pixel);
                garland += pixel == kGarland;
            }
        }
    }
    TEST_ASSERT_GREATER_THAN_INT(0, csi);
    TEST_ASSERT_GREATER_THAN_INT(csi, garland);
}

#include "test_network.h"

#include "test_gestures.h"
#include "test_choices.h"
#include "test_screens.h"
#include "test_uploads.h"
#include "test_packs.h"
#include "test_auth.h"
#include "test_api.h"
#include "test_light.h"

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_off_countdown_keeps_the_garland_lit_then_turns_off);
    RUN_TEST(test_off_countdown_survives_the_clock_wrapping);
    RUN_TEST(test_any_button_cancels_the_countdown_and_does_nothing_else);
    RUN_TEST(test_holding_the_cancelling_press_does_not_restart_the_countdown);
    RUN_TEST(test_a_deliberate_order_ends_the_countdown);
    RUN_TEST(test_long_press_power_in_standby_does_nothing);
    RUN_TEST(test_double_click_effect_goes_to_the_first_favorite_when_on);
    RUN_TEST(test_long_press_effect_starts_and_stops_the_playlist);
    RUN_TEST(test_long_press_speed_returns_to_normal_when_on);
    RUN_TEST(test_a_press_that_wakes_the_screen_does_nothing_else);
    RUN_TEST(test_schedule_accepts_half_speed);
    RUN_TEST(test_ap_password_uses_its_alphabet_evenly);
    RUN_TEST(test_ap_password_skips_bytes_that_would_skew_it);
    RUN_TEST(test_ap_password_check_rejects_what_was_not_made);
    RUN_TEST(test_backlight_duty_follows_the_eye);
    RUN_TEST(test_backlight_settings_range_and_no_wake);
    run_device_tests();
    run_choice_tests();
    run_screen_tests();
    run_upload_tests();
    run_pack_tests();
    run_auth_tests();
    run_api_tests();
    RUN_TEST(test_saver_starts_after_its_delay_and_a_press_ends_it);
    RUN_TEST(test_saver_none_never_starts);
    RUN_TEST(test_saver_stays_off_the_countdown);
    RUN_TEST(test_starfield_stays_on_the_panel);
    RUN_TEST(test_flying_clock_bounces_inside_the_panel);
    RUN_TEST(test_flying_clock_shows_uptime_until_the_time_is_known);
    RUN_TEST(test_breath_rises_and_falls);
    RUN_TEST(test_saver_delay_range);
    RUN_TEST(test_brightness_cycle);
    RUN_TEST(test_speed_cycle);
    RUN_TEST(test_next_favorite);
    RUN_TEST(test_power_button);
    RUN_TEST(test_effect_button_follows_favorites);
    RUN_TEST(test_portal_activates_any_effect_and_turns_on);
    RUN_TEST(test_portal_mode_values);
    RUN_TEST(test_portal_mode_changes_only_what_it_names);
    RUN_TEST(test_led_count_priority);
    RUN_TEST(test_led_setting_range);
    RUN_TEST(test_state_applies_led_setting);
    RUN_TEST(test_limiter_clamps_full_white);
    RUN_TEST(test_brightness_keeps_fractions_of_a_step);
    RUN_TEST(test_brightness_percent_is_perceptual);
    RUN_TEST(test_dither_averages_to_the_target);
    RUN_TEST(test_dither_sends_nothing_for_whole_steps);
    RUN_TEST(test_dither_spreads_leds_of_one_level);
    RUN_TEST(test_parser_reads_a_layer);
    RUN_TEST(test_parser_ignores_unknown_fields);
    RUN_TEST(test_parser_rejects_bad_files);
    RUN_TEST(test_catalog_loads_every_shipped_effect);
    RUN_TEST(test_a_parse_out_of_memory_leaves_the_effect_held);
    RUN_TEST(test_planned_effects_are_not_playable);
    RUN_TEST(test_every_shipped_effect_lights_up_within_limits);
    RUN_TEST(test_favorites_text_round_trip);
    RUN_TEST(test_fade_starts_at_zero_and_peaks);
    RUN_TEST(test_fade_rises_evenly);
    RUN_TEST(test_chase_runs_first_to_last);
    RUN_TEST(test_chase_keeps_its_pace_on_any_length);
    RUN_TEST(test_sx_scales_the_pace);
    RUN_TEST(test_palette_interpolates);
    RUN_TEST(test_stripes_show_both_colors);
    RUN_TEST(test_twinkle_share_follows_ix);
    RUN_TEST(test_fire_and_candle_stay_alive);
    RUN_TEST(test_breathe_never_goes_below_ix);
    RUN_TEST(test_wipe_spreads_then_takes_the_next_color);
    RUN_TEST(test_wipe_alternates_a_lone_color_with_dark);
    RUN_TEST(test_theater_steps_along_the_chain);
    RUN_TEST(test_meteor_tail_leaves_the_chain);
    RUN_TEST(test_scanner_turns_at_the_ends);
    RUN_TEST(test_colorloop_moves_the_whole_chain_together);
    RUN_TEST(test_drift_changes_color_gradually);
    RUN_TEST(test_fireworks_burst_and_die_out);
    RUN_TEST(test_heartbeat_beats_twice_then_rests);
    RUN_TEST(test_classic_bulbs_keep_their_own_colors);
    RUN_TEST(test_flash_alternates_the_two_groups);
    RUN_TEST(test_slow_fade_never_goes_dark);
    RUN_TEST(test_waves_take_turns);
    RUN_TEST(test_sequential_turns_on_then_off_in_order);
    RUN_TEST(test_chasing_lights_one_group_at_a_time);
    RUN_TEST(test_slo_glo_moves_the_whole_chain_together);
    RUN_TEST(test_combination_goes_through_the_moving_modes);
    RUN_TEST(test_catalog_fits_the_favourites_mask);
    RUN_TEST(test_default_favourites_are_the_classic_garland_and_fireworks);
    RUN_TEST(test_every_effect_has_a_group_and_groups_stay_together);
    RUN_TEST(test_running_light_moves_along_the_chain);
    RUN_TEST(test_random_colors_fade_then_hold);
    RUN_TEST(test_dissolve_fills_and_empties);
    RUN_TEST(test_balls_rise_and_stay_on_the_chain);
    RUN_TEST(test_juggle_dots_stay_on_the_chain);
    RUN_TEST(test_ripple_spreads_and_dies_out);
    RUN_TEST(test_sunrise_climbs_once_and_stays);
    RUN_TEST(test_drip_swells_then_falls_to_the_end);
    RUN_TEST(test_rain_keeps_falling);
    RUN_TEST(test_a_stateful_effect_catches_up_on_the_steps_it_missed);
    RUN_TEST(test_every_effect_runs_on_any_chain_length);
    RUN_TEST(test_every_effect_survives_a_long_uptime);
    RUN_TEST(test_bands_repeat_the_stops_and_stand_still_at_speed_zero);
    RUN_TEST(test_bands_move_with_speed);
    RUN_TEST(test_sweep_comes_back_from_the_far_end);
    RUN_TEST(test_percent_fills_the_share_of_the_chain);
    RUN_TEST(test_traffic_light_shows_one_phase_at_a_time);
    RUN_TEST(test_blink_keeps_its_duty);
    RUN_TEST(test_strobe_burst_rattles_and_rests);
    RUN_TEST(test_alternate_flashes_one_side_at_a_time);
    RUN_TEST(test_tetrix_fills_the_chain_and_clears_it);
    RUN_TEST(test_sinelon_leaves_a_trail_that_fades);
    RUN_TEST(test_noise_changes_smoothly);
    run_rtc_tests();
    RUN_TEST(test_zone_masks_by_figurine);
    RUN_TEST(test_zone_every_and_offset);
    RUN_TEST(test_blend_modes);
    RUN_TEST(test_range_group_spacing);
    RUN_TEST(test_reverse_mirror_and_bri);
    RUN_TEST(test_out_of_range_layer_is_harmless);
    RUN_TEST(test_probe_presence);
    RUN_TEST(test_probe_estimate);
    RUN_TEST(test_brightness_button_from_any_level);
    run_schedule_tests();
    RUN_TEST(test_wordmark_colours_and_size);
    run_network_tests();
    run_light_tests();
    return UNITY_END();
}
