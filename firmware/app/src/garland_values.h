/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The values the garland's state holds, their steps and ranges: brightness, speed, the screen, favourites
 * Contributors: None
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace garland {

enum class ProbeStatus : uint8_t { NotChecked, Absent, Present };

// The next step up. A level set by the schedule can lie between the steps.
constexpr uint8_t next_brightness(uint8_t percent) {
    return percent < 10 ? 10 : percent < 25 ? 25 : percent < 50 ? 50 : percent < 75 ? 75 : percent < 100 ? 100 : 10;
}

// Speed is held in quarters so every step stays an integer: 0.25x is 1 and 3x is 12.
inline constexpr uint8_t kSpeedSteps[] = {1, 2, 3, 4, 6, 8, 12};
constexpr uint8_t kNormalSpeed = 4;
constexpr uint8_t next_speed(uint8_t speed_x4) {
    for (const uint8_t step : kSpeedSteps) {
        if (step > speed_x4) {
            return step;
        }
    }
    return kSpeedSteps[0];
}

constexpr bool speed_x4_is_valid(uint8_t speed_x4) {
    for (const uint8_t step : kSpeedSteps) {
        if (step == speed_x4) {
            return true;
        }
    }
    return false;
}

// As a multiple, in JSON. Quarters are exact in binary, so comparing them as doubles is safe. 0
// for anything else.
constexpr uint8_t speed_x4_from_value(double speed) {
    for (const uint8_t step : kSpeedSteps) {
        if (speed == step / 4.0) {
            return step;
        }
    }
    return 0;
}

// The portal's slider goes further and finer than the button's steps and the schedule's list:
// 0.25x to 5x by quarters. The button goes on from there to the next of its steps.
constexpr uint8_t kManualSpeedMax = 20;

constexpr bool manual_speed_x4_is_valid(uint8_t speed_x4) {
    return speed_x4 >= 1 && speed_x4 <= kManualSpeedMax;
}

// As speed_x4_from_value, for the slider.
constexpr uint8_t manual_speed_x4_from_value(double speed) {
    for (uint8_t quarters = 1; quarters <= kManualSpeedMax; ++quarters) {
        if (speed == quarters / 4.0) {
            return quarters;
        }
    }
    return 0;
}

// What a person or the schedule may ask for. Fully off is the power's job.
constexpr bool brightness_percent_is_valid(int percent) {
    return percent >= 1 && percent <= 100;
}

constexpr size_t kSpeedTextBytes = 8;

// "0.25", "1", "1.5": a JSON number and, with an x behind it, what the screen shows.
inline void speed_to_text(uint8_t speed_x4, char (&text)[kSpeedTextBytes]) {
    constexpr const char* kParts[] = {"", ".25", ".5", ".75"};
    snprintf(text, sizeof(text), "%u%s", static_cast<unsigned>(speed_x4 / 4 % 100), kParts[speed_x4 % 4]);
}

enum class ButtonId : uint8_t { Power, Effect, Speed };

constexpr uint32_t kOffCountdownMs = 5000;
constexpr uint8_t kOffCountdownSeconds = kOffCountdownMs / 1000;
static_assert(kOffCountdownSeconds <= 7, "the seconds left share a word with what plays, in three bits");

// Scheduler24 tells of the schedule's next step, and is offered only with the clock module, which keeps the
// time through a power cut; saved, and the module gone since, it runs as the Flying Clock. New after 1.1.1:
// a firmware before it reads it as no valid kind, and takes the default.
enum class SaverKind : uint8_t { None, Starfield, FlyingClock, BreathingDot, BlackScreen, Scheduler24 };

constexpr uint16_t kSaverDelayMinSeconds = 10;
constexpr uint16_t kSaverDelayMaxSeconds = 600;
constexpr uint16_t kSaverDelayDefaultSeconds = 60;

constexpr bool saver_delay_is_valid(int seconds) {
    return seconds >= kSaverDelayMinSeconds && seconds <= kSaverDelayMaxSeconds;
}

constexpr bool saver_kind_is_valid(int kind) {
    return kind >= static_cast<int>(SaverKind::None) && kind <= static_cast<int>(SaverKind::Scheduler24);
}

// Screen backlight, in percent. A lower limit keeps a setting from leaving the screen
// looking dead; Black Screen is the way to turn it off.
constexpr uint8_t kBacklightMinPercent = 5;
constexpr uint8_t kScreenBrightnessDefault = 100;
constexpr uint8_t kSaverBrightnessDefault = 30;

constexpr bool backlight_percent_is_valid(int percent) {
    return percent >= kBacklightMinPercent && percent <= 100;
}

// Squared, as the eye sees steps at the dim end far more strongly. Only 0 is dark.
constexpr uint32_t backlight_duty(uint8_t percent, uint32_t full_duty) {
    if (percent == 0) {
        return 0;
    }
    const uint32_t capped = percent > 100 ? 100 : percent;
    const uint32_t duty = capped * capped * full_duty / 10000;
    return duty > 0 ? duty : 1;
}

struct DisplaySettings {
    SaverKind saver = SaverKind::Starfield;
    uint16_t saver_delay_seconds = kSaverDelayDefaultSeconds;
    uint8_t screen_percent = kScreenBrightnessDefault;
    uint8_t saver_percent = kSaverBrightnessDefault;
};

// One bit per catalog position, with room for the 512 effects the partition table allows.
class EffectMask {
public:
    static constexpr size_t kWords = 9;
    static constexpr size_t kBits = kWords * 64;

    constexpr EffectMask(uint64_t first_word = 0) : words_{first_word} {}

    static constexpr EffectMask all(size_t count) {
        EffectMask mask;
        for (size_t i = 0; i < count && i < kBits; ++i) {
            mask.set(i, true);
        }
        return mask;
    }

    constexpr bool test(size_t bit) const { return (words_[bit / 64] & (uint64_t{1} << (bit % 64))) != 0; }

    constexpr void set(size_t bit, bool on) {
        const uint64_t single = uint64_t{1} << (bit % 64);
        words_[bit / 64] = on ? words_[bit / 64] | single : words_[bit / 64] & ~single;
    }

    constexpr bool any() const {
        for (const uint64_t word : words_) {
            if (word != 0) {
                return true;
            }
        }
        return false;
    }

    constexpr uint64_t word(size_t index) const { return words_[index]; }
    constexpr void set_word(size_t index, uint64_t value) { words_[index] = value; }

    constexpr bool operator==(const EffectMask&) const = default;

private:
    std::array<uint64_t, kWords> words_;
};

// The next favourite after active, wrapping around. active itself need not be a
// favourite, since the portal can activate any effect. With no favourites, stays.
constexpr uint16_t next_favorite(uint16_t active, const EffectMask& favorites, uint16_t count) {
    for (uint16_t step = 1; step <= count; ++step) {
        const uint16_t candidate = static_cast<uint16_t>((active + step) % count);
        if (favorites.test(candidate)) {
            return candidate;
        }
    }
    return active;
}

// The LED count setting: 0 follows the probe, anything else is the owner's number.
constexpr uint16_t kLedCountAuto = 0;

constexpr bool led_setting_is_valid(int setting, int max_leds) {
    return setting >= kLedCountAuto && setting <= max_leds;
}

// A number the owner typed wins. The probe's estimate comes next; with neither,
// the configured length keeps the garland lit.
constexpr uint16_t effective_led_count(uint16_t setting, ProbeStatus probe, uint16_t estimate,
                                       uint16_t fallback) {
    if (setting != kLedCountAuto) {
        return setting;
    }
    return (probe == ProbeStatus::Present && estimate > 0) ? estimate : fallback;
}

// A firmware update's progress in one byte: 0-100 is the percent received, then these.
constexpr uint8_t kUpdateApplying = 101;
constexpr uint8_t kUpdateRestarting = 102;
constexpr uint8_t kNoUpdate = 255;

constexpr uint8_t update_shown_percent(uint8_t progress) {
    return progress > 100 ? 100 : progress;
}

constexpr int update_bar_pixels(uint8_t progress, int full_width) {
    return full_width * update_shown_percent(progress) / 100;
}

// A browser's access code on the screen, shifted so that zero stays "nothing".
constexpr uint16_t kPairDisplayNone = 0;
constexpr uint16_t kPairDisplayForgotten = 0xFFFF;
constexpr uint16_t kPairDisplayForgetFailed = 0xFFFE;
constexpr uint16_t pair_display_for_code(uint16_t code) {
    return static_cast<uint16_t>(code + 1);
}

}  // namespace garland
