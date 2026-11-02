/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Owner settings kept in NVS across power cycles
 * Contributors: None
 */

#include "settings.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "bootloader_random.h"
#include "esp_log.h"
#include "esp_random.h"
#include "garland_state.h"
#include "network_logic.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace garland {
namespace {

const char* const TAG = "settings";

char s_ap_password[kApPasswordLength + 1] = "";

const char* const kNamespace = "garland";
const char* const kEncryptedMarkerKey = "encrypted";
const char* const kApPasswordKey = "ap_password";
const char* const kLedCountKey = "led_count";
// A new key, so the catalog's new default favourites replace what earlier firmware saved.
const char* const kFavoritesKey = "favorites_v2";
const char* const kTimeZoneKey = "time_zone";
// The version is in the key: the blob is ScheduleEntry's memory layout.
const char* const kScheduleKey = "schedule_v1";
const char* const kSaverKindKey = "saver_kind";
const char* const kSaverDelayKey = "saver_delay";
const char* const kScreenLevelKey = "screen_level";
const char* const kSaverLevelKey = "saver_level";
const char* const kRememberedKey = "remembered";
const char* const kWifiSsidKey = "wifi_ssid";
const char* const kHostnameKey = "hostname";
const char* const kAuthKey = "auth";
const char* const kTlsKey = "tls_v1";
const char* const kTlsStartsKey = "tls_starts";
const char* const kTlsDomainKey = "tls_domain";
const char* const kWifiPasswordKey = "wifi_pass";
const char* const kLastEpochKey = "last_epoch";
const char* const kRtcUnfinishedKey = "rtc_unfinished";

bool read_string(const char* key, char* out, size_t out_size) {
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    size_t length = out_size;
    const esp_err_t err = nvs_get_str(handle, key, out, &length);
    nvs_close(handle);
    return err == ESP_OK;
}

esp_err_t erase_and_init() {
    const esp_err_t err = nvs_flash_erase();
    return err == ESP_OK ? nvs_flash_init() : err;
}

// A firmware from before the encryption left its settings in the clear, the Wi-Fi
// password among them. Encrypted reads no longer see those entries, but their bytes stay
// on the flash until the pages are erased. So a partition without the marker is erased
// once. That is also all it costs on a new board, which has nothing to lose.
//
// Only a marker known to be missing means that. Any other failure to read says nothing about
// what the partition holds, so it stays, and the failure is reported.
esp_err_t purge_plain_settings() {
    nvs_handle_t handle = 0;
    uint8_t marker = 0;
    esp_err_t found = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (found == ESP_OK) {
        found = nvs_get_u8(handle, kEncryptedMarkerKey, &marker);
        nvs_close(handle);
    }
    if (found == ESP_OK) {
        return ESP_OK;
    }
    if (found != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "cannot read the settings: %s", esp_err_to_name(found));
        return found;
    }
    ESP_LOGW(TAG, "erasing settings that are not encrypted");
    esp_err_t err = erase_and_init();
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(handle, kEncryptedMarkerKey, 1);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

void load_ap_password() {
    if (read_string(kApPasswordKey, s_ap_password, sizeof(s_ap_password)) && ap_password_is_valid(s_ap_password)) {
        return;
    }
    // Without this the hardware source is not truly random until Wi-Fi is running.
    bootloader_random_enable();
    make_ap_password(s_ap_password, [] { return static_cast<uint8_t>(esp_random()); });
    bootloader_random_disable();

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, kApPasswordKey, s_ap_password);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        // The screen shows whatever is in use, so the network stays reachable; it just
        // changes on the next start.
        ESP_LOGE(TAG, "saving the setup password failed: %s", esp_err_to_name(err));
    }
}

}  // namespace

esp_err_t settings_init() {
    esp_err_t err = nvs_flash_init();
    // The two answers that mean the pages themselves are of no use: full of nothing, or written
    // by a later format. Anything else may be passing, and erasing would lose what is there.
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "settings unusable: %s", esp_err_to_name(err));
        err = erase_and_init();
    }
    if (err == ESP_OK) {
        err = purge_plain_settings();
    }
    if (err == ESP_OK) {
        load_ap_password();
    }
    return err;
}

const char* settings_ap_password() {
    return s_ap_password;
}

