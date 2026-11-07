/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Owner settings kept in NVS across power cycles
 * Contributors: None
 */

#include "settings.h"
#include "settings_nvs.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "bootloader_random.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "garland_state.h"
#include "network_logic.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "playlist_logic.h"

namespace garland {
namespace {

const char* const TAG = "settings";

char s_ap_password[kApPasswordLength + 1] = "";
// NVS itself came up, which says nothing yet of whether what it holds can be read.
bool s_storage_ready = false;

const char* const kNamespace = kSettingsNamespace;
const char* const kEncryptedMarkerKey = "encrypted";
const char* const kApPasswordKey = "ap_password";
const char* const kTimeZoneKey = "time_zone";
// The version is in the key: the blob is ScheduleEntry's memory layout.
const char* const kScheduleKey = "schedule_v1";
// Which actions of schedule_v1 rise from dark, which 1.0.0 reads as on: the marks, see
// schedule_rise_marks. New after 1.0.0.
const char* const kScheduleRiseKey = "schedule_rises";
// The playlist and whether it was left playing, in one record: see playlist_record_make. New after 1.0.0.
const char* const kPlaylistRecordKey = "playlist_rec";
// What builds before the record kept apart, the text and a mark: the text is still read, and both are
// erased at the first save of a list.
const char* const kPlaylistTextKey = "playlist";
const char* const kPlaylistOldMarkKey = "playlist_with";
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

esp_err_t read_string_err(const char* key, char* out, size_t out_size) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }
    size_t length = out_size;
    err = nvs_get_str(handle, key, out, &length);
    nvs_close(handle);
    return err;
}

bool read_string(const char* key, char* out, size_t out_size) {
    return read_string_err(key, out, out_size) == ESP_OK;
}

// Held across each write that reads what another writes: the playlist's mark is made of remembered and
// the list, which the scheduler and the server save, and each reads the other's before it writes.
SemaphoreHandle_t s_playlist_lock = nullptr;
StaticSemaphore_t s_playlist_lock_storage;

// Counted from a random start at each start of the garland, as the schedule's is: a page or program that
// read the list before a restart holds a revision the new count does not meet, and its save is refused.
static uint32_t s_playlist_revision = 1;

class PlaylistGuard {
public:
    PlaylistGuard() {
        if (s_playlist_lock != nullptr) {
            xSemaphoreTake(s_playlist_lock, portMAX_DELAY);
        }
    }
    ~PlaylistGuard() {
        if (s_playlist_lock != nullptr) {
            xSemaphoreGive(s_playlist_lock);
        }
    }
    PlaylistGuard(const PlaylistGuard&) = delete;
    PlaylistGuard& operator=(const PlaylistGuard&) = delete;
};

// An erase of a partition in use takes it out of use first, so whatever fails here leaves no NVS at all.
esp_err_t erase_and_init() {
    esp_err_t err = nvs_flash_erase();
    if (err == ESP_OK) {
        err = nvs_flash_init();
    }
    s_storage_ready = err == ESP_OK;
    return err;
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
    // Before any task that saves starts: this runs first of all.
    if (s_playlist_lock == nullptr) {
        s_playlist_lock = xSemaphoreCreateMutexStatic(&s_playlist_lock_storage);
    }
    // Never 0, which a client sends for none.
    s_playlist_revision = esp_random() | 1;
    esp_err_t err = nvs_flash_init();
    // The two answers that mean the pages themselves are of no use: full of nothing, or written
    // by a later format. Anything else may be passing, and erasing would lose what is there.
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "settings unusable: %s", esp_err_to_name(err));
        err = erase_and_init();
    }
    s_storage_ready = err == ESP_OK;
    if (err == ESP_OK) {
        err = purge_plain_settings();
    }
    // Whatever came of the rest: the setup network, its password on the screen, is how a closed garland
    // whose settings cannot be read still takes the next update. One not saved changes at the next start.
    load_ap_password();
    return err;
}

bool settings_storage_ready() {
    return s_storage_ready;
}

const char* settings_ap_password() {
    return s_ap_password;
}

