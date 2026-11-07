/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Keeps the effects pack in two flash slots and switches between them safely
 * Contributors: None
 */

#include "effect_pack_store.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "effect_pack.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "psa/crypto.h"

namespace garland {
namespace {

const char* const TAG = "effect_pack";

constexpr std::array<const char*, 2> kSlotLabels = {"pack_a", "pack_b"};

std::array<const esp_partition_t*, 2> s_slots = {nullptr, nullptr};
size_t s_target = 0;
uint32_t s_next_sequence = 1;
size_t s_pack_bytes = 0;
size_t s_written = 0;
std::array<uint8_t, kPackDigestBytes> s_digest{};
char s_active_digest[kPackDigestBytes * 2 + 1] = "";

const esp_partition_t* find_slot(const char* label) {
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
}

// Read in small pieces: the hash hardware should not be pointed at mapped flash.
bool digest_of_pack(const esp_partition_t* slot, size_t bytes, std::array<uint8_t, kPackDigestBytes>& digest) {
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    if (psa_hash_setup(&hash, PSA_ALG_SHA_256) != PSA_SUCCESS) {
        return false;
    }
    std::array<uint8_t, 256> piece;
    for (size_t done = 0; done < bytes;) {
        const size_t length = std::min(piece.size(), bytes - done);
        if (esp_partition_read(slot, kPackSlotHeaderBytes + done, piece.data(), length) != ESP_OK ||
            psa_hash_update(&hash, piece.data(), length) != PSA_SUCCESS) {
            psa_hash_abort(&hash);
            return false;
        }
        done += length;
    }
    size_t length = 0;
    if (psa_hash_finish(&hash, digest.data(), digest.size(), &length) != PSA_SUCCESS) {
        // A finish that failed leaves the operation to be aborted, and the hardware it holds let go, as ota.cpp does.
        psa_hash_abort(&hash);
        return false;
    }
    return length == digest.size();
}

const char* check_mapped(const esp_partition_t* slot, size_t bytes) {
    const void* mapped = nullptr;
    esp_partition_mmap_handle_t handle = 0;
    if (esp_partition_mmap(slot, 0, slot->size, ESP_PARTITION_MMAP_DATA, &mapped, &handle) != ESP_OK) {
        return "could not read the effects pack";
    }
    const char* const problem = pack_check(static_cast<const char*>(mapped) + kPackSlotHeaderBytes, bytes);
    esp_partition_munmap(handle);
    return problem;
}

PackSlot examine(const esp_partition_t* slot) {
    PackSlot found;
    std::array<uint8_t, kPackSlotRecordBytes> raw;
    if (slot == nullptr || esp_partition_read(slot, 0, raw.data(), raw.size()) != ESP_OK ||
        !pack_slot_read(raw.data(), slot->size, found)) {
        return found;
    }
    s_next_sequence = std::max(s_next_sequence, found.sequence + 1);
    std::array<uint8_t, kPackDigestBytes> digest{};
    if (!digest_of_pack(slot, found.bytes, digest) || digest != found.digest) {
        ESP_LOGW(TAG, "%s is damaged or was cut short", slot->label);
        return found;
    }
    const char* const problem = check_mapped(slot, found.bytes);
    if (problem != nullptr) {
        ESP_LOGW(TAG, "%s refused: %s", slot->label, problem);
        return found;
    }
    found.usable = true;
    return found;
}

}  // namespace

PackView pack_store_open() {
    s_slots = {find_slot(kSlotLabels[0]), find_slot(kSlotLabels[1])};
    if (s_slots[0] == nullptr || s_slots[1] == nullptr) {
        ESP_LOGE(TAG, "no pack slots: this board needs one flash over USB");
        return {};
    }
    if (psa_crypto_init() != PSA_SUCCESS) {
        return {};
    }
    const PackSlot first = examine(s_slots[0]);
    const PackSlot second = examine(s_slots[1]);
    const int chosen = pack_choose_slot(first, second);
    s_target = chosen == 0 ? 1 : 0;
    if (chosen < 0) {
        ESP_LOGE(TAG, "no good effects pack");
        return {};
    }
    const PackSlot& slot = chosen == 0 ? first : second;
    const void* mapped = nullptr;
    esp_partition_mmap_handle_t handle = 0;
    const esp_err_t err = esp_partition_mmap(s_slots[static_cast<size_t>(chosen)], 0,
                                             s_slots[static_cast<size_t>(chosen)]->size, ESP_PARTITION_MMAP_DATA,
                                             &mapped, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not map the pack: %s", esp_err_to_name(err));
        return {};
    }
    for (size_t i = 0; i < slot.digest.size(); ++i) {
        std::snprintf(s_active_digest + i * 2, 3, "%02x", slot.digest[i]);
    }
    ESP_LOGI(TAG, "pack %lu from %s, %lu bytes", static_cast<unsigned long>(slot.sequence),
             kSlotLabels[static_cast<size_t>(chosen)], static_cast<unsigned long>(slot.bytes));
    return {static_cast<const char*>(mapped) + kPackSlotHeaderBytes, slot.bytes};
}

const char* pack_store_active_digest() {
    return s_active_digest;
}

size_t pack_store_capacity() {
    const esp_partition_t* const slot = s_slots[s_target];
    return slot != nullptr && slot->size > kPackSlotHeaderBytes ? slot->size - kPackSlotHeaderBytes : 0;
}

const char* pack_store_begin(size_t bytes) {
    if (bytes > pack_store_capacity()) {
        return "the effects pack does not fit";
    }
    const esp_partition_t* const slot = s_slots[s_target];
    const esp_err_t err = esp_partition_erase_range(slot, 0, slot->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "erase failed: %s", esp_err_to_name(err));
        return "could not write the effects pack";
    }
    s_pack_bytes = bytes;
    s_written = 0;
    return nullptr;
}

