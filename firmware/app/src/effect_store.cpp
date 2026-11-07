/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Keeps uploaded effects in their own flash partition
 * Contributors: None
 */

#include "effect_store.h"

#include <cJSON.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "effect_store_keys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace garland {
namespace {

const char* const TAG = "effect_store";

const char* const kPartition = "effects";
const char* const kNamespace = "effects";
constexpr size_t kIdBytes = kStoreIdBytes;
// A blob is the id in a fixed place, then the file.
constexpr size_t kBlobBytes = kIdBytes + kMaxEffectFileBytes + 1;

// Static: too large for any stack here, and only the server task writes it once started: a request's body, and
// the file it asks for.
char s_blob[kBlobBytes];
// What the store knows, and the blobs read from its list, apart from s_blob: the scheduler's task reads what is
// left unread too, for a garland that no browser opens, so all of it is under s_store_lock, and so are the
// changes to the catalog's uploads made here.
std::mutex s_store_lock;
char s_read_blob[kBlobBytes];
constexpr int kStartReadAttempts = 3;
constexpr uint32_t kStartReadPauseMs = 50;
bool s_available = false;
StorePartition s_partition = StorePartition::Unready;
StoreKeys s_keys;
size_t s_refused_keys = 0;
UnreadStoreKeys s_unread;
// Whether none is left unread, for any task without the lock: published each time a reading is done.
std::atomic<bool> s_all_read{false};
// Counts the blobs read, so that a reading again can tell whether it read any.
uint32_t s_entries_read = 0;

// True when the blob was read, kept or refused; false, and nothing known of it, when it could not be. Too
// short to hold an id is read all the same: it holds none, and is left as it is.
bool load_entry(nvs_handle_t handle, const char* key) {
    char* const blob = s_read_blob;
    size_t bytes = sizeof(s_read_blob);
    const esp_err_t err = nvs_get_blob(handle, key, blob, &bytes);
    if (err != ESP_OK || bytes <= kIdBytes) {
        ESP_LOGW(TAG, "%s unreadable: %s", key, esp_err_to_name(err));
        s_entries_read += err == ESP_OK ? 1 : 0;
        return err == ESP_OK;
    }
    ++s_entries_read;
    blob[kIdBytes - 1] = '\0';
    blob[bytes - 1] = '\0';
    EffectSpec spec;
    const char* error = nullptr;
    // Left where it is when refused: it may suit a later firmware.
    bool kept = false;
    if (!parse_effect(blob + kIdBytes, blob, spec, error)) {
        ESP_LOGW(TAG, "effect %s refused: %s", blob, error);
    } else if (catalog_install(spec) < 0) {
        ESP_LOGW(TAG, "no room for effect %s", blob);
    } else {
        kept = true;
    }
    if (kept || s_refused_keys < kMaxRefusedStoreKeys) {
        s_refused_keys += kept ? 0 : 1;
        s_keys.add(blob, key);
    }
    return true;
}

// Every entry not known yet, those that cannot be read left in s_unread. True when the whole list was had,
// none saved among it; false when the namespace could not be opened, or the list could not be had or
// stopped short, which says nothing of what is there: it is no empty store.
bool load_all() {
    nvs_handle_t handle = 0;
    const esp_err_t opened = nvs_open_from_partition(kPartition, kNamespace, NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) {
        return true;  // Nothing saved yet: the namespace does not exist.
    }
    if (opened != ESP_OK) {
        ESP_LOGW(TAG, "opening the uploaded effects failed: %s", esp_err_to_name(opened));
        return false;
    }
    nvs_iterator_t entry = nullptr;
    esp_err_t err = nvs_entry_find(kPartition, kNamespace, NVS_TYPE_BLOB, &entry);
    while (err == ESP_OK) {
        nvs_entry_info_t info;
        err = nvs_entry_info(entry, &info);
        if (err != ESP_OK) {
            break;
        }
        // Read already, as when the whole list is read again: kept, or refused and known.
        if (!s_keys.knows_key(info.key) && !load_entry(handle, info.key)) {
            s_unread.add(info.key);
        }
        err = nvs_entry_next(&entry);
    }
    nvs_release_iterator(entry);
    nvs_close(handle);
    // The end of the list, as of an empty one, is NOT_FOUND; any other answer stopped it short.
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "listing the uploaded effects failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

const char* parse_body(const char* id, EffectSpec& spec) {
    if (std::strlen(effect_store_body()) == 0) {
        return "the file is empty";
    }
    cJSON_Minify(effect_store_body());
    const char* error = nullptr;
    return parse_effect(effect_store_body(), id, spec, error) ? nullptr : error;
}

// Readies the partition. A full one, or one of a newer format, is erased as NVS asks, the only answers that
// erase it; any other failure leaves it as it is, and unread, to be readied again.
StorePartition ready_partition() {
    esp_err_t err = nvs_flash_init_partition(kPartition);
    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "no %s partition: effects cannot be saved by this partition table", kPartition);
        return StorePartition::Missing;
    }
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "erasing an unreadable partition: %s", esp_err_to_name(err));
        err = nvs_flash_erase_partition(kPartition);
        if (err == ESP_OK) {
            err = nvs_flash_init_partition(kPartition);
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "partition failed, tried again later: %s", esp_err_to_name(err));
        return StorePartition::Unready;
    }
    s_available = true;
    return StorePartition::Ready;
}