uint16_t settings_load_led_count() {
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return kLedCountAuto;
    }
    uint16_t setting = kLedCountAuto;
    const esp_err_t err = nvs_get_u16(handle, kLedCountKey, &setting);
    nvs_close(handle);
    if (err != ESP_OK || !led_setting_is_valid(setting, CSIGARLAND_MAX_LEDS)) {
        return kLedCountAuto;
    }
    return setting;
}

esp_err_t settings_save_led_count(uint16_t setting) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u16(handle, kLedCountKey, setting);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

bool settings_load_favorites(char* out, size_t out_size) {
    return read_string(kFavoritesKey, out, out_size);
}

esp_err_t settings_save_favorites(const char* text) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(handle, kFavoritesKey, text);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

bool settings_load_timezone(char* out, size_t out_size) {
    return read_string(kTimeZoneKey, out, out_size);
}

esp_err_t settings_save_timezone(const char* zone) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(handle, kTimeZoneKey, zone);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

size_t settings_load_schedule(ScheduleEntry* out, size_t max_entries) {
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return 0;
    }
    static std::array<ScheduleEntry, kMaxScheduleEntries> s_loaded;
    size_t bytes = sizeof(s_loaded);
    const esp_err_t err = nvs_get_blob(handle, kScheduleKey, s_loaded.data(), &bytes);
    nvs_close(handle);
    if (err != ESP_OK || bytes % sizeof(ScheduleEntry) != 0) {
        return 0;
    }
    size_t kept = 0;
    const size_t stored = bytes / sizeof(ScheduleEntry);
    for (size_t i = 0; i < stored && kept < max_entries; ++i) {
        const char* error = nullptr;
        if (entry_is_valid(s_loaded[i], error)) {
            out[kept++] = s_loaded[i];
        }
    }
    return kept;
}

esp_err_t settings_save_schedule(const ScheduleEntry* entries, size_t count) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    // An empty blob is valid NVS data and reads back as no actions.
    err = nvs_set_blob(handle, kScheduleKey, entries, count * sizeof(ScheduleEntry));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

void settings_load_display(DisplaySettings& out) {
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    uint8_t saved_kind = 0;
    uint16_t saved_delay = 0;
    uint8_t saved_screen = 0;
    uint8_t saved_saver = 0;
    if (nvs_get_u8(handle, kSaverKindKey, &saved_kind) == ESP_OK && saver_kind_is_valid(saved_kind)) {
        out.saver = static_cast<SaverKind>(saved_kind);
    }
    if (nvs_get_u16(handle, kSaverDelayKey, &saved_delay) == ESP_OK && saver_delay_is_valid(saved_delay)) {
        out.saver_delay_seconds = saved_delay;
    }
    if (nvs_get_u8(handle, kScreenLevelKey, &saved_screen) == ESP_OK && backlight_percent_is_valid(saved_screen)) {
        out.screen_percent = saved_screen;
    }
    if (nvs_get_u8(handle, kSaverLevelKey, &saved_saver) == ESP_OK && backlight_percent_is_valid(saved_saver)) {
        out.saver_percent = saved_saver;
    }
    nvs_close(handle);
}

esp_err_t settings_save_display(const DisplaySettings& settings) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(handle, kSaverKindKey, static_cast<uint8_t>(settings.saver));
    if (err == ESP_OK) {
        err = nvs_set_u16(handle, kSaverDelayKey, settings.saver_delay_seconds);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, kScreenLevelKey, settings.screen_percent);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, kSaverLevelKey, settings.saver_percent);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

bool settings_load_remembered(StoredSettings& out) {
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    size_t bytes = sizeof(out);
    const esp_err_t err = nvs_get_blob(handle, kRememberedKey, &out, &bytes);
    nvs_close(handle);
    if (err != ESP_OK || bytes != sizeof(out)) {
        out = StoredSettings{};
        return false;
    }
    out.effect_id[sizeof(out.effect_id) - 1] = '\0';
    return true;
}

int64_t settings_load_last_epoch() {
    nvs_handle_t handle = 0;
    int64_t epoch = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) == ESP_OK) {
        if (nvs_get_i64(handle, kLastEpochKey, &epoch) != ESP_OK) {
            epoch = 0;
        }
        nvs_close(handle);
    }
    return epoch;
}

esp_err_t settings_save_last_epoch(int64_t epoch) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_i64(handle, kLastEpochKey, epoch);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

bool settings_load_rtc_unfinished() {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    uint8_t unfinished = 0;
    if (err == ESP_OK) {
        err = nvs_get_u8(handle, kRtcUnfinishedKey, &unfinished);
        nvs_close(handle);
    }
    // Absent only ever means that no setting has been begun.
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return false;
    }
    return err != ESP_OK || unfinished != 0;
}