const char* pack_store_write(const uint8_t* data, size_t length) {
    if (s_written + length > s_pack_bytes) {
        return "the effects pack does not fit";
    }
    const esp_err_t err = esp_partition_write(s_slots[s_target], kPackSlotHeaderBytes + s_written, data, length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write failed: %s", esp_err_to_name(err));
        return "could not write the effects pack";
    }
    s_written += length;
    return nullptr;
}

const char* pack_store_check(const uint8_t* digest, bool must_be_readable) {
    if (s_written != s_pack_bytes || !digest_of_pack(s_slots[s_target], s_pack_bytes, s_digest) ||
        std::memcmp(s_digest.data(), digest, s_digest.size()) != 0) {
        return "the effects pack did not reach the flash intact";
    }
    return must_be_readable ? check_mapped(s_slots[s_target], s_pack_bytes) : nullptr;
}

const char* pack_store_commit() {
    std::array<uint8_t, kPackSlotRecordBytes> raw;
    pack_slot_write(raw.data(), s_next_sequence, static_cast<uint32_t>(s_pack_bytes), s_digest.data());
    const esp_err_t err = esp_partition_write(s_slots[s_target], 0, raw.data(), raw.size());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "commit failed: %s", esp_err_to_name(err));
        return "could not switch to the new effects pack";
    }
    ESP_LOGI(TAG, "pack %lu in %s", static_cast<unsigned long>(s_next_sequence), kSlotLabels[s_target]);
    ++s_next_sequence;
    return nullptr;
}

const char* pack_store_revert() {
    // The record's sector: with no record the slot does not count, and the one mapped still does.
    const esp_err_t err = esp_partition_erase_range(s_slots[s_target], 0, s_slots[s_target]->erase_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "taking back the pack failed: %s", esp_err_to_name(err));
        return "could not take back the effects pack";
    }
    ESP_LOGW(TAG, "new pack in %s taken back", kSlotLabels[s_target]);
    return nullptr;
}

}  // namespace garland
