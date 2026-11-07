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
#include <mutex>

#include "power_model.h"

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
    bool rise;                  // The turn on asked for rises over a minute, from the schedule.
    bool playlist;              // The playlist is playing: the lighting task moves it on.
    uint8_t playlist_at;        // Where what plays stands in the playlist, from 1, or 0 when not in it.
    uint8_t playlist_size;      // How many effects the playlist holds.
    uint8_t playlist_mark;      // Counts the starts of the playlist, modulo 4.

    bool operator==(const Snapshot&) const = default;
};

// The playlist's effects, by catalog position, and how long each plays.
constexpr size_t kMaxPlaylistEffects = 16;
constexpr uint16_t kPlaylistDwellMinSeconds = 10;
constexpr uint16_t kPlaylistDwellMaxSeconds = 3600;
constexpr uint16_t kPlaylistDwellDefaultSeconds = 60;

constexpr bool playlist_dwell_is_valid(int seconds) {
    return seconds >= kPlaylistDwellMinSeconds && seconds <= kPlaylistDwellMaxSeconds;
}

// The effect after current in the list, or the first when current is not in it. -1 for an empty list.
constexpr int playlist_next(const uint16_t* effects, size_t count, uint16_t current) {
    if (count == 0) {
        return -1;
    }
    for (size_t i = 0; i < count; ++i) {
        if (effects[i] == current) {
            return effects[(i + 1) % count];
        }
    }
    return effects[0];
}

// What a power step asks for: as it is, off, on, or on rising slowly from dark.
constexpr int kPowerKeep = -1;
constexpr int kPowerOff = 0;
constexpr int kPowerOn = 1;
constexpr int kPowerRise = 2;