// The record of the playlist through handle, into record, and its length; for a garland that has only
// the text a build before the record saved, the text alone, as a record not left playing.
static esp_err_t read_playlist_record(nvs_handle_t handle, uint8_t* record, size_t& bytes) {
    bytes = kPlaylistRecordBytes;
    esp_err_t err = nvs_get_blob(handle, kPlaylistRecordKey, record, &bytes);
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }
    bytes = kPlaylistTextBytes;
    err = nvs_get_str(handle, kPlaylistTextKey, reinterpret_cast<char*>(record), &bytes);
    return err;
}

// The playlist as NVS holds it, with remembered, read in one go under the guard the caller holds, so that
// every decision is made of one picture of the two.
struct PlaylistNow {
    bool found = false;  // A list was saved.
    char text[kPlaylistTextBytes] = "";
    std::array<uint8_t, kPlaylistRecordBytes> record{};
    size_t record_bytes = 0;
    bool remembered_known = false;
    StoredSettings remembered;
    bool resumes = false;  // The list was left playing with remembered as it is.
};

// ESP_OK with what is there, or what is not, as a list never saved or nothing remembered yet; an error
// when either is there and cannot be read, which says nothing of whether the list plays. With
// list_only, remembered failing to read leaves the list read, as one marked for nothing: what the list
// holds does not hang on it.
static esp_err_t read_playlist(nvs_handle_t handle, PlaylistNow& now, bool list_only = false) {
    now = PlaylistNow{};
    esp_err_t err = read_playlist_record(handle, now.record.data(), now.record_bytes);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    size_t remembered_bytes = sizeof(now.remembered);
    err = nvs_get_blob(handle, kRememberedKey, &now.remembered, &remembered_bytes);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND && !list_only) {
        return err;
    }
    now.remembered_known = err == ESP_OK && remembered_bytes == sizeof(now.remembered);
    if (!playlist_record_read(now.record.data(), now.record_bytes, now.text, sizeof(now.text), now.remembered_known ? &now.remembered : nullptr,
                              now.resumes)) {
        return ESP_ERR_INVALID_SIZE;
    }
    now.found = true;
    return ESP_OK;
}

static PlaylistNow s_playlist_now;

esp_err_t settings_read_playlist(char* out, size_t out_size, uint32_t* revision) {
    const PlaylistGuard guard;
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        err = read_playlist(handle, s_playlist_now, true);
        nvs_close(handle);
    }
    if (err == ESP_OK && !s_playlist_now.found) {
        err = ESP_ERR_NVS_NOT_FOUND;
    }
    if (err == ESP_OK) {
        std::snprintf(out, out_size, "%s", s_playlist_now.text);
    }
    if (revision != nullptr) {
        *revision = s_playlist_revision;
    }
    return err;
}

bool settings_load_playlist(char* out, size_t out_size) {
    return settings_read_playlist(out, out_size) == ESP_OK;
}

