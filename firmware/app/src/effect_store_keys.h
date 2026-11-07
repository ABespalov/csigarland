/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Which NVS key holds which uploaded effect
 * Contributors: None
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "effect_catalog.h"

namespace garland {

constexpr size_t kStoreIdBytes = kMaxEffectIdLength + 1;
constexpr size_t kStoreKeyBytes = 10;  // 'e', eight hex digits and the terminator; NVS allows 15.
// Files this firmware refuses stay in flash, as a later one may take them. A few are known, so
// that replacing one reuses its key; the rest of the table is for the uploads themselves.
constexpr size_t kMaxRefusedStoreKeys = 8;

// An id may be longer than an NVS key, so a key starts as a hash of it. Different ids can share
// a hash, as costarring and liquid do, so the next key is taken then. Each blob holds its id, so
// the table is rebuilt from flash at every start, whatever key an entry got.
inline void store_key_for(const char* id, uint32_t attempt, char (&key)[kStoreKeyBytes]) {
    uint32_t hash = 2166136261u;
    for (const char* c = id; *c != '\0'; ++c) {
        hash = (hash ^ static_cast<uint8_t>(*c)) * 16777619u;
    }
    if (attempt != 0) {
        hash = (hash ^ attempt) * 16777619u;
    }
    std::snprintf(key, sizeof(key), "e%08lx", static_cast<unsigned long>(hash));
}

class StoreKeys {
public:
    const char* find(const char* id) const {
        for (const Entry& entry : entries_) {
            if (entry.id[0] != '\0' && std::strcmp(entry.id, id) == 0) {
                return entry.key;
            }
        }
        return nullptr;
    }

    bool has_room() const {
        for (const Entry& entry : entries_) {
            if (entry.id[0] == '\0') {
                return true;
            }
        }
        return false;
    }

    bool add(const char* id, const char* key) {
        for (Entry& entry : entries_) {
            if (entry.id[0] == '\0') {
                std::snprintf(entry.id, sizeof(entry.id), "%s", id);
                std::snprintf(entry.key, sizeof(entry.key), "%s", key);
                return true;
            }
        }
        return false;
    }

    // Whether an entry holds key: one known, kept or refused, which a reading of the whole list again passes by.
    bool knows_key(const char* key) const { return holds_key(key); }

    void remove(const char* id) {
        for (Entry& entry : entries_) {
            if (std::strcmp(entry.id, id) == 0) {
                entry = Entry{};
            }
        }
    }

    // A key for an id with none yet: free both here and, by the caller's check, in flash, which
    // may hold entries unknown here. False if every attempt failed.
    template <typename IsFree>
    bool choose(const char* id, char (&key)[kStoreKeyBytes], IsFree is_free) const {
        for (uint32_t attempt = 0; attempt < kAttempts; ++attempt) {
            store_key_for(id, attempt, key);
            if (!holds_key(key) && is_free(key)) {
                return true;
            }
        }
        return false;
    }

private:
    struct Entry {
        char id[kStoreIdBytes] = {};
        char key[kStoreKeyBytes] = {};
    };
    static constexpr uint32_t kAttempts = 64;

    bool holds_key(const char* key) const {
        for (const Entry& entry : entries_) {
            if (entry.id[0] != '\0' && std::strcmp(entry.key, key) == 0) {
                return true;
            }
        }
        return false;
    }

    std::array<Entry, kMaxUploadedEffects + kMaxRefusedStoreKeys> entries_{};
};

// Keys whose blob could not be read at the start. Any may hold the id an upload takes for new, and a second
// copy saved beside it would come back at the next start once the upload is deleted, so none is uploaded
// under an id not known while any is left. Read again before each such upload. More than it can hold
// leaves it unread for good, until a restart.
constexpr size_t kMaxUnreadStoreKeys = 8;

class UnreadStoreKeys {
public:
    void add(const char* key) {
        for (auto& held : keys_) {
            if (held[0] == '\0') {
                std::snprintf(held, sizeof(held), "%s", key);
                return;
            }
        }
        overflowed_ = true;
    }

    // The list of keys itself could not be had, or stopped short: any key may be one not read, so none is
    // known to be free until the whole list is read again.
    void mark_index_unread() { index_unread_ = true; }
    bool index_unread() const { return index_unread_; }

    bool any() const {
        if (overflowed_ || index_unread_) {
            return true;
        }
        for (const auto& held : keys_) {
            if (held[0] != '\0') {
                return true;
            }
        }
        return false;
    }

    // read(key) is true when that key's blob was read now, and so is no longer unread.
    template <typename Read>
    void read_again(Read read) {
        for (auto& held : keys_) {
            if (held[0] != '\0' && read(static_cast<const char*>(held))) {
                held[0] = '\0';
            }
        }
    }

private:
    // Any key NVS holds, ours or not: its longest, 15 characters, and the terminator.
    static constexpr size_t kKeyBytes = 16;
    std::array<char[kKeyBytes], kMaxUnreadStoreKeys> keys_{};
    bool overflowed_ = false;
    bool index_unread_ = false;
};

// The partition of uploads as readying it found it. One the partition table lacks holds nothing, as nothing
// could ever be saved there: a store known empty. One that is there but could not be readied, as when memory
// ran short, may hold any upload, so its whole list stays unread until it is readied and listed.
enum class StorePartition : uint8_t { Ready, Missing, Unready };

inline void note_store_partition(StorePartition partition, UnreadStoreKeys& unread) {
    if (partition == StorePartition::Unready) {
        unread.mark_index_unread();
    }
}

// Readied again, before what is unread is read again: only one that is there and was not readied.
inline bool store_partition_retried(StorePartition partition) {
    return partition == StorePartition::Unready;
}

}  // namespace garland
