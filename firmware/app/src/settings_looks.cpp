/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The LED count, power budget, favourites and screen settings in NVS, telling none saved from one not read
 * Contributors: None
 */

#include "settings.h"
#include "settings_nvs.h"

namespace garland {
namespace {

const char* const kLedCountKey = "led_count";
// A new key, so the catalog's new default favourites replace what earlier firmware saved.
const char* const kFavoritesKey = "favorites_v2";
const char* const kSaverKindKey = "saver_kind";
const char* const kSaverDelayKey = "saver_delay";
const char* const kScreenLevelKey = "screen_level";
const char* const kSaverLevelKey = "saver_level";
const char* const kSupplyKey = "supply_ma";
const char* const kLedWhiteKey = "led_white_ma";
const char* const kLedIdleKey = "led_idle_ma";
const char* const kMarginKey = "power_margin";

// The settings opened to read, or why not, closed when done with.
class Reading {
public:
    Reading() : opened_(nvs_open(kSettingsNamespace, NVS_READONLY, &handle_)) {}
    ~Reading() {
        if (opened_ == ESP_OK) {
            nvs_close(handle_);
        }
    }
    Reading(const Reading&) = delete;
    Reading& operator=(const Reading&) = delete;

    // What came of getting a key with get, which writes it on success, as stored_read counts it.
    template <typename Get>
    StoredRead read(Get get) const {
        return stored_read(nvs_answer(opened_), opened_ == ESP_OK ? nvs_answer(get(handle_)) : NvsAnswer::Failed);
    }

private:
    nvs_handle_t handle_ = 0;
    esp_err_t opened_;
};

template <typename Value, typename Valid>
StoredRead read_into(const Reading& reading, esp_err_t (*get)(nvs_handle_t, const char*, Value*), const char* key, Value& out,
                     Valid valid) {
    Value value{};
    const StoredRead read = reading.read([&](nvs_handle_t handle) { return get(handle, key, &value); });
    if (read == StoredRead::Loaded && valid(value)) {
        out = value;
    }
    return read;
}

template <typename Write>
esp_err_t write_with(Write write) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kSettingsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = write(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

}  // namespace

StoredRead settings_read_led_count(uint16_t& out) {
    out = kLedCountAuto;
    return read_into(Reading(), nvs_get_u16, kLedCountKey, out,
                     [](uint16_t setting) { return led_setting_is_valid(setting, CSIGARLAND_MAX_LEDS); });
}

esp_err_t settings_save_led_count(uint16_t setting) {
    return write_with([setting](nvs_handle_t handle) { return nvs_set_u16(handle, kLedCountKey, setting); });
}

StoredRead settings_read_favorites(char* out, size_t out_size) {
    out[0] = '\0';
    const StoredRead read = Reading().read([&](nvs_handle_t handle) {
        size_t length = out_size;
        return nvs_get_str(handle, kFavoritesKey, out, &length);
    });
    if (read != StoredRead::Loaded) {
        out[0] = '\0';
    }
    return read;
}

esp_err_t settings_save_favorites(const char* text) {
    return write_with([text](nvs_handle_t handle) { return nvs_set_str(handle, kFavoritesKey, text); });
}

StoredRead settings_read_display(DisplaySettings& out) {
    const Reading reading;
    uint8_t kind = static_cast<uint8_t>(out.saver);
    StoredRead read = read_into(reading, nvs_get_u8, kSaverKindKey, kind, [](uint8_t value) { return saver_kind_is_valid(value); });
    out.saver = static_cast<SaverKind>(kind);
    read = stored_read_with(read, read_into(reading, nvs_get_u16, kSaverDelayKey, out.saver_delay_seconds,
                                            [](uint16_t value) { return saver_delay_is_valid(value); }));
    read = stored_read_with(read, read_into(reading, nvs_get_u8, kScreenLevelKey, out.screen_percent,
                                            [](uint8_t value) { return backlight_percent_is_valid(value); }));
    return stored_read_with(read, read_into(reading, nvs_get_u8, kSaverLevelKey, out.saver_percent,
                                            [](uint8_t value) { return backlight_percent_is_valid(value); }));
}

esp_err_t settings_save_display(const DisplaySettings& settings) {
    return write_with([&settings](nvs_handle_t handle) {
        esp_err_t err = nvs_set_u8(handle, kSaverKindKey, static_cast<uint8_t>(settings.saver));
        if (err == ESP_OK) {
            err = nvs_set_u16(handle, kSaverDelayKey, settings.saver_delay_seconds);
        }
        if (err == ESP_OK) {
            err = nvs_set_u8(handle, kScreenLevelKey, settings.screen_percent);
        }
        return err == ESP_OK ? nvs_set_u8(handle, kSaverLevelKey, settings.saver_percent) : err;
    });
}

StoredRead settings_read_power(PowerSettings& out) {
    const Reading reading;
    StoredRead read = read_into(reading, nvs_get_u16, kSupplyKey, out.supply_ma, [](uint16_t value) { return supply_ma_is_valid(value); });
    read = stored_read_with(read, read_into(reading, nvs_get_u8, kLedWhiteKey, out.white_ma,
                                            [](uint8_t value) { return led_white_ma_is_valid(value); }));
    read = stored_read_with(read, read_into(reading, nvs_get_u8, kLedIdleKey, out.idle_ma,
                                            [](uint8_t value) { return led_idle_ma_is_valid(value); }));
    return stored_read_with(read, read_into(reading, nvs_get_u8, kMarginKey, out.margin_percent,
                                            [](uint8_t value) { return margin_percent_is_valid(value); }));
}

esp_err_t settings_save_power(const PowerSettings& settings) {
    return write_with([&settings](nvs_handle_t handle) {
        esp_err_t err = nvs_set_u16(handle, kSupplyKey, settings.supply_ma);
        if (err == ESP_OK) {
            err = nvs_set_u8(handle, kLedWhiteKey, settings.white_ma);
        }
        if (err == ESP_OK) {
            err = nvs_set_u8(handle, kLedIdleKey, settings.idle_ma);
        }
        return err == ESP_OK ? nvs_set_u8(handle, kMarginKey, settings.margin_percent) : err;
    });
}

}  // namespace garland
