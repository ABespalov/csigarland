/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Static memory of mbedTLS's own, so a TLS record buffer never hunts for a block in the heap
 * Contributors: None
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace garland {

// A slot for the one large buffer mbedTLS asks for at a time: a 16 KB record's receive buffer, 16749 bytes with
// ESP-IDF's header. It keeps one connection per buffer and frees the old before asking for the next, and an
// update's body is read by the portal's one server task, so the record buffer is the one large ask in flight.
constexpr size_t kTlsSlotBytes = 16768;
// Asks above this go to the slot: the send buffer, 4.8 KB, and everything smaller go to the pool.
constexpr size_t kTlsSlotFromBytes = 8192;
// The pool for the rest: sessions, handshakes, send buffers. The Lab build measured some 12 KB of it at the
// most, with two handshakes and the portal's event streams, on 2026-11-23.
constexpr size_t kTlsPoolBytes = 16384;

// mbedTLS's memory, from the slot, the pool and the heap past them.
struct TlsPoolStats {
    uint32_t pool_used_peak = 0;  // The most the pool has held at once, with its own overhead.
    uint32_t pool_free = 0;
    uint32_t pool_largest = 0;    // The pool's largest free block.
    uint32_t slot_misses = 0;     // Large asks that found the slot taken, served by the heap.
    uint32_t spilled = 0;         // Asks the pool or the slot could not take, served by the heap.
    uint32_t spilled_failed = 0;  // Of those, the ones the heap could not take either.
};

// First at startup, before Wi-Fi and the portal: puts the slot and the pool behind mbedtls_calloc and
// mbedtls_free. With the pool unusable, which a fixed static region should never be, the rest keeps the heap.
void tls_pool_start();

// The allocator behind mbedtls_calloc and mbedtls_free, for the Lab build's count to pass its asks on to.
void* tls_pool_calloc(size_t count, size_t size);
void tls_pool_free(void* block);

// A block's size as its allocator gives it, the slot's, the pool's or the heap's: what the Lab build counts.
size_t tls_pool_block_size(const void* block);

TlsPoolStats tls_pool_stats();

}  // namespace garland
