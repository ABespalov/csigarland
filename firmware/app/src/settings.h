/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Owner settings kept in NVS across power cycles
 * Contributors: None
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "auth_logic.h"
#include "effect_memory.h"
#include "esp_err.h"
#include "garland_state.h"
#include "schedule.h"
#include "tls_logic.h"

namespace garland {

struct WifiCredentials {
    char ssid[33];
    char password[65];
};

// Prepares NVS, which the Wi-Fi driver needs as well, and the setup network's password.
// Call once at startup, before anything else uses the ADC or Wi-Fi: making the password
// and, on a new board, the encryption key takes the hardware random source to itself.
//
// NVS is encrypted with a key derived from an eFuse key, which a new board burns here
// once and for good. A dump of the flash reads as noise, but this does not stop someone
// holding the board from flashing a program that asks the chip to decrypt it.
esp_err_t settings_init();

// This board's setup network password, made once. Never logged.
const char* settings_ap_password();

// The saved LED count setting, or kLedCountAuto if there is none or it is out of range.
uint16_t settings_load_led_count();
esp_err_t settings_save_led_count(uint16_t setting);

// False when nothing was ever saved, which is different from an empty list.
bool settings_load_favorites(char* out, size_t out_size);
esp_err_t settings_save_favorites(const char* text);

// A POSIX TZ string. False when none was ever chosen.
bool settings_load_timezone(char* out, size_t out_size);
esp_err_t settings_save_timezone(const char* zone);

// Returns how many actions were loaded. Invalid entries are dropped, not fatal.
size_t settings_load_schedule(ScheduleEntry* out, size_t max_entries);
esp_err_t settings_save_schedule(const ScheduleEntry* entries, size_t count);

// Leaves the defaults in place for whatever was not saved or is not valid.
void settings_load_display(DisplaySettings& out);
esp_err_t settings_save_display(const DisplaySettings& settings);

// The last effect, brightness and speed. False when none were saved. The fields are as
// stored: the caller checks them against this firmware's catalog and ranges.
bool settings_load_remembered(StoredSettings& out);
esp_err_t settings_save_remembered(const char* effect_id, uint8_t brightness_percent, uint8_t speed_x4, bool playing, int64_t choice_epoch);
// The time when a certificate was last found to need renewing, for a start with no clock yet.
int64_t settings_load_last_epoch();
esp_err_t settings_save_last_epoch(int64_t epoch);
// A setting of the clock module was begun and not seen through, so it may hold a mix of two times.
// Also true if that cannot be read: what is not known to be clean is not to be believed.
bool settings_load_rtc_unfinished();
esp_err_t settings_save_rtc_unfinished(bool unfinished);

bool settings_load_wifi(WifiCredentials& out);
esp_err_t settings_save_wifi(const WifiCredentials& credentials);
esp_err_t settings_clear_wifi();

// The name before .local. False when the owner has not chosen one.
bool settings_load_hostname(char* out, size_t out_size);
esp_err_t settings_save_hostname(const char* name);

// The browsers allowed on the portal. Only hashes of their tokens, in encrypted NVS.
void settings_load_auth(AuthDevice* out, size_t count);
esp_err_t settings_save_auth(const AuthDevice* devices, size_t count);

// The portal's own keys and certificates, and the owner's one domain for them.
// Missing only where flash says there is none: a read that failed says Unreadable.
StoredRead settings_load_tls(TlsIdentity& out);
esp_err_t settings_save_tls(const TlsIdentity& identity);
void settings_load_tls_starts(TlsStarts& out);
esp_err_t settings_save_tls_starts(const TlsStarts& starts);
void settings_load_tls_domain(char* out, size_t out_size);
esp_err_t settings_save_tls_domain(const char* domain);

}  // namespace garland