// All of it in the 31 bits of one word: an effect in 10, brightness in 7, speed in 5.
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

    // From the portal: any effect, and it turns the garland on to show it. A choice of one effect
    // ends the playlist.
    void activate_effect(uint16_t index) {
        {
            // A choice of what plays goes under the lock a late resume holds, so that one never comes between
            // the resume's look at it and its change: see resume_playlist_unless_chosen.
            const std::lock_guard<std::mutex> guard(playlist_lock_);
            note_owner_play();
            change_play([index](Play& play) {
                play.off_seconds = 0;
                play.effect = index;
                play.playlist = false;
                order_on(play, true);
            });
        }
        notify_power_listener();
    }

    // The list the playlist plays, which the lighting task reads as it moves on. Word by word, like
    // the favourites: a change arriving mid-read mixes old and new for one step at most. Under
    // playlist_lock_, as are the starts, so a start never reads a list that is then emptied before it
    // takes: see apply_scheduled.
    void set_playlist(const uint16_t* effects, size_t count, uint16_t dwell_seconds) {
        const std::lock_guard<std::mutex> guard(playlist_lock_);
        count = count > kMaxPlaylistEffects ? kMaxPlaylistEffects : count;
        for (size_t i = 0; i < count; ++i) {
            playlist_effects_[i].store(effects[i]);
        }
        playlist_dwell_seconds_.store(dwell_seconds);
        playlist_count_.store(static_cast<uint8_t>(count));
        if (count == 0) {
            change_play([](Play& play) { play.playlist = false; });
        }
    }
    // Copies the list into out, which holds kMaxPlaylistEffects, and returns how many.
    size_t playlist(uint16_t* out) const {
        const size_t count = playlist_count_.load();
        for (size_t i = 0; i < count; ++i) {
            out[i] = playlist_effects_[i].load();
        }
        return count;
    }
    uint16_t playlist_dwell_seconds() const { return playlist_dwell_seconds_.load(); }

    // When the playlist goes on to its next effect, in ms of the uptime clock, wrapping as it does; 0 while it
    // does not play, lit. The Lighting task, which times it, tells; the Scheduler24 screensaver says it.
    void set_playlist_next_ms(uint32_t at_ms) { playlist_next_ms_.store(at_ms); }
    uint32_t playlist_next_ms() const { return playlist_next_ms_.load(); }

    // The owner starts the playlist, or stops it and keeps what plays. Starting turns the garland
    // on at the list's first effect. False for an empty list.
    bool play_playlist(bool on) {
        const std::lock_guard<std::mutex> guard(playlist_lock_);
        uint16_t effects[kMaxPlaylistEffects] = {};
        const size_t count = playlist(effects);
        if (on && count == 0) {
            return false;
        }
        note_owner_play();
        const uint16_t first = effects[0];
        change_play([on, first](Play& play) {
            play.playlist = on;
            if (on) {
                play.off_seconds = 0;
                play.effect = first;
                count_start(play);
                order_on(play, true);
            }
        });
        if (on) {
            notify_power_listener();
        }
        return true;
    }

    // Where an effect stands in the playlist, from 1, or 0 when it is not in it.
    uint8_t playlist_place(uint16_t effect) const {
        const size_t count = playlist_count_.load();
        for (size_t i = 0; i < count; ++i) {
            if (playlist_effects_[i].load() == effect) {
                return static_cast<uint8_t>(i + 1);
            }
        }
        return 0;
    }

    // At a start, read late, as it was left: unless the owner has chosen what plays since the start, an
    // effect or the playlist, by a button, a page or a program, or a scheduled step has, either of which
    // outranks what was left. A change of only the power, brightness or speed says nothing of what plays,
    // and leaves it to come back. Under the lock every choice of what plays takes, so none comes between
    // the look at its mark and the change. True when it plays on.
    bool resume_playlist_unless_chosen() {
        const std::lock_guard<std::mutex> guard(playlist_lock_);
        if (playlist_count_.load() == 0) {
            return false;
        }
        const uint16_t first = playlist_effects_[0].load();
        bool resumed = false;
        change_play([this, first, &resumed](Play& play) {
            resumed = !owner_play_.load() && !scheduled_play_.load();
            if (resumed) {
                play.playlist = true;
                play.effect = first;
                count_start(play);
            }
        });
        return resumed;
    }

    // What a start brings back, all at once, as apply_action takes it: unless the owner has chosen anything
    // since the start, by a button, a page or a program, which is newer. A choice notes itself before it
    // changes anything, so one that comes after the look still comes after this, and stands. Read late,
    // with tasks running, a check made apart from the change would leave room for a choice in between.
    // The playlist stopped, an effect gone from the catalog too: whether it was left playing is resumed apart,
    // and one a scheduled step started meanwhile is that step's, which the replay does again after this.
    // True when it was put on.
    bool restore_unless_chosen(uint8_t brightness_percent, uint8_t speed_x4, int effect, int power) {
        bool restored = false;
        change_play([&](Play& play) {
            restored = !owner_chose_.load();
            if (restored) {
                apply_parts(play, brightness_percent, speed_x4, effect, power);
                play.playlist = false;
            }
        });
        if (restored && power >= 0) {
            notify_power_listener();
        }
        return restored;
    }
    // The remembered effect, found late, in place of expected, played for it meanwhile: unless what plays was
    // chosen since, by the owner or a step, or the playlist plays. See PendingEffect. True when put on.
    bool restore_effect_unless_chosen(uint16_t expected, uint16_t effect) {
        bool restored = false;
        change_play([&](Play& play) {
            restored = !owner_play_.load() && !scheduled_play_.load() && !play.playlist && play.effect == expected;
            play.effect = restored ? effect : play.effect;
        });
        return restored;
    }

    // From the lighting task: on to next, only if the playlist still plays from expected, and from the
    // start the task timed it by, mark. Anything chosen since is newer, and stands, and a start since,
    // though at the same effect, has its first effect to play whole.
    bool advance_playlist(uint16_t expected, uint16_t next, uint8_t mark) {
        uint32_t word = play_.load();
        uint32_t updated = 0;
        do {
            Play moved = unpack(word);
            if (!moved.playlist || moved.effect != expected || moved.start_mark != mark) {
                return false;
            }
            moved.effect = next;
            updated = pack(moved);
        } while (!play_.compare_exchange_weak(word, updated));
        return true;
    }

    // From one effect to the next: how long the old one's last frame takes to give way.
    void set_cross_fade_ms(uint16_t ms) { cross_fade_ms_.store(ms); }
    uint16_t cross_fade_ms() const { return cross_fade_ms_.load(); }

    // Only what it names, all at once. Zero, or a negative effect or kPowerKeep, leaves that part as it is.
    void apply_action(uint8_t brightness_percent, uint8_t speed_x4, int effect, int power) {
        change_play([=](Play& play) { apply_parts(play, brightness_percent, speed_x4, effect, power); });
        if (power >= 0) {
            notify_power_listener();
        }
    }

    // From the schedule, which worked this out after take_owner_choice. A choice since then is
    // newer, so nothing changes and it says so. In the same word, so nothing can come in between.
    // With playlist, it starts the playlist at its first effect, in place of effect, if it has effects,
    // and with none takes effect_without and power_without, what the step comes to then, see
    // ResolvedAction::with_playlist. Decided here, on the one reading of the list it starts from: a list
    // emptied meanwhile cannot leave the step half of each. had_playlist, when given, says what that reading
    // found, so that what the step did is told from the list it was decided on, never a later one.
    bool apply_scheduled(uint8_t brightness_percent, uint8_t speed_x4, int effect, int power, bool playlist = false,
                         int effect_without = -1, int power_without = kPowerKeep, bool* had_playlist = nullptr) {
        const std::lock_guard<std::mutex> guard(playlist_lock_);
        uint16_t effects[kMaxPlaylistEffects] = {};
        const bool starts = playlist && this->playlist(effects) > 0;
        if (had_playlist != nullptr) {
            *had_playlist = starts;
        }
        if (playlist && !starts) {
            effect = effect_without;
            power = power_without;
        }
        uint32_t word = play_.load();
        uint32_t updated = 0;
        do {
            Play next = unpack(word);
            if (next.chosen_since_look) {
                return false;
            }
            apply_parts(next, brightness_percent, speed_x4, starts ? effects[0] : effect, power);
            next.playlist = next.playlist || starts;
            if (starts) {
                count_start(next);
            }
            updated = pack(next);
        } while (!play_.compare_exchange_weak(word, updated));
        // Only once it took, and still under the lock, which a resume of what was left takes too, so none
        // comes between: a step that chooses what plays is newer than what was left, and one that gave way to
        // the owner chose nothing.
        if (starts || effect >= 0) {
            scheduled_play_.store(true);
        }
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
        owner_chose_.store(true);
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
    // Whether the owner has chosen anything since the start.
    bool owner_chose() const { return owner_chose_.load(); }

    // A choice of what plays, an effect or the playlist, started or stopped: one of the power, brightness or
    // speed alone says nothing of it.
    void note_owner_play() {
        owner_play_.store(true);
        note_owner_choice();
    }
    // Whether the owner has chosen what plays since the start.
    bool owner_chose_play() const { return owner_play_.load(); }

    // Whether a scheduled step has chosen what plays, an effect or the playlist, since the start.
    bool scheduled_play() const { return scheduled_play_.load(); }

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
            const std::lock_guard<std::mutex> guard(playlist_lock_);
            note_owner_play();
            change_play([&](Play& play) {
                play.effect = next_favorite(play.effect, mask, count);
                play.playlist = false;
            });
        }
    }

    // With a playlist set, starts it, or stops it and keeps what plays. With none, does nothing.
    void long_press_effect() {
        if (is_consumed(ButtonId::Effect) || !play().requested || playlist_count_.load() == 0) {
            return;
        }
        play_playlist(!play().playlist);
    }

    // First in the button's own cycle, which is the catalog's first effect unless
    // that one is not a favourite. A choice of one effect, so the playlist stops.
    void double_click_effect() {
        if (!is_consumed(ButtonId::Effect) && play().requested) {
            const std::lock_guard<std::mutex> guard(playlist_lock_);
            note_owner_play();
            change_play([](Play& play) { play.playlist = false; });
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

    // The LEDs' power budget, valid settings only. One word, which the Lighting task reads each frame, with
    // whether they are the owner's, read or saved, or still the defaults a start begins with: one known bit
    // above the settings, which a reading or a save sets with them.
    void set_power(const PowerSettings& settings) { power_.store(pack_power(settings) | kPowerKnownBit); }
    PowerSettings power() const { return unpack_power(power_.load()); }
    struct PowerNow {
        PowerSettings settings;
        bool known;
    };
    PowerNow power_now() const {
        const uint32_t word = power_.load();
        return PowerNow{unpack_power(word), (word & kPowerKnownBit) != 0};
    }

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
            now.rise,
            now.playlist,
            playlist_place(now.effect),
            playlist_count_.load(),
            now.start_mark,
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
        bool rise;      // The turn on asked for rises over a minute. Only a scheduled step sets it.
        bool playlist;  // The playlist plays. A choice of one effect, by anyone, ends it.
        // Counts the starts of the playlist, modulo 4, in the same word, so the lighting task times its
        // first effect from then, though that effect may have been playing already. Two bits, so that two
        // or three starts between two looks of the task still show.
        uint8_t start_mark;
    };

    static constexpr void count_start(Play& play) { play.start_mark = static_cast<uint8_t>((play.start_mark + 1) & 3); }

    static constexpr uint32_t pack(const Play& play) {
        return uint32_t{play.effect} | uint32_t{play.brightness} << 10 | uint32_t{play.speed} << 17 |
               uint32_t{play.requested} << 22 | uint32_t{play.chosen} << 23 | uint32_t{play.chosen_since_look} << 24 |
               uint32_t{play.off_seconds} << 25 | uint32_t{play.rise} << 28 | uint32_t{play.playlist} << 29 |
               uint32_t{play.start_mark & 3u} << 30;
    }

    static constexpr Play unpack(uint32_t word) {
        return Play{(word >> 22 & 1) != 0, (word >> 23 & 1) != 0, static_cast<uint8_t>(word >> 10 & 0x7F), static_cast<uint8_t>(word >> 17 & 0x1F),
                    static_cast<uint16_t>(word & 0x3FF), (word >> 24 & 1) != 0, static_cast<uint8_t>(word >> 25 & 7),
                    (word >> 28 & 1) != 0, (word >> 29 & 1) != 0, static_cast<uint8_t>(word >> 30 & 3)};
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
            play.playlist = false;
        }
        if (power >= 0) {
            play.off_seconds = 0;
            order_on(play, power != kPowerOff);
            play.rise = power == kPowerRise;
        }
    }

    // Any order but a scheduled rise turns on at the usual pace.
    static constexpr void order_on(Play& play, bool on) {
        play.requested = on;
        play.chosen = on;
        play.rise = false;
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
    // Set once the owner has chosen anything since the start: the moment above may be 0 with no clock.
    std::atomic<bool> owner_chose_{false};
    std::atomic<bool> owner_play_{false};
    // Set once a scheduled step has chosen what plays, an effect or the playlist, since the start.
    std::atomic<bool> scheduled_play_{false};
    std::atomic<uint32_t (*)()> choice_clock_{nullptr};
    std::atomic<SaverKind> saver_kind_{SaverKind::Starfield};
    std::atomic<uint16_t> saver_delay_seconds_{kSaverDelayDefaultSeconds};
    std::atomic<uint8_t> screen_percent_{kScreenBrightnessDefault};
    std::atomic<uint8_t> saver_percent_{kSaverBrightnessDefault};
    static constexpr uint32_t kPowerKnownBit = 1U << 31;
    // The defaults, not known: a start takes the owner's settings, or that there are none, before the rail.
    std::atomic<uint32_t> power_{pack_power(PowerSettings{})};
    std::atomic<bool> saver_active_{false};
    std::atomic<bool> setup_network_{false};
    std::atomic<uint8_t> update_percent_{kNoUpdate};
    std::atomic<bool> clock_module_{false};
    std::atomic<uint32_t> play_{pack(Play{false, false, 50, kNormalSpeed, 0, false, 0, false, false, false})};
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
    std::array<std::atomic<uint16_t>, kMaxPlaylistEffects> playlist_effects_{};
    std::atomic<uint8_t> playlist_count_{0};
    // Held by what changes the list and by what starts it from what it reads, never by the lighting
    // task, which only moves on by advance_playlist.
    std::mutex playlist_lock_;
    std::atomic<uint16_t> playlist_dwell_seconds_{kPlaylistDwellDefaultSeconds};
    std::atomic<uint32_t> playlist_next_ms_{0};
    std::atomic<uint16_t> cross_fade_ms_{1000};
    std::array<std::atomic<uint32_t>, EffectMask::kWords * 2> favorites_{};
    std::atomic<ProbeStatus> probe_{ProbeStatus::NotChecked};
    std::atomic<uint16_t> led_estimate_{0};
    std::atomic<uint32_t> station_ip_{0};
};

}  // namespace garland