bool read_unread_locked();

}  // namespace

void effect_store_start() {
    const std::lock_guard<std::mutex> guard(s_store_lock);
    s_partition = ready_partition();
    note_store_partition(s_partition, s_unread);
    if (s_partition == StorePartition::Ready && !load_all()) {
        s_unread.mark_index_unread();
    }
    s_all_read.store(!s_unread.any());
    // A reading that failed is tried again a few times here, as the other settings are, then by the scheduler.
    for (int attempt = 1; attempt < kStartReadAttempts && s_unread.any(); ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(kStartReadPauseMs));
        read_unread_locked();
    }
    ESP_LOGI(TAG, "%u uploaded effects", static_cast<unsigned>(catalog_uploaded_count()));
}

char* effect_store_body() {
    return s_blob + kIdBytes;
}

bool effect_store_read_unread() {
    const std::lock_guard<std::mutex> guard(s_store_lock);
    return read_unread_locked();
}

namespace {

bool read_unread_locked() {
    if (!s_unread.any()) {
        return false;
    }
    if (!s_available) {
        // Readied again, its list still unread; once readied, read whole below.
        if (!store_partition_retried(s_partition)) {
            return false;
        }
        s_partition = ready_partition();
        if (s_partition != StorePartition::Ready) {
            return false;
        }
    }
    const uint32_t before = s_entries_read;
    if (s_unread.index_unread()) {
        // The whole list again, as an entry it did not list may be under any key: those known are passed by,
        // and those still not read are noted afresh.
        s_unread = UnreadStoreKeys{};
        if (!load_all()) {
            s_unread.mark_index_unread();
        }
    } else {
        nvs_handle_t handle = 0;
        if (nvs_open_from_partition(kPartition, kNamespace, NVS_READONLY, &handle) != ESP_OK) {
            return false;
        }
        s_unread.read_again([handle](const char* key) { return load_entry(handle, key); });
        nvs_close(handle);
    }
    // After what was read is in the catalog: a task that sees all read finds there every effect there is.
    s_all_read.store(!s_unread.any());
    const bool read = s_entries_read != before;
    if (read) {
        ESP_LOGI(TAG, "uploaded effects read late: %u in all", static_cast<unsigned>(catalog_uploaded_count()));
    }
    return read;
}

}  // namespace

bool effect_store_all_read() {
    return s_all_read.load();
}

namespace {

bool unread_may_hold_locked(const char* id) {
    return s_keys.find(id) == nullptr && s_unread.any();
}

}  // namespace

bool effect_store_unread_may_hold(const char* id) {
    const std::lock_guard<std::mutex> guard(s_store_lock);
    return unread_may_hold_locked(id);
}

