/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The LED count, power budget, favourites and screen settings on the state, read again while they could not be
 * Contributors: None
 */

#include "looks.h"

#include <atomic>

#include "effect_catalog.h"
#include "effect_store.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "settings.h"

namespace garland {
namespace {

const char* const TAG = "looks";

constexpr int kReadAttempts = 3;
constexpr uint32_t kReadPauseMs = 50;
constexpr uint32_t kRetryMs = 5000;

// Each unread until a reading has said what is there, none among it: until then the state holds defaults.
std::atomic<bool> s_led_unread{true};
std::atomic<bool> s_favorites_unread{true};
std::atomic<bool> s_display_unread{true};
std::atomic<bool> s_power_unread{true};
// The favourites read name an effect the catalog lacks while uploaded effects are not all read: one of those
// may be it, so the mask on the state lacks it for now, and a save of that mask would lose its mark.
std::atomic<bool> s_favorites_partial{false};
uint32_t s_tried_ms = 0;

// Held across each reading put on the state, and each save that the reading must not come after.
SemaphoreHandle_t s_lock = nullptr;
StaticSemaphore_t s_lock_storage;

class Guard {
public:
    Guard() { xSemaphoreTake(s_lock, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(s_lock); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};

// Reads one onto the state if it is unread, under the lock: true once it is read, or was.
bool read_led(State& state) {
    if (!s_led_unread.load()) {
        return true;
    }
    uint16_t setting = kLedCountAuto;
    if (settings_read_led_count(setting) == StoredRead::Unreadable) {
        return false;
    }
    state.set_led_setting(setting);
    s_led_unread.store(false);
    return true;
}

// The favourites as flash held them when last read, under the lock: put on the catalog again once the
// uploaded effects they name are read, as nothing is saved over them meanwhile.
char s_favorites_text[kFavoritesTextBytes];

// The favourites read, or none saved, onto the catalog as it is now.
void take_favorites(State& state, bool loaded) {
    // Every saved effect may be gone from a newer catalog; then the defaults count again.
    const EffectMask saved = loaded ? favorites_from_text(s_favorites_text) : EffectMask{};
    state.set_favorites(saved.any() ? saved : catalog_default_favorites());
    s_favorites_partial.store(loaded && !effect_store_all_read() && favorites_name_unknown(s_favorites_text));
}

bool read_favorites(State& state) {
    if (!s_favorites_unread.load()) {
        return true;
    }
    const StoredRead read = settings_read_favorites(s_favorites_text, sizeof(s_favorites_text));
    if (read == StoredRead::Unreadable) {
        return false;
    }
    take_favorites(state, read == StoredRead::Loaded);
    s_favorites_unread.store(false);
    return true;
}

bool read_display(State& state) {
    if (!s_display_unread.load()) {
        return true;
    }
    DisplaySettings display;
    if (settings_read_display(display) == StoredRead::Unreadable) {
        return false;
    }
    state.set_saver(display.saver, display.saver_delay_seconds);
    state.set_backlight_levels(display.screen_percent, display.saver_percent);
    s_display_unread.store(false);
    return true;
}

// Read, or found to be none, which then takes the defaults, the limit of the releases before these settings,
// and either way known: until then the Lighting task keeps the rail off, as the defaults may allow more
// current than what the owner saved.
bool read_power(State& state) {
    if (!s_power_unread.load()) {
        return true;
    }
    PowerSettings power;
    if (settings_read_power(power) == StoredRead::Unreadable) {
        return false;
    }
    state.set_power(power);
    s_power_unread.store(false);
    return true;
}

// All four read, as far as they can be. True when none is left unread.
bool read_all(State& state) {
    const Guard guard;
    const bool led = read_led(state);
    const bool power = read_power(state);
    const bool favorites = read_favorites(state);
    return read_display(state) && led && power && favorites;
}

}  // namespace

void looks_load(State& state) {
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    // The defaults, which a reading that fails leaves in place, as one that finds none does.
    state.set_favorites(catalog_default_favorites());
    for (int attempt = 0; attempt < kReadAttempts; ++attempt) {
        if (read_all(state)) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(kReadPauseMs));
    }
    ESP_LOGE(TAG, "the LED count, power budget, favourites or screen settings could not be read: tried again every few seconds, and not saved over");
}

void looks_retry_if_unread(State& state, uint32_t now_ms) {
    const bool unread = s_led_unread.load() || s_power_unread.load() || s_favorites_unread.load() || s_display_unread.load();
    if (!unread || s_lock == nullptr || now_ms - s_tried_ms < kRetryMs) {
        return;
    }
    s_tried_ms = now_ms;
    if (read_all(state)) {
        ESP_LOGI(TAG, "the LED count, power budget, favourites and screen settings were read late");
    }
}

bool looks_favorites_read(State& state) {
    const Guard guard;
    return read_favorites(state);
}

bool looks_favorites_whole(State& state) {
    const Guard guard;
    if (!read_favorites(state)) {
        return false;
    }
    // The effects they named are read, or all are and the rest are gone: the list read, onto the catalog as it
    // is. From what was read, not flash again, whose reading could fail this time: nothing was saved meanwhile,
    // as a save waits for this. Other uploads not read yet keep nothing back.
    if (s_favorites_partial.load() && (effect_store_all_read() || !favorites_name_unknown(s_favorites_text))) {
        take_favorites(state, true);
    }
    return !s_favorites_unread.load() && !s_favorites_partial.load();
}

bool looks_display_read(State& state) {
    const Guard guard;
    return read_display(state);
}

bool looks_power_read(State& state) {
    const Guard guard;
    return read_power(state);
}

esp_err_t looks_save_power(State& state, const PowerSettings& settings) {
    const Guard guard;
    const esp_err_t err = settings_save_power(settings);
    if (err == ESP_OK) {
        state.set_power(settings);
        s_power_unread.store(false);
    }
    return err;
}

esp_err_t looks_save_led_count(State& state, uint16_t setting) {
    const Guard guard;
    const esp_err_t err = settings_save_led_count(setting);
    if (err == ESP_OK) {
        state.set_led_setting(setting);
        s_led_unread.store(false);
    }
    return err;
}

}  // namespace garland
