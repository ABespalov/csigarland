/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The LEDs' power budget: what the supply gives, what an LED draws, and the limit worked out from them
 * Contributors: None
 */

#pragma once

#include <cstdint>

namespace garland {

// The owner's settings, on the Device page. The defaults are the Hi-Link's 2000 mA, a WS2812B as measured
// on a garland of this design, 36.1 mA lit above 0.51 mA idle at full white, with room for the spread of a
// batch, and the margin that gives the 1400 mA limit of the releases before these settings: (2000 - 250) * 80 %.
struct PowerSettings {
    uint16_t supply_ma = 2000;
    uint8_t white_ma = 40;  // One LED at full white, above its idle draw.
    uint8_t idle_ma = 1;    // One LED dark, which no brightness takes away.
    uint8_t margin_percent = 20;

    bool operator==(const PowerSettings&) const = default;
};

constexpr int kSupplyMinMa = 500;
constexpr int kSupplyMaxMa = 20000;
constexpr int kLedWhiteMinMa = 5;
constexpr int kLedWhiteMaxMa = 100;
constexpr int kLedIdleMaxMa = 5;
constexpr int kMarginMinPercent = 5;
constexpr int kMarginMaxPercent = 20;
// The ESP32-C6, its screen and Wi-Fi, which the same supply feeds beside the LEDs: 120-180 mA typical by
// the specification, about 250 mA at a peak, which is what is kept for it. Not measured yet.
constexpr uint32_t kBoardReserveMa = 250;

constexpr bool supply_ma_is_valid(int value) {
    return value >= kSupplyMinMa && value <= kSupplyMaxMa;
}

constexpr bool led_white_ma_is_valid(int value) {
    return value >= kLedWhiteMinMa && value <= kLedWhiteMaxMa;
}

constexpr bool led_idle_ma_is_valid(int value) {
    return value >= 0 && value <= kLedIdleMaxMa;
}

constexpr bool margin_percent_is_valid(int value) {
    return value >= kMarginMinPercent && value <= kMarginMaxPercent;
}

constexpr bool power_settings_are_valid(const PowerSettings& s) {
    return supply_ma_is_valid(s.supply_ma) && led_white_ma_is_valid(s.white_ma) && led_idle_ma_is_valid(s.idle_ma) &&
           margin_percent_is_valid(s.margin_percent);
}

// What the LEDs may draw: what the supply gives beyond the board's own draw, less the margin, a share of
// that. 200 mA at the least, for the smallest supply with the largest margin.
constexpr uint32_t power_limit_ma(const PowerSettings& s) {
    const uint32_t beyond_board = s.supply_ma > kBoardReserveMa ? s.supply_ma - kBoardReserveMa : 0;
    return beyond_board * (100U - s.margin_percent) / 100U;
}

// One LED at full white, its idle draw with it.
constexpr uint32_t led_full_ma(const PowerSettings& s) {
    return uint32_t{s.white_ma} + s.idle_ma;
}

// A chain of leds all at full white.
constexpr uint32_t peak_ma(const PowerSettings& s, uint32_t leds) {
    return leds * led_full_ma(s);
}

// How many LEDs show full white with nothing dimmed.
constexpr uint32_t undimmed_leds(const PowerSettings& s) {
    return power_limit_ma(s) / led_full_ma(s);
}

// What the estimate and the limiter go by, each frame.
struct CurrentModel {
    uint32_t white_ma;
    uint32_t idle_ma;
    uint32_t limit_ma;
};

constexpr CurrentModel current_model(const PowerSettings& s) {
    return CurrentModel{s.white_ma, s.idle_ma, power_limit_ma(s)};
}

// All four in one word, so that the Lighting task reads those of one save together: 15 bits of supply,
// 7 of white, 3 of idle and 5 of margin. Valid settings only.
constexpr uint32_t pack_power(const PowerSettings& s) {
    return uint32_t{s.supply_ma} | (uint32_t{s.white_ma} << 15) | (uint32_t{s.idle_ma} << 22) |
           (uint32_t{s.margin_percent} << 25);
}

constexpr PowerSettings unpack_power(uint32_t word) {
    PowerSettings s;
    s.supply_ma = static_cast<uint16_t>(word & 0x7FFFU);
    s.white_ma = static_cast<uint8_t>((word >> 15) & 0x7FU);
    s.idle_ma = static_cast<uint8_t>((word >> 22) & 0x7U);
    s.margin_percent = static_cast<uint8_t>((word >> 25) & 0x1FU);
    return s;
}

static_assert(power_limit_ma(PowerSettings{}) == 1400, "the defaults keep the limit of earlier releases");
static_assert(kSupplyMaxMa <= 0x7FFF && kLedWhiteMaxMa <= 0x7F && kLedIdleMaxMa <= 0x7 && kMarginMaxPercent <= 0x1F,
              "each setting fits its bits");
static_assert(power_limit_ma(PowerSettings{kSupplyMinMa, kLedWhiteMinMa, 0, kMarginMaxPercent}) > 0,
              "the smallest supply leaves the LEDs some current");

}  // namespace garland