const char* effect_store_read(const char* id) {
    const std::lock_guard<std::mutex> guard(s_store_lock);
    const char* const key = s_keys.find(id);
    nvs_handle_t handle = 0;
    if (key == nullptr || !s_available || nvs_open_from_partition(kPartition, kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return nullptr;
    }
    size_t bytes = sizeof(s_blob);
    const esp_err_t err = nvs_get_blob(handle, key, s_blob, &bytes);
    nvs_close(handle);
    if (err != ESP_OK || bytes <= kIdBytes) {
        return nullptr;
    }
    s_blob[bytes - 1] = '\0';
    return effect_store_body();
}

const char* effect_store_save(const char* id) {
    const std::lock_guard<std::mutex> guard(s_store_lock);
    if (!s_available) {
        return s_partition == StorePartition::Unready
                   ? "the uploaded effects could not be opened yet: try again"
                   : "this board needs one flash over USB before it can keep effects";
    }
    if (!uploaded_id_is_valid(id)) {
        return "the id, from the file's name, must be 1-32 lowercase letters, digits or hyphens";
    }
    if (!catalog_can_install(id)) {
        return "no room: delete an uploaded effect first";
    }
    EffectSpec spec;
    const char* const problem = parse_body(id, spec);
    if (problem != nullptr) {
        return problem;
    }
    std::snprintf(s_blob, kIdBytes, "%s", id);

    const char* const known_key = s_keys.find(id);
    if (unread_may_hold_locked(id)) {
        return "uploaded effects saved before could not all be read yet, and one may have this id: try again";
    }
    if (known_key == nullptr && !s_keys.has_room()) {
        return "no room: delete an uploaded effect first";
    }
    char key[kStoreKeyBytes] = "";
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open_from_partition(kPartition, kNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        if (known_key != nullptr) {
            std::snprintf(key, sizeof(key), "%s", known_key);
        } else if (!s_keys.choose(id, key, [handle](const char* candidate) {
                       nvs_type_t type = NVS_TYPE_ANY;
                       return nvs_find_key(handle, candidate, &type) == ESP_ERR_NVS_NOT_FOUND;
                   })) {
            err = ESP_FAIL;
        }
        if (err == ESP_OK) {
            err = nvs_set_blob(handle, key, s_blob, kIdBytes + std::strlen(effect_store_body()) + 1);
        }
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving %s failed: %s", id, esp_err_to_name(err));
        return err == ESP_ERR_NVS_NOT_ENOUGH_SPACE ? "no room: delete an uploaded effect first" : "could not save";
    }
    if (known_key == nullptr) {
        s_keys.add(id, key);
    }
    catalog_install(spec);
    ESP_LOGI(TAG, "effect %s saved", id);
    return nullptr;
}

const char* effect_store_preview() {
    const std::lock_guard<std::mutex> guard(s_store_lock);
    EffectSpec spec;
    const char* const problem = parse_body(kPreviewEffectId, spec);
    if (problem != nullptr) {
        return problem;
    }
    catalog_preview(spec);
    return nullptr;
}

const char* effect_store_delete(const char* id) {
    const std::lock_guard<std::mutex> guard(s_store_lock);
    const int position = catalog_find(id);
    if (position < 0 || !catalog_is_uploaded(static_cast<size_t>(position))) {
        return "no such uploaded effect";
    }
    const char* const key = s_keys.find(id);
    if (key == nullptr) {
        ESP_LOGE(TAG, "no key known for %s", id);
        return "could not delete";
    }
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open_from_partition(kPartition, kNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_erase_key(handle, key);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "deleting %s failed: %s", id, esp_err_to_name(err));
        return "could not delete";
    }
    s_keys.remove(id);
    catalog_remove(static_cast<size_t>(position));
    ESP_LOGI(TAG, "effect %s deleted", id);
    return nullptr;
}

EffectStoreStatus effect_store_status() {
    const std::lock_guard<std::mutex> guard(s_store_lock);
    EffectStoreStatus status;
    status.available = s_available;
    nvs_stats_t stats = {};
    if (s_available && nvs_get_stats(kPartition, &stats) == ESP_OK) {
        status.entries_used = stats.used_entries;
        status.entries_total = stats.total_entries;
    }
    return status;
}

}  // namespace garland