esp_err_t settings_save_rtc_unfinished(bool unfinished) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(handle, kRtcUnfinishedKey, unfinished ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t settings_save_remembered(const char* effect_id, uint8_t brightness_percent, uint8_t speed_x4, bool playing, int64_t choice_epoch) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    StoredSettings stored;
    stored.brightness_percent = brightness_percent;
    stored.speed_x4 = speed_x4;
    stored.playing = playing ? 1 : 0;
    stored.choice_epoch = choice_epoch;
    std::snprintf(stored.effect_id, sizeof(stored.effect_id), "%s", effect_id);
    err = nvs_set_blob(handle, kRememberedKey, &stored, sizeof(stored));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

bool settings_load_wifi(WifiCredentials& out) {
    out = WifiCredentials{};
    return read_string(kWifiSsidKey, out.ssid, sizeof(out.ssid)) && out.ssid[0] != '\0' &&
           read_string(kWifiPasswordKey, out.password, sizeof(out.password));
}

esp_err_t settings_save_wifi(const WifiCredentials& credentials) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(handle, kWifiSsidKey, credentials.ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, kWifiPasswordKey, credentials.password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

bool settings_load_hostname(char* out, size_t out_size) {
    return read_string(kHostnameKey, out, out_size);
}

esp_err_t settings_save_hostname(const char* name) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(handle, kHostnameKey, name);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

void settings_load_auth(AuthDevice* out, size_t count) {
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    size_t bytes = sizeof(AuthDevice) * count;
    // Of another size it is from another layout, and the browsers pair again.
    if (nvs_get_blob(handle, kAuthKey, out, &bytes) != ESP_OK || bytes != sizeof(AuthDevice) * count) {
        std::memset(out, 0, sizeof(AuthDevice) * count);
    }
    nvs_close(handle);
}

esp_err_t settings_save_auth(const AuthDevice* devices, size_t count) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(handle, kAuthKey, devices, sizeof(AuthDevice) * count);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

StoredRead settings_load_tls(TlsIdentity& out) {
    out = TlsIdentity{};
    nvs_handle_t handle = 0;
    const esp_err_t opened = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) {
        return StoredRead::Missing;
    }
    if (opened != ESP_OK) {
        return StoredRead::Unreadable;
    }
    size_t bytes = sizeof(out);
    const esp_err_t err = nvs_get_blob(handle, kTlsKey, &out, &bytes);
    nvs_close(handle);
    if (err == ESP_OK && bytes == sizeof(out)) {
        return StoredRead::Loaded;
    }
    out = TlsIdentity{};
    // Not there, or there in another size, which no build has written yet: that is no identity.
    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_LENGTH || err == ESP_OK) {
        return StoredRead::Missing;
    }
    return StoredRead::Unreadable;
}

esp_err_t settings_save_tls(const TlsIdentity& identity) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(handle, kTlsKey, &identity, sizeof(identity));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

void settings_load_tls_starts(TlsStarts& out) {
    out = TlsStarts{};
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    size_t bytes = sizeof(out);
    if (nvs_get_blob(handle, kTlsStartsKey, &out, &bytes) != ESP_OK || bytes != sizeof(out)) {
        out = TlsStarts{};
    }
    nvs_close(handle);
}

esp_err_t settings_save_tls_starts(const TlsStarts& starts) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(handle, kTlsStartsKey, &starts, sizeof(starts));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

void settings_load_tls_domain(char* out, size_t out_size) {
    if (out_size > 0 && !read_string(kTlsDomainKey, out, out_size)) {
        out[0] = '\0';
    }
}

esp_err_t settings_save_tls_domain(const char* domain) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(handle, kTlsDomainKey, domain);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t settings_clear_wifi() {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    // Both are tried whatever comes of the first, and one already gone is what was wanted.
    const esp_err_t ssid_err = nvs_erase_key(handle, kWifiSsidKey);
    const esp_err_t password_err = nvs_erase_key(handle, kWifiPasswordKey);
    err = nvs_commit(handle);
    nvs_close(handle);
    if (ssid_err != ESP_OK && ssid_err != ESP_ERR_NVS_NOT_FOUND) {
        return ssid_err;
    }
    if (password_err != ESP_OK && password_err != ESP_ERR_NVS_NOT_FOUND) {
        return password_err;
    }
    return err;
}

}  // namespace garland
