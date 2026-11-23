/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: What the Lab build measures and how it writes it, free of the hardware for host tests
 * Contributors: None
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

// The Lab build, esp32c6-lab, defines it as 1; every other build has none of what it adds.
#ifndef CSIGARLAND_LAB
#define CSIGARLAND_LAB 0
#endif

namespace garland {

constexpr bool kLabBuild = CSIGARLAND_LAB != 0;

// The health line, once a minute.
constexpr uint32_t kLabHealthPeriodMs = 60000;

// mbedTLS's memory, as the Lab build counts it behind mbedtls_calloc: a line when its peak has grown by this much
// since the last, looked at every second, so a failed upload's peak is in the log. A good one ends in a restart,
// which takes the kept log, in RAM, with it: the count of the last second before it is kept apart, and said at
// the next start as "before the restart, tls: ...".
constexpr uint32_t kLabTlsLookMs = 1000;
constexpr uint32_t kLabTlsPeakStepBytes = 1024;
// An ask this large is one of the record buffers or a handshake's: before it, the heap's largest free block is
// read, as that is what such an ask needs, contiguous.
constexpr uint32_t kLabTlsBigAskBytes = 4096;

// What the counting allocator has seen since the start. Bytes are as the heap gives them, a block's whole size.
struct LabTls {
    uint32_t live = 0;        // Held by mbedTLS now.
    uint32_t peak = 0;        // The most it held at once.
    uint32_t largest_ask = 0; // The largest single ask.
    uint32_t allocs = 0;
    uint32_t failed = 0;
    uint32_t failed_ask = 0;  // The last ask that found no block.
    bool big_seen = false;
    uint32_t tightest_block = 0;  // The smallest largest free block found before a big ask...
    uint32_t tightest_ask = 0;    // ...and that ask.
    uint32_t heap_block = 0;      // The heap's largest free block now.
    uint32_t pool_size = 0;       // mbedTLS's own pool, see tls_pool.h...
    uint32_t pool_peak = 0;       // ...the most it has held at once...
    uint32_t pool_largest = 0;    // ...its largest free block now...
    uint32_t slot_misses = 0;     // ...the large asks that found the record buffer's slot taken...
    uint32_t spilled = 0;         // ...and the asks past the slot and the pool, served by the heap.
};

// "tls: live 5120, peak 46080, ask 16749, 812 allocs, 1 failed (16749); pool 11800/16384 peak, 9728 block, slot
// 0 missed, 0 spilled; heap 23456, 17024 before a 16749", into out, for the lab log. The heap's tightest block before a
// big ask says what a spilled one would have found.
inline size_t lab_format_tls(const LabTls& t, char* out, size_t size) {
    char failed[32] = "0 failed";
    if (t.failed != 0) {
        std::snprintf(failed, sizeof(failed), "%lu failed (%lu)", static_cast<unsigned long>(t.failed),
                      static_cast<unsigned long>(t.failed_ask));
    }
    char tightest[40] = "";
    if (t.big_seen) {
        std::snprintf(tightest, sizeof(tightest), ", %lu before a %lu", static_cast<unsigned long>(t.tightest_block),
                      static_cast<unsigned long>(t.tightest_ask));
    }
    const int written = std::snprintf(
        out, size,
        "tls: live %lu, peak %lu, ask %lu, %lu allocs, %s; pool %lu/%lu peak, %lu block, slot %lu missed, %lu spilled; heap %lu%s",
        static_cast<unsigned long>(t.live), static_cast<unsigned long>(t.peak), static_cast<unsigned long>(t.largest_ask),
        static_cast<unsigned long>(t.allocs), failed, static_cast<unsigned long>(t.pool_peak),
        static_cast<unsigned long>(t.pool_size), static_cast<unsigned long>(t.pool_largest),
        static_cast<unsigned long>(t.slot_misses), static_cast<unsigned long>(t.spilled), static_cast<unsigned long>(t.heap_block), tightest);
    if (written < 0 || size == 0) {
        return 0;
    }
    return static_cast<size_t>(written) < size ? static_cast<size_t>(written) : size - 1;
}

// The lowest and highest of a reading since the start.
struct LabRange {
    int32_t low = 0;
    int32_t high = 0;
    bool seen = false;

