/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Static memory of mbedTLS's own, so a TLS record buffer never hunts for a block in the heap
 * Contributors: None
 */

#include "tls_pool.h"

#include <atomic>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "mbedtls/platform.h"
#include "multi_heap.h"

namespace garland {
namespace {

const char* const TAG = "tls_pool";

// An update's TLS record buffer, 16.7 KB, is freed after each record and asked for again for the next. In the
// heap, the Wi-Fi buffers of the same upload took the space between; in one pool for everything, the 4.8 KB send
// buffers of the event streams, which tell the update's progress, did. Either way no 16.7 KB block was left, and
// the update failed. The slot holds that one buffer and nothing else.
alignas(8) uint8_t s_slot[kTlsSlotBytes];
std::atomic<bool> s_slot_taken{false};
alignas(8) uint8_t s_pool[kTlsPoolBytes];
multi_heap_handle_t s_heap = nullptr;
// multi_heap takes no lock of its own: a heap registered by hand has none until it is given one, and mbedTLS is
// called from the portal's server, its event streams and the Wi-Fi supplicant alike. A spinlock, as the system
// heap's, held for one allocation or one free.
portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint32_t> s_slot_misses{0};
std::atomic<uint32_t> s_spilled{0};
std::atomic<uint32_t> s_spilled_failed{0};

// By address as a number: comparing pointers into different objects, as a heap block and the pool, is
// unspecified in C++.
bool in_pool(const void* block) {
    const auto at = reinterpret_cast<uintptr_t>(block);
    const auto start = reinterpret_cast<uintptr_t>(s_pool);
    return at >= start && at < start + sizeof(s_pool);
}

void* from_heap(size_t count, size_t size) {
    s_spilled.fetch_add(1, std::memory_order_relaxed);
    void* block = heap_caps_calloc(count, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (block == nullptr) {
        s_spilled_failed.fetch_add(1, std::memory_order_relaxed);
    }
    return block;
}

}  // namespace

void* tls_pool_calloc(size_t count, size_t size) {
    if (count != 0 && size > SIZE_MAX / count) {
        return nullptr;
    }
    const size_t bytes = count * size;
    if (bytes == 0) {
        return nullptr;
    }
    if (bytes > kTlsSlotFromBytes) {
        if (bytes <= sizeof(s_slot)) {
            if (!s_slot_taken.exchange(true, std::memory_order_acquire)) {
                std::memset(s_slot, 0, bytes);
                return s_slot;
            }
            // A second large ask at once, as a handshake beside an update would be: the heap, as before.
            s_slot_misses.fetch_add(1, std::memory_order_relaxed);
        }
        // One larger than the slot, which no configuration here asks for, is a spill and no miss.
        return from_heap(count, size);
    }
    void* block = s_heap != nullptr ? multi_heap_malloc(s_heap, bytes) : nullptr;
    if (block != nullptr) {
        std::memset(block, 0, bytes);
        return block;
    }
    return from_heap(count, size);
}

void tls_pool_free(void* block) {
    if (block == nullptr) {
        return;
    }
    if (block == s_slot) {
        s_slot_taken.store(false, std::memory_order_release);
    } else if (in_pool(block)) {
        multi_heap_free(s_heap, block);
    } else {
        heap_caps_free(block);
    }
}

size_t tls_pool_block_size(const void* block) {
    if (block == nullptr) {
        return 0;
    }
    if (block == s_slot) {
        return sizeof(s_slot);
    }
    return in_pool(block) ? multi_heap_get_allocated_size(s_heap, const_cast<void*>(block))
                          : heap_caps_get_allocated_size(const_cast<void*>(block));
}

void tls_pool_start() {
    if (s_heap != nullptr) {
        return;
    }
    s_heap = multi_heap_register(s_pool, sizeof(s_pool));
    if (s_heap == nullptr) {
        ESP_LOGE(TAG, "the pool could not be set up: mbedTLS has the slot and the heap");
    } else {
        multi_heap_set_lock(s_heap, &s_lock);
    }
    mbedtls_platform_set_calloc_free(tls_pool_calloc, tls_pool_free);
    ESP_LOGI(TAG, "%u bytes of slot and %u of pool for mbedTLS", static_cast<unsigned>(sizeof(s_slot)),
             static_cast<unsigned>(sizeof(s_pool)));
}

TlsPoolStats tls_pool_stats() {
    TlsPoolStats stats;
    if (s_heap != nullptr) {
        multi_heap_info_t info = {};
        multi_heap_get_info(s_heap, &info);
        stats.pool_used_peak = static_cast<uint32_t>(sizeof(s_pool) - info.minimum_free_bytes);
        stats.pool_free = static_cast<uint32_t>(info.total_free_bytes);
        stats.pool_largest = static_cast<uint32_t>(info.largest_free_block);
    }
    stats.slot_misses = s_slot_misses.load(std::memory_order_relaxed);
    stats.spilled = s_spilled.load(std::memory_order_relaxed);
    stats.spilled_failed = s_spilled_failed.load(std::memory_order_relaxed);
    return stats;
}

}  // namespace garland
