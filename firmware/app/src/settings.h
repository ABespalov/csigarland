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

// Whether NVS came up and is still in use, which the Wi-Fi driver needs to keep data of its own there.
// False when it did not come up, or when settings_init erased it and that, or bringing it up again, failed.
bool settings_storage_ready();

// This board's setup network password, made once, and made again at each start that cannot save it,
// so there is always one, whatever came of settings_init. Never logged.
const char* settings_ap_password();

// The LED count, power budget, favourites and screen settings, in settings_looks.cpp. Each read tells none saved, Missing,
// from one there and not read, Unreadable, which a save would write over: see looks.h.
// The saved LED count setting into out, kLedCountAuto if there is none or it is out of range.
StoredRead settings_read_led_count(uint16_t& out);
esp_err_t settings_save_led_count(uint16_t setting);

// The text of the favourites, empty unless Loaded. Missing when nothing was ever saved, which is different
// from an empty list.
StoredRead settings_read_favorites(char* out, size_t out_size);
esp_err_t settings_save_favorites(const char* text);

// The playlist as playlist_to_text writes it. False when none was ever saved, or it could not be read.
bool settings_load_playlist(char* out, size_t out_size);
// The same, telling the two apart: ESP_ERR_NVS_NOT_FOUND when none was ever saved, any other error when
// one may be there and could not be read, which is no empty list. revision, if given, is the list's.
esp_err_t settings_read_playlist(char* out, size_t out_size, uint32_t* revision = nullptr);
// Saves text, with whether the list it replaces was left playing, in one write. With expected, only if the
// list is still of that revision: ESP_ERR_INVALID_STATE when it was replaced since. revision, if given,
// is the new list's. Revisions count the saves since the start.
esp_err_t settings_save_playlist(const char* text, const uint32_t* expected = nullptr, uint32_t* revision = nullptr);

// A POSIX TZ string. Missing when none was ever chosen, Unreadable when one may be there and was not read.
StoredRead settings_read_timezone(char* out, size_t out_size);
esp_err_t settings_save_timezone(const char* zone);

// Returns how many actions were loaded. Invalid entries are dropped, not fatal.
size_t settings_load_schedule(ScheduleEntry* out, size_t max_entries);
esp_err_t settings_save_schedule(const ScheduleEntry* entries, size_t count);

// Leaves what out holds in place for whatever was not saved, is not valid, or could not be read.
StoredRead settings_read_display(DisplaySettings& out);
esp_err_t settings_save_display(const DisplaySettings& settings);
// The same for the LEDs' power budget, whose defaults a garland saved by 1.1.1 or before reads. A record there that
// holds no valid settings gives the most cautious ones in their place, and invalid says so: see power_from_record.
StoredRead settings_read_power(PowerSettings& out, bool& invalid);
esp_err_t settings_save_power(const PowerSettings& settings);

// The last effect, brightness and speed. False when none were saved. The fields are as
// stored: the caller checks them against this firmware's catalog and ranges.
bool settings_load_remembered(StoredSettings& out);
// The same, telling none saved, ESP_ERR_NVS_NOT_FOUND, from one there and not read.
esp_err_t settings_read_remembered(StoredSettings& out);
// The schedule into out, and how many, as settings_load_schedule; telling none saved, ESP_OK with 0,
// from a list or its rises there and not read, which is no empty list.
esp_err_t settings_read_schedule(ScheduleEntry* out, size_t max_entries, size_t& count);
// At a start, once the schedule has been read: rewrites the rise marks to hold the list in force alone,
// where a save cut short, or one whose last write failed, left a mark of another list beside it.
esp_err_t settings_tidy_schedule_rises();
// How the playlist goes with what is remembered: playing, stopped, or as it was saved with the value
// this replaces, as when a choice is given its time and nothing else changes.
enum class PlaylistMark : uint8_t { Stopped, Playing, AsSaved };

// playlist: whether the playlist was playing, marked in its own record with this value, as remembered's
// layout is 1.0.0's. Fails, to be tried again, if the record or remembered is there and cannot be read.
esp_err_t settings_save_remembered(const char* effect_id, uint8_t brightness_percent, uint8_t speed_x4, bool playing, int64_t choice_epoch,
                                   PlaylistMark playlist);
// Whether the playlist was playing when remembered was last saved: false once anything, 1.0.0 among
// them, has saved remembered since.
bool settings_load_playlist_playing();
// The same, telling a reading that failed from a playlist not left playing.
esp_err_t settings_read_playlist_playing(bool& playing);
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

// The browsers allowed on the portal. Only hashes of their tokens, in encrypted NVS. out is empty unless
// Loaded, and Unreadable says a list may be there all the same: see auth_list_read.
StoredRead settings_load_auth(AuthDevice* out, size_t count);
esp_err_t settings_save_auth(const AuthDevice* devices, size_t count);

// The portal's own keys and certificates, and the owner's one domain for them.
// Missing only where flash says there is none: a read that failed says Unreadable.
StoredRead settings_load_tls(TlsIdentity& out);
esp_err_t settings_save_tls(const TlsIdentity& identity);
void settings_load_tls_starts(TlsStarts& out);
esp_err_t settings_save_tls_starts(const TlsStarts& starts);
// Missing, with out empty, only where flash says none was chosen.
StoredRead settings_load_tls_domain(char* out, size_t out_size);
esp_err_t settings_save_tls_domain(const char* domain);

}  // namespace garland