    void note(int32_t value) {
        if (!seen || value < low) {
            low = value;
        }
        if (!seen || value > high) {
            high = value;
        }
        seen = true;
    }
};

// The DS3231's temperature, in hundredths of a degree, from its registers 0x11 and 0x12: a 10-bit two's
// complement count of quarter degrees, whole degrees in the first byte and the quarters in the top two bits of
// the second.
constexpr int32_t rtc_temperature_centi(uint8_t whole, uint8_t quarters) {
    const auto raw = static_cast<int16_t>(static_cast<uint16_t>((whole << 8) | quarters));
    return static_cast<int32_t>(raw >> 6) * 25;
}

// Hundredths as "-1.25", into out, which holds at least 13 bytes.
inline void lab_format_centi(int32_t centi, char* out, size_t size) {
    const char* sign = centi < 0 ? "-" : "";
    const uint32_t magnitude = centi < 0 ? static_cast<uint32_t>(-static_cast<int64_t>(centi)) : static_cast<uint32_t>(centi);
    std::snprintf(out, size, "%s%lu.%02lu", sign, static_cast<unsigned long>(magnitude / 100),
                  static_cast<unsigned long>(magnitude % 100));
}

// What the health line says.
struct LabHealth {
    uint32_t up_s = 0;
    uint32_t heap = 0;
    uint32_t heap_low = 0;  // The lowest free heap since the start, as ESP-IDF keeps it.
    bool chip_read = false;
    int32_t chip_centi = 0;
    LabRange chip;
    bool box_read = false;  // The DS3231, in the box: false without the clock module.
    int32_t box_centi = 0;
    LabRange box;
    uint32_t model_ma = 0;  // The last frame's current by the model, before the limit.
    uint32_t model_top_ma = 0;
    bool rail_on = false;
    uint16_t leds = 0;
};

// "up 3600 s, heap 123456 (low 98765), chip 41.20 C (39.10..43.50), box 31.25 C (30.00..33.00),
// model 1012 mA (top 1012), rail on, 32 LEDs", into out, for the lab log. A temperature not read says "-".
inline size_t lab_format_health(const LabHealth& h, char* out, size_t size) {
    char chip[48] = "-";
    char box[48] = "-";
    if (h.chip_read) {
        char now[13];
        char low[13];
        char high[13];
        lab_format_centi(h.chip_centi, now, sizeof(now));
        lab_format_centi(h.chip.low, low, sizeof(low));
        lab_format_centi(h.chip.high, high, sizeof(high));
        std::snprintf(chip, sizeof(chip), "%s C (%s..%s)", now, low, high);
    }
    if (h.box_read) {
        char now[13];
        char low[13];
        char high[13];
        lab_format_centi(h.box_centi, now, sizeof(now));
        lab_format_centi(h.box.low, low, sizeof(low));
        lab_format_centi(h.box.high, high, sizeof(high));
        std::snprintf(box, sizeof(box), "%s C (%s..%s)", now, low, high);
    }
    const int written = std::snprintf(out, size, "up %lu s, heap %lu (low %lu), chip %s, box %s, model %lu mA (top %lu), rail %s, %u LEDs",
                                      static_cast<unsigned long>(h.up_s), static_cast<unsigned long>(h.heap),
                                      static_cast<unsigned long>(h.heap_low), chip, box, static_cast<unsigned long>(h.model_ma),
                                      static_cast<unsigned long>(h.model_top_ma), h.rail_on ? "on" : "off",
                                      static_cast<unsigned>(h.leds));
    if (written < 0 || size == 0) {
        return 0;
    }
    return static_cast<size_t>(written) < size ? static_cast<size_t>(written) : size - 1;
}

}  // namespace garland