esp_err_t settings_save_playlist(const char* text, const uint32_t* expected, uint32_t* revision) {
    const PlaylistGuard guard;
    if (expected != nullptr && *expected != s_playlist_revision) {
        return ESP_ERR_INVALID_STATE;
    }
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    // Whether the list it replaces was left playing, which the new one then is, if it names an effect: in
    // the same write as the list, so that no failure or power cut can part them. What cannot be read is
    // not written over.
    err = read_playlist(handle, s_playlist_now);
    static std::array<uint8_t, kPlaylistRecordBytes> s_record;
    size_t bytes = 0;
    if (err == ESP_OK) {
        const bool playing = s_playlist_now.found && s_playlist_now.resumes;
        bytes = playlist_record_make(text, playing ? &s_playlist_now.remembered : nullptr, playing ? 1 : 0, s_record.data(), s_record.size());
        err = bytes == 0 ? ESP_ERR_INVALID_SIZE : nvs_set_blob(handle, kPlaylistRecordKey, s_record.data(), bytes);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (err == ESP_OK) {
        ++s_playlist_revision;
        // What builds before the record kept: the record holds it all now. Nothing hangs on these.
        nvs_erase_key(handle, kPlaylistTextKey);
        nvs_erase_key(handle, kPlaylistOldMarkKey);
        nvs_commit(handle);
    }
    nvs_close(handle);
    if (revision != nullptr) {
        *revision = s_playlist_revision;
    }
    return err;
}
StoredRead settings_read_timezone(char* out, size_t out_size) {
    // Either step's NOT_FOUND is none saved, and any other failure one there and not read.
    return stored_read(NvsAnswer::Ok, nvs_answer(read_string_err(kTimeZoneKey, out, out_size)));
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

// What is saved beside schedule_v1 of the rises, into out, and its length: 0 for none. Room for the
// entries whole, as builds before the marks kept them.
constexpr size_t kRisesBytes = kMaxScheduleEntries * sizeof(ScheduleEntry);
// Into bytes, 0 for none saved; an error when they are there and cannot be read.
static esp_err_t load_rises(nvs_handle_t handle, uint8_t* out, size_t& bytes) {
    bytes = kRisesBytes;
    const esp_err_t err = nvs_get_blob(handle, kScheduleRiseKey, out, &bytes);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        bytes = 0;
        return ESP_OK;
    }
    return err;
}

esp_err_t settings_read_schedule(ScheduleEntry* out, size_t max_entries, size_t& count) {
    count = 0;
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    static std::array<ScheduleEntry, kMaxScheduleEntries> s_loaded;
    size_t bytes = sizeof(s_loaded);
    err = nvs_get_blob(handle, kScheduleKey, s_loaded.data(), &bytes);
    alignas(ScheduleEntry) static std::array<uint8_t, kRisesBytes> s_rises;
    size_t rises_bytes = 0;
    // The rises with the list, or neither: a list read without them would have its rises as plain turns on,
    // and the next save of another action would keep them so.
    const esp_err_t rises = err == ESP_OK ? load_rises(handle, s_rises.data(), rises_bytes) : ESP_OK;
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (rises != ESP_OK) {
        return rises;
    }
    if (bytes % sizeof(ScheduleEntry) != 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t kept = 0;
    const size_t stored = bytes / sizeof(ScheduleEntry);
    const uint32_t mask = schedule_rises_of(s_loaded.data(), stored, s_rises.data(), rises_bytes);
    for (size_t i = 0; i < stored && kept < max_entries; ++i) {
        s_loaded[i].power = schedule_power_as_read(s_loaded[i].power, (mask >> i & 1) != 0);
        const char* error = nullptr;
        if (entry_is_valid(s_loaded[i], error)) {
            out[kept++] = s_loaded[i];
        }
    }
    count = kept;
    return ESP_OK;
}

esp_err_t settings_tidy_schedule_rises() {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
    }
    static std::array<ScheduleEntry, kMaxScheduleEntries> s_stored;
    size_t bytes = sizeof(s_stored);
    err = nvs_get_blob(handle, kScheduleKey, s_stored.data(), &bytes);
    alignas(ScheduleEntry) static std::array<uint8_t, kRisesBytes> s_rises;
    size_t rises_bytes = 0;
    if (err == ESP_OK) {
        err = load_rises(handle, s_rises.data(), rises_bytes);
    }
    uint8_t tidy[kRiseMarksBytes];
    if (err == ESP_OK && bytes % sizeof(ScheduleEntry) == 0 &&
        schedule_rise_marks_tidy(s_stored.data(), bytes / sizeof(ScheduleEntry), s_rises.data(), rises_bytes, tidy)) {
        err = nvs_set_blob(handle, kScheduleRiseKey, tidy, sizeof(tidy));
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "the rise marks now hold the schedule in force alone");
        }
    }
    nvs_close(handle);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

size_t settings_load_schedule(ScheduleEntry* out, size_t max_entries) {
    size_t count = 0;
    return settings_read_schedule(out, max_entries, count) == ESP_OK ? count : 0;
}

