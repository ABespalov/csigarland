/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Shared garland state and the actions that change it
 * Contributors: None
 */

#pragma once

#include <array>
#include <atomic>
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

enum class SaverKind : uint8_t { None, Starfield, FlyingClock, BreathingDot, BlackScreen };

constexpr uint16_t kSaverDelayMinSeconds = 10;
constexpr uint16_t kSaverDelayMaxSeconds = 600;
constexpr uint16_t kSaverDelayDefaultSeconds = 60;

constexpr bool saver_delay_is_valid(int seconds) {
    return seconds >= kSaverDelayMinSeconds && seconds <= kSaverDelayMaxSeconds;
}

constexpr bool saver_kind_is_valid(int kind) {
    return kind >= static_cast<int>(SaverKind::None) && kind <= static_cast<int>(SaverKind::BlackScreen);
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

// One consistent-enough copy of the state for the display and the log.
struct Snapshot {
    bool power_requested;
    bool rail_on;
    uint8_t brightness_percent;
    uint16_t effect;
    EffectMask favorites;
    uint8_t speed_x4;
    ProbeStatus probe;
    uint16_t led_estimate;
    uint16_t led_setting;
    uint16_t led_count;
    uint32_t station_ip;  // Network byte order, 0 when not on the home network.
    uint8_t off_seconds;  // Left until the garland turns off, 0 when not counting down.
    bool setup_network;   // The setup access point is up, so the screen shows how to join it.
    uint8_t update_percent;  // A firmware update's progress, see kUpdateApplying, or kNoUpdate.
    bool clock_module;       // The optional DS3231 answered at startup.
    bool power_chosen;       // On or off as last ordered; a missing garland refuses, not changes, it.
    uint16_t pair_display;   // What the access screen shows, see pair_display_for_code.
    uint16_t catalog_revision;  // Moves when an upload or removal may have changed an effect.
    bool making_key;            // The portal's keys or certificates are being made.
    bool output_failed;         // Frames are not reaching the LEDs, see OutputHealth.
    uint16_t led_kept;          // The length in use after led_count was refused, or 0.
    bool action_soon;           // A scheduled action comes within a day.

    bool operator==(const Snapshot&) const = default;
};

// All of it in the 28 bits of one word: an effect in 10, brightness in 7, speed in 5.
static_assert(kManualSpeedMax < 32, "the speed shares a word with what plays, in five bits");

// Written by the button callbacks, the portal, the schedule and the lighting task, read by the
// lighting and UI tasks. What goes together in one action, power and what plays, shares one
// atomic word, so an action arrives whole; the rest stand alone. No task can block the lighting
// task on a lock.
class State {
public:
    explicit State(uint16_t fallback_led_count = 32) : fallback_led_count_(fallback_led_count) {}

    void set_led_setting(uint16_t setting) { led_setting_.store(setting); }

    void set_effect_count(uint16_t count) { effect_count_.store(count); }

    void set_favorites(const EffectMask& favorites) {
        for (size_t i = 0; i < EffectMask::kWords; ++i) {
            favorites_[i * 2].store(static_cast<uint32_t>(favorites.word(i)));
            favorites_[i * 2 + 1].store(static_cast<uint32_t>(favorites.word(i) >> 32));
        }
    }

    // At startup: the first favourite, so the button's cycle begins inside the list.
    void select_first_favorite() {
        const uint16_t count = effect_count_.load();
        if (count > 0) {
            const uint16_t first = next_favorite(static_cast<uint16_t>(count - 1), favorites(), count);
            change_play([first](Play& play) { play.effect = first; });
        }
    }

    // From the portal: any effect, and it turns the garland on to show it.
    void activate_effect(uint16_t index) {
        note_owner_choice();
        change_play([index](Play& play) {
            play.off_seconds = 0;
            play.effect = index;
            order_on(play, true);
        });
        notify_power_listener();
    }

    // Only what it names, all at once. Zero, or a negative effect or power, leaves that part as it is.
    void apply_action(uint8_t brightness_percent, uint8_t speed_x4, int effect, int power) {
        change_play([=](Play& play) { apply_parts(play, brightness_percent, speed_x4, effect, power); });
        if (power >= 0) {
            notify_power_listener();
        }
    }

    // From the schedule, which worked this out after take_owner_choice. A choice since then is
    // newer, so nothing changes and it says so. In the same word, so nothing can come in between.
    bool apply_scheduled(uint8_t brightness_percent, uint8_t speed_x4, int effect, int power) {
        uint32_t word = play_.load();
        uint32_t updated = 0;
        do {
            Play next = unpack(word);
            if (next.chosen_since_look) {
                return false;
            }
            apply_parts(next, brightness_percent, speed_x4, effect, power);
            updated = pack(next);
        } while (!play_.compare_exchange_weak(word, updated));
        if (power >= 0) {
            notify_power_listener();
        }
        return true;
    }

    void set_brightness(uint8_t percent) {
        change_play([percent](Play& play) { play.brightness = percent; });
    }
    void set_speed(uint8_t speed_x4) {
        change_play([speed_x4](Play& play) { play.speed = speed_x4; });
    }
    void set_effect(uint16_t index) {
        change_play([index](Play& play) { play.effect = index; });
    }
    void set_power(bool on) {
        // A deliberate order outranks a countdown that was still running.
        change_play([on](Play& play) {
            play.off_seconds = 0;
            order_on(play, on);
        });
        notify_power_listener();
    }

    // The owner has just chosen what is playing, whether or not that changes it. Counted apart from
    // the values, as choosing the same again, or going back to them, leaves those as they were.
    // Not from the schedule, and not from what a start restores.
    void note_owner_choice() {
        uint32_t (*const clock)() = choice_clock_.load();
        owner_choice_ms_.store(clock != nullptr ? clock() : 0);
        change_play([](Play& play) { play.chosen_since_look = true; });
    }

    // Whether the owner has chosen anything since the last time this was asked. Asked before working
    // out what the schedule wants, so that any choice from here on stops it being applied. A mark
    // rather than a count, as no number of choices can bring it back to what it was.
    bool take_owner_choice() {
        bool was_chosen = false;
        change_play([&was_chosen](Play& play) {
            was_chosen = play.chosen_since_look;
            play.chosen_since_look = false;
        });
        return was_chosen;
    }
    uint32_t owner_choice_ms() const { return owner_choice_ms_.load(); }

    // Milliseconds of a clock that needs no lock, as the lighting task and buttons get here, which
    // rules out the time of day. Whoever reads the moment of a choice sets this.
    void set_choice_clock(uint32_t (*clock)()) { choice_clock_.store(clock); }

    // Every press starts here. A press that cancels the countdown or wakes the
    // screen does only that: its click or long press that follows is ignored.
    void button_down(ButtonId button) {
        const auto bit = static_cast<uint8_t>(1u << static_cast<uint8_t>(button));
        const bool cancelled = end_countdown();
        const bool woke = saver_active_.exchange(false);
        activity_.fetch_add(1);
        if (cancelled) {
            note_owner_choice();
        }
        if (cancelled || woke) {
            consumed_buttons_.fetch_or(bit);
        } else {
            consumed_buttons_.fetch_and(static_cast<uint8_t>(~bit));
        }
    }

    void click_power() {
        if (is_consumed(ButtonId::Power)) {
            return;
        }
        note_owner_choice();
        if (!play().requested) {
            order_power(true);
            return;
        }
        change_play([](Play& play) { play.brightness = next_brightness(play.brightness); });
    }

    // Starts the countdown; the garland stays lit until it runs out.
    void long_press_power(uint32_t now_ms) {
        if (is_consumed(ButtonId::Power) || !play().requested) {
            return;
        }
        note_owner_choice();
        off_deadline_ms_.store(now_ms + kOffCountdownMs);
        change_play([](Play& play) { play.off_seconds = kOffCountdownSeconds; });
    }

    // Called often by the lighting task. Compare-and-swap, because a button press
    // can cancel the countdown between the read and the write.
    void tick_off_countdown(uint32_t now_ms) {
        const uint8_t left = play().off_seconds;
        if (left == 0) {
            return;
        }
        const auto remaining = static_cast<int32_t>(off_deadline_ms_.load() - now_ms);
        if (remaining <= 0) {
            if (end_countdown()) {
                note_owner_choice();
                order_power(false);
            }
            return;
        }
        const auto seconds = static_cast<uint8_t>(std::min<int32_t>((remaining + 999) / 1000, kOffCountdownSeconds));
        if (seconds != left) {
            change_play([seconds](Play& play) {
                if (play.off_seconds != 0) {
                    play.off_seconds = seconds;
                }
            });
        }
    }

    void click_effect() {
        const uint16_t count = effect_count_.load();
        if (!is_consumed(ButtonId::Effect) && play().requested && count > 0) {
            const EffectMask mask = favorites();
            note_owner_choice();
            change_play([&](Play& play) { play.effect = next_favorite(play.effect, mask, count); });
        }
    }

    // First in the button's own cycle, which is the catalog's first effect unless
    // that one is not a favourite.
    void long_press_effect() {
        if (!is_consumed(ButtonId::Effect) && play().requested) {
            note_owner_choice();
            select_first_favorite();
        }
    }

    void click_speed() {
        if (!is_consumed(ButtonId::Speed) && play().requested) {
            note_owner_choice();
            change_play([](Play& play) { play.speed = next_speed(play.speed); });
        }
    }

    void long_press_speed() {
        if (!is_consumed(ButtonId::Speed) && play().requested) {
            note_owner_choice();
            set_speed(kNormalSpeed);
        }
    }

    // A change restarts the idle count, so the new choice gets its whole delay.
    void set_saver(SaverKind kind, uint16_t delay_seconds) {
        saver_kind_.store(kind);
        saver_delay_seconds_.store(delay_seconds);
        activity_.fetch_add(1);
    }
    SaverKind saver_kind() const { return saver_kind_.load(); }
    uint16_t saver_delay_seconds() const { return saver_delay_seconds_.load(); }

    // Take effect at once, and do not wake the screen, so the saver's level can be judged.
    void set_backlight_levels(uint8_t screen_percent, uint8_t saver_percent) {
        screen_percent_.store(screen_percent);
        saver_percent_.store(saver_percent);
    }
    uint8_t screen_percent() const { return screen_percent_.load(); }
    uint8_t saver_percent() const { return saver_percent_.load(); }

    // Told by the display, so button_down knows the press is only a wake-up.
    void set_saver_active(bool active) { saver_active_.store(active); }

    // Changes with every press; the display restarts its idle count on a change.
    uint32_t activity() const { return activity_.load(); }

    void set_rail_on(bool on) { rail_on_.store(on); }

    void set_output_failed(bool failed) { output_failed_.store(failed); }
    void set_led_kept(uint16_t count) { led_kept_.store(count); }
    void set_action_soon(bool soon) { action_soon_.store(soon); }

    // Leaves power_chosen: a garland that is unplugged for now does not change the owner's mind.
    void refuse_power() {
        change_play([](Play& play) { play.requested = false; });
    }

    void order_power(bool on) {
        change_play([on](Play& play) { order_on(play, on); });
        notify_power_listener();
    }

    // Told when the owner's choice of on or off may have changed, so that it can be saved
    // without waiting. Must return at once: buttons and the lighting task call it.
    void set_power_listener(void (*listener)()) { power_listener_.store(listener); }

    void set_probe(ProbeStatus status, uint16_t led_estimate) {
        led_estimate_.store(led_estimate);
        probe_.store(status);
    }

    // From a presence check, which cannot count. The estimate stands for when the garland is back,
    // as one reading that found none must not cost its count until the next turn on.
    void set_presence(ProbeStatus status) {
        probe_.store(status);
    }

    void set_station_ip(uint32_t ip) { station_ip_.store(ip); }

    void set_setup_network(bool on) { setup_network_.store(on); }

    void set_update_percent(uint8_t percent) { update_percent_.store(percent); }

    void set_clock_module(bool present) { clock_module_.store(present); }

    void set_pair_display(uint16_t display) { pair_display_.store(display); }

    void set_making_key(bool making) { making_key_.store(making); }

    // An upload or removal may change what a position plays with nothing else changing.
    void note_catalog_changed() { catalog_revision_.fetch_add(1); }

    // The lighting task's word that it has stopped touching the LED output, during an update.
    void set_lighting_paused(bool paused) { lighting_paused_.store(paused); }
    bool lighting_paused() const { return lighting_paused_.load(); }

    Snapshot snapshot() const {
        const ProbeStatus probe = probe_.load();
        const uint16_t estimate = probe == ProbeStatus::Present ? led_estimate_.load() : 0;
        const uint16_t setting = led_setting_.load();
        const Play now = play();
        return Snapshot{
            now.requested,
            rail_on_.load(),
            now.brightness,
            now.effect,
            favorites(),
            now.speed,
            probe,
            estimate,
            setting,
            effective_led_count(setting, probe, estimate, fallback_led_count_),
            station_ip_.load(),
            now.off_seconds,
            setup_network_.load(),
            update_percent_.load(),
            clock_module_.load(),
            now.chosen,
            pair_display_.load(),
            catalog_revision_.load(),
            making_key_.load(),
            output_failed_.load(),
            led_kept_.load(),
            action_soon_.load(),
        };
    }

private:
    struct Play {
        bool requested;
        bool chosen;  // Refusing power leaves it: a garland unplugged for now is no change of mind.
        uint8_t brightness;
        uint8_t speed;
        uint16_t effect;
        bool chosen_since_look;  // Here so that the schedule can yield to a choice, see apply_scheduled.
        // Left of the countdown to off, 0 when not counting. Here so that a step turning the garland
        // on or off ends the countdown it found, and never one begun after it.
        uint8_t off_seconds;
    };

    static constexpr uint32_t pack(const Play& play) {
        return uint32_t{play.effect} | uint32_t{play.brightness} << 10 | uint32_t{play.speed} << 17 |
               uint32_t{play.requested} << 22 | uint32_t{play.chosen} << 23 | uint32_t{play.chosen_since_look} << 24 |
               uint32_t{play.off_seconds} << 25;
    }

    static constexpr Play unpack(uint32_t word) {
        return Play{(word >> 22 & 1) != 0, (word >> 23 & 1) != 0, static_cast<uint8_t>(word >> 10 & 0x7F), static_cast<uint8_t>(word >> 17 & 0x1F),
                    static_cast<uint16_t>(word & 0x3FF), (word >> 24 & 1) != 0, static_cast<uint8_t>(word >> 25 & 7)};
    }

    static constexpr void apply_parts(Play& play, uint8_t brightness_percent, uint8_t speed_x4, int effect, int power) {
        if (brightness_percent != 0) {
            play.brightness = brightness_percent;
        }
        if (speed_x4 != 0) {
            play.speed = speed_x4;
        }
        if (effect >= 0) {
            play.effect = static_cast<uint16_t>(effect);
        }
        if (power >= 0) {
            play.off_seconds = 0;
            order_on(play, power != 0);
        }
    }

    static constexpr void order_on(Play& play, bool on) {
        play.requested = on;
        play.chosen = on;
    }

    Play play() const { return unpack(play_.load()); }

    // True if a countdown was running, and is now not.
    bool end_countdown() {
        bool was_counting = false;
        change_play([&was_counting](Play& play) {
            was_counting = play.off_seconds != 0;
            play.off_seconds = 0;
        });
        return was_counting;
    }

    template <typename Change>
    void change_play(Change&& change) {
        uint32_t word = play_.load();
        uint32_t updated = 0;
        do {
            Play next = unpack(word);
            change(next);
            updated = pack(next);
        } while (!play_.compare_exchange_weak(word, updated));
    }

    void notify_power_listener() const {
        void (*const listener)() = power_listener_.load();
        if (listener != nullptr) {
            listener();
        }
    }

    // Word by word: the owner's tick arriving mid-read mixes old and new for one read at most.
    // Native-width words, which a 32-bit chip reads without a hidden lock.
    EffectMask favorites() const {
        EffectMask mask;
        for (size_t i = 0; i < EffectMask::kWords; ++i) {
            mask.set_word(i, uint64_t{favorites_[i * 2].load()} | uint64_t{favorites_[i * 2 + 1].load()} << 32);
        }
        return mask;
    }

    bool is_consumed(ButtonId button) const {
        return (consumed_buttons_.load() & (1u << static_cast<uint8_t>(button))) != 0;
    }

    const uint16_t fallback_led_count_;
    std::atomic<uint32_t> off_deadline_ms_{0};
    std::atomic<uint8_t> consumed_buttons_{0};
    std::atomic<uint32_t> activity_{0};
    std::atomic<uint32_t> owner_choice_ms_{0};
    std::atomic<uint32_t (*)()> choice_clock_{nullptr};
    std::atomic<SaverKind> saver_kind_{SaverKind::Starfield};
    std::atomic<uint16_t> saver_delay_seconds_{kSaverDelayDefaultSeconds};
    std::atomic<uint8_t> screen_percent_{kScreenBrightnessDefault};
    std::atomic<uint8_t> saver_percent_{kSaverBrightnessDefault};
    std::atomic<bool> saver_active_{false};
    std::atomic<bool> setup_network_{false};
    std::atomic<uint8_t> update_percent_{kNoUpdate};
    std::atomic<bool> clock_module_{false};
    std::atomic<uint32_t> play_{pack(Play{false, false, 50, kNormalSpeed, 0, false, 0})};
    std::atomic<void (*)()> power_listener_{nullptr};
    std::atomic<uint16_t> catalog_revision_{0};
    std::atomic<bool> lighting_paused_{false};
    std::atomic<bool> making_key_{false};
    std::atomic<uint16_t> pair_display_{kPairDisplayNone};
    std::atomic<uint16_t> led_setting_{kLedCountAuto};
    std::atomic<bool> rail_on_{false};
    std::atomic<bool> output_failed_{false};
    std::atomic<uint16_t> led_kept_{0};
    std::atomic<bool> action_soon_{false};
    std::atomic<uint16_t> effect_count_{0};
    std::array<std::atomic<uint32_t>, EffectMask::kWords * 2> favorites_{};
    std::atomic<ProbeStatus> probe_{ProbeStatus::NotChecked};
    std::atomic<uint16_t> led_estimate_{0};
    std::atomic<uint32_t> station_ip_{0};
};

}  // namespace garland
