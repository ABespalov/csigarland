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

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "effect_store_keys.h"
#include "esp_log.h"
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

// Static: too large for any stack here, and only the server task writes it once started.
char s_blob[kBlobBytes];
bool s_available = false;
StoreKeys s_keys;
size_t s_refused_keys = 0;
UnreadStoreKeys s_unread;

// True when the blob was read, kept or refused; false, and nothing known of it, when it could not be. Too
// short to hold an id is read all the same: it holds none, and is left as it is.
bool load_entry(nvs_handle_t handle, const char* key) {
    size_t bytes = sizeof(s_blob);
    const esp_err_t err = nvs_get_blob(handle, key, s_blob, &bytes);
    if (err != ESP_OK || bytes <= kIdBytes) {
        ESP_LOGW(TAG, "%s unreadable: %s", key, esp_err_to_name(err));
        return err == ESP_OK;
    }
    s_blob[kIdBytes - 1] = '\0';
    s_blob[bytes - 1] = '\0';
    EffectSpec spec;
    const char* error = nullptr;
    // Left where it is when refused: it may suit a later firmware.
    bool kept = false;
    if (!parse_effect(s_blob + kIdBytes, s_blob, spec, error)) {
        ESP_LOGW(TAG, "effect %s refused: %s", s_blob, error);
    } else if (catalog_install(spec) < 0) {
        ESP_LOGW(TAG, "no room for effect %s", s_blob);
    } else {
        kept = true;
    }
    if (kept || s_refused_keys < kMaxRefusedStoreKeys) {
        s_refused_keys += kept ? 0 : 1;
        s_keys.add(s_blob, key);
    }
    return true;
}

void load_all() {
    nvs_handle_t handle = 0;
    if (nvs_open_from_partition(kPartition, kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return;  // Nothing saved yet: the namespace does not exist.
    }
    nvs_iterator_t entry = nullptr;
    esp_err_t err = nvs_entry_find(kPartition, kNamespace, NVS_TYPE_BLOB, &entry);
    while (err == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(entry, &info);
        if (!load_entry(handle, info.key)) {
            s_unread.add(info.key);
        }
        err = nvs_entry_next(&entry);
    }
    nvs_release_iterator(entry);
    nvs_close(handle);
}

const char* parse_body(const char* id, EffectSpec& spec) {
    if (std::strlen(effect_store_body()) == 0) {
        return "the file is empty";
    }
    cJSON_Minify(effect_store_body());
    const char* error = nullptr;
    return parse_effect(effect_store_body(), id, spec, error) ? nullptr : error;
}

}  // namespace

void effect_store_start() {
    esp_err_t err = nvs_flash_init_partition(kPartition);
    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "no %s partition: effects cannot be saved until a flash over USB", kPartition);
        return;
    }
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "erasing an unreadable partition: %s", esp_err_to_name(err));
        err = nvs_flash_erase_partition(kPartition);
        if (err == ESP_OK) {
            err = nvs_flash_init_partition(kPartition);
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "partition failed: %s", esp_err_to_name(err));
        return;
    }
    s_available = true;
    load_all();
    ESP_LOGI(TAG, "%u uploaded effects", static_cast<unsigned>(catalog_uploaded_count()));
}

char* effect_store_body() {
    return s_blob + kIdBytes;
}

bool effect_store_read_unread() {
    if (!s_available || !s_unread.any()) {
        return false;
    }
    nvs_handle_t handle = 0;
    if (nvs_open_from_partition(kPartition, kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    bool read = false;
    s_unread.read_again([handle, &read](const char* key) {
        const bool now = load_entry(handle, key);
        read = read || now;
        return now;
    });
    nvs_close(handle);
    if (read) {
        ESP_LOGI(TAG, "uploaded effects read late: %u in all", static_cast<unsigned>(catalog_uploaded_count()));
    }
    return read;
}

bool effect_store_unread_may_hold(const char* id) {
    return s_keys.find(id) == nullptr && s_unread.any();
}

const char* effect_store_read(const char* id) {
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
    if (!s_available) {
        return "this board needs one flash over USB before it can keep effects";
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
    if (effect_store_unread_may_hold(id)) {
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
    EffectSpec spec;
    const char* const problem = parse_body(kPreviewEffectId, spec);
    if (problem != nullptr) {
        return problem;
    }
    catalog_preview(spec);
    return nullptr;
}

const char* effect_store_delete(const char* id) {
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