esp_err_t settings_save_schedule(const ScheduleEntry* entries, size_t count) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    // As 1.0.0 reads it, rising steps as plain ones, and which rise apart: a firmware gone back to it
    // then turns them on, where it would take a step it does not know for off.
    static std::array<ScheduleEntry, kMaxScheduleEntries> s_saved;
    count = count > kMaxScheduleEntries ? kMaxScheduleEntries : count;
    for (size_t i = 0; i < count; ++i) {
        s_saved[i] = entries[i];
        s_saved[i].power = schedule_power_as_saved(entries[i].power);
    }
    const size_t saved_bytes = count * sizeof(ScheduleEntry);
    // The list it replaces, and which of its entries rise, so that the marks hold for either.
    // What cannot be read is not written over as if it were not there: its rises would go with it.
    static std::array<ScheduleEntry, kMaxScheduleEntries> s_before;
    size_t before_bytes = sizeof(s_before);
    err = nvs_get_blob(handle, kScheduleKey, s_before.data(), &before_bytes);
    if (err == ESP_ERR_NVS_NOT_FOUND || (err == ESP_OK && before_bytes % sizeof(ScheduleEntry) != 0)) {
        before_bytes = 0;
        err = ESP_OK;
    }
    alignas(ScheduleEntry) static std::array<uint8_t, kRisesBytes> s_rises;
    size_t rises_bytes = 0;
    if (err == ESP_OK) {
        err = load_rises(handle, s_rises.data(), rises_bytes);
    }
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    const uint32_t before_mask = schedule_rises_of(s_before.data(), before_bytes / sizeof(ScheduleEntry), s_rises.data(), rises_bytes);
    uint8_t marks[kRiseMarksBytes];
    schedule_rise_marks(schedule_rise_mask(entries, count), reinterpret_cast<const uint8_t*>(s_saved.data()), saved_bytes, before_mask,
                        reinterpret_cast<const uint8_t*>(s_before.data()), before_bytes, marks);
    // The marks first: NVS writes each blob whole at once, and nothing joins two into one, so the list
    // left after a failed or cut second write is the one before, and the marks hold for it too.
    err = nvs_set_blob(handle, kScheduleRiseKey, marks, sizeof(marks));
    // An empty blob is valid NVS data and reads back as no actions.
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, kScheduleKey, s_saved.data(), saved_bytes);
    }
    // Saved: the mark of the list before goes, as 1.0.0 may save those very bytes again one day, and a
    // mask that held for them then would turn its plain turns on into rises. Left there if this fails,
    // which is the least of it.
    if (err == ESP_OK) {
        uint8_t own[kRiseMarksBytes];
        schedule_rise_marks(schedule_rise_mask(entries, count), reinterpret_cast<const uint8_t*>(s_saved.data()), saved_bytes,
                            schedule_rise_mask(entries, count), reinterpret_cast<const uint8_t*>(s_saved.data()), saved_bytes, own);
        if (nvs_set_blob(handle, kScheduleRiseKey, own, sizeof(own)) != ESP_OK) {
            ESP_LOGW(TAG, "the mark of the list before stays: it holds only for those bytes");
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t settings_read_remembered(StoredSettings& out) {
    out = StoredSettings{};
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }
    size_t bytes = sizeof(out);
    err = nvs_get_blob(handle, kRememberedKey, &out, &bytes);
    nvs_close(handle);
    if (err == ESP_OK && bytes != sizeof(out)) {
        err = ESP_ERR_INVALID_SIZE;
    }
    if (err != ESP_OK) {
        out = StoredSettings{};
        return err;
    }
    out.effect_id[sizeof(out.effect_id) - 1] = '\0';
    return ESP_OK;
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

esp_err_t settings_read_playlist_playing(bool& playing) {
    const PlaylistGuard guard;
    playing = false;
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    err = read_playlist(handle, s_playlist_now);
    nvs_close(handle);
    playing = err == ESP_OK && s_playlist_now.found && s_playlist_now.resumes;
    return err;
}

bool settings_load_playlist_playing() {
    bool playing = false;
    return settings_read_playlist_playing(playing) == ESP_OK && playing;
}

esp_err_t settings_save_remembered(const char* effect_id, uint8_t brightness_percent, uint8_t speed_x4, bool playing, int64_t choice_epoch,
                                   PlaylistMark playlist) {
    const PlaylistGuard guard;
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    // The list and remembered as they are, in one picture: what cannot be read fails the save, which the
    // scheduler tries again, rather than leave a playing list unmarked as if it had been marked.
    err = read_playlist(handle, s_playlist_now);
    StoredSettings stored;
    stored.brightness_percent = brightness_percent;
    stored.speed_x4 = speed_x4;
    stored.playing = playing ? 1 : 0;
    stored.choice_epoch = choice_epoch;
    std::snprintf(stored.effect_id, sizeof(stored.effect_id), "%s", effect_id);
    // Whichever a power cut between the writes leaves, it reads as the state before or the state after,
    // never a mix, and never a state older than both. Still playing, or starting: the record first,
    // marked for the value about to be written and, only if the list played with it, the one there.
    // Stopping: the record first marked for the one there alone, as an older mark could match the value
    // about to be written; then remembered, unless it stays the same bytes; then the record unmarked.
    const bool plays = playlist == PlaylistMark::Playing || (playlist == PlaylistMark::AsSaved && s_playlist_now.resumes);
    const bool same = s_playlist_now.remembered_known && std::memcmp(&s_playlist_now.remembered, &stored, sizeof(stored)) == 0;
    static std::array<uint8_t, kPlaylistRecordBytes> s_wanted;
    const auto write_record = [&](const StoredSettings* marks, size_t count) {
        const size_t bytes = playlist_record_make(s_playlist_now.text, marks, count, s_wanted.data(), s_wanted.size());
        if (bytes == 0) {
            return ESP_ERR_INVALID_SIZE;
        }
        if (bytes == s_playlist_now.record_bytes && std::memcmp(s_wanted.data(), s_playlist_now.record.data(), bytes) == 0) {
            return ESP_OK;
        }
        const esp_err_t written = nvs_set_blob(handle, kPlaylistRecordKey, s_wanted.data(), bytes);
        if (written == ESP_OK) {
            std::memcpy(s_playlist_now.record.data(), s_wanted.data(), bytes);
            s_playlist_now.record_bytes = bytes;
        }
        return written;
    };
    if (err == ESP_OK && s_playlist_now.found && plays) {
        StoredSettings marks[kPlaylistMarks];
        size_t count = 0;
        if (s_playlist_now.resumes && !same) {
            marks[count++] = s_playlist_now.remembered;
        }
        marks[count++] = stored;
        err = write_record(marks, count);
    } else if (err == ESP_OK && s_playlist_now.found && s_playlist_now.resumes && !same) {
        err = write_record(&s_playlist_now.remembered, 1);
    } else if (err == ESP_OK && s_playlist_now.found && !plays && !s_playlist_now.resumes) {
        // Not playing before either: marks left from older values come off at once.
        err = write_record(nullptr, 0);
    }
    if (err == ESP_OK && !same) {
        err = nvs_set_blob(handle, kRememberedKey, &stored, sizeof(stored));
    }
    if (err == ESP_OK && s_playlist_now.found && !plays) {
        err = write_record(nullptr, 0);
    }    if (err == ESP_OK) {
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

StoredRead settings_load_auth(AuthDevice* out, size_t count) {

    const size_t expected = sizeof(AuthDevice) * count;
    size_t bytes = expected;
    nvs_handle_t handle = 0;
    const esp_err_t opened = nvs_open(kNamespace, NVS_READONLY, &handle);
    esp_err_t got = ESP_FAIL;
    if (opened == ESP_OK) {
        got = nvs_get_blob(handle, kAuthKey, out, &bytes);
        nvs_close(handle);
    }
    const StoredRead read = auth_list_read(nvs_answer(opened), nvs_answer(got), bytes, expected);
    if (read != StoredRead::Loaded) {
        std::memset(out, 0, expected);
    }
    return read;
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

StoredRead settings_load_tls_domain(char* out, size_t out_size) {
    if (out_size == 0) {
        return StoredRead::Unreadable;
    }
    out[0] = '\0';
    nvs_handle_t handle = 0;
    const esp_err_t opened = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) {
        return StoredRead::Missing;
    }
    if (opened != ESP_OK) {
        return StoredRead::Unreadable;
    }
    size_t bytes = out_size;
    const esp_err_t err = nvs_get_str(handle, kTlsDomainKey, out, &bytes);
    nvs_close(handle);
    if (err == ESP_OK) {
        return StoredRead::Loaded;
    }
    out[0] = '\0';
    return err == ESP_ERR_NVS_NOT_FOUND ? StoredRead::Missing : StoredRead::Unreadable;
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
