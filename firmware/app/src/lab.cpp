/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The Lab build's measurements on the board: why it started, temperatures, memory and current
 * Contributors: None
 */

#include "lab.h"

#if CSIGARLAND_LAB
#include <atomic>
#include <cmath>

#include "driver/temperature_sensor.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/platform.h"
#include "rtc.h"
#include "tls_pool.h"
#endif

namespace garland {

#if CSIGARLAND_LAB

namespace {

const char* const TAG = "lab";
constexpr uint32_t kTaskStackBytes = 4096;
// The chip's sensor reads best in the range it is set to; a sealed box under load stays within it.
constexpr int kChipRangeMinC = -10;
constexpr int kChipRangeMaxC = 80;

// Written by the Lighting task alone, read here.
std::atomic<uint32_t> s_model_ma{0};
std::atomic<uint32_t> s_model_top_ma{0};
temperature_sensor_handle_t s_chip_sensor = nullptr;

// mbedTLS's memory, from every task that calls it: the portal's server, the Wi-Fi supplicant. Atomics only, as
// an allocator must not wait on what may be waiting on it.
std::atomic<int32_t> s_tls_live{0};
std::atomic<uint32_t> s_tls_peak{0};
std::atomic<uint32_t> s_tls_largest_ask{0};
std::atomic<uint32_t> s_tls_allocs{0};
std::atomic<uint32_t> s_tls_failed{0};
std::atomic<uint32_t> s_tls_failed_ask{0};
std::atomic<bool> s_tls_big_seen{false};
std::atomic<uint32_t> s_tls_tightest_block{UINT32_MAX};
std::atomic<uint32_t> s_tls_tightest_ask{0};

// The last count, kept through a restart by the firmware in memory no start clears: an update's upload ends in
// one, which takes the log in RAM with it, and its peak, misses and spills with it. Said once at the next start.
constexpr uint32_t kKeptMagic = 0x4C41424BU;  // "LABK"
struct KeptTls {
    uint32_t magic;
    LabTls tls;
};
RTC_NOINIT_ATTR KeptTls s_kept_tls;

void raise_to(std::atomic<uint32_t>& value, uint32_t candidate) {
    uint32_t now = value.load(std::memory_order_relaxed);
    while (candidate > now && !value.compare_exchange_weak(now, candidate, std::memory_order_relaxed)) {
    }
}

// Counts, then asks the pool, as mbedTLS would have. A block's size is its allocator's own, taken the same way
// when it is freed, so what is held adds up whatever mbedTLS asked for.
void* counting_calloc(size_t count, size_t size) {
    const uint64_t asked = uint64_t{count} * size;
    const auto ask = static_cast<uint32_t>(asked > UINT32_MAX ? UINT32_MAX : asked);
    if (ask >= kLabTlsBigAskBytes) {
        const auto block = static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
        uint32_t tightest = s_tls_tightest_block.load(std::memory_order_relaxed);
        // Again while another task lowered it first, as long as this one is lower still.
        while (block < tightest && !s_tls_tightest_block.compare_exchange_weak(tightest, block, std::memory_order_relaxed)) {
        }
        if (block < tightest) {
            s_tls_tightest_ask.store(ask, std::memory_order_relaxed);
        }
        s_tls_big_seen.store(true, std::memory_order_relaxed);
    }
    s_tls_allocs.fetch_add(1, std::memory_order_relaxed);
    void* block = tls_pool_calloc(count, size);
    if (block == nullptr) {
        s_tls_failed.fetch_add(1, std::memory_order_relaxed);
        s_tls_failed_ask.store(ask, std::memory_order_relaxed);
        return nullptr;
    }
    raise_to(s_tls_largest_ask, ask);
    const auto held = static_cast<int32_t>(tls_pool_block_size(block));
    const int32_t live = s_tls_live.fetch_add(held, std::memory_order_relaxed) + held;
    raise_to(s_tls_peak, live > 0 ? static_cast<uint32_t>(live) : 0);
    return block;
}

void counting_free(void* block) {
    if (block == nullptr) {
        return;
    }
    s_tls_live.fetch_sub(static_cast<int32_t>(tls_pool_block_size(block)), std::memory_order_relaxed);
    tls_pool_free(block);
}

LabTls read_tls() {
    LabTls t;
    const int32_t live = s_tls_live.load(std::memory_order_relaxed);
    t.live = live > 0 ? static_cast<uint32_t>(live) : 0;
    t.peak = s_tls_peak.load(std::memory_order_relaxed);
    t.largest_ask = s_tls_largest_ask.load(std::memory_order_relaxed);
    t.allocs = s_tls_allocs.load(std::memory_order_relaxed);
    t.failed = s_tls_failed.load(std::memory_order_relaxed);
    t.failed_ask = s_tls_failed_ask.load(std::memory_order_relaxed);
    t.big_seen = s_tls_big_seen.load(std::memory_order_relaxed);
    t.tightest_block = s_tls_tightest_block.load(std::memory_order_relaxed);
    t.tightest_ask = s_tls_tightest_ask.load(std::memory_order_relaxed);
    t.heap_block = static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    const TlsPoolStats pool = tls_pool_stats();
    t.pool_size = static_cast<uint32_t>(kTlsPoolBytes);
    t.pool_peak = pool.pool_used_peak;
    t.pool_largest = pool.pool_largest;
    t.slot_misses = pool.slot_misses;
    t.spilled = pool.spilled;
    return t;
}

void log_tls(const LabTls& t) {
    char line[200];
    lab_format_tls(t, line, sizeof(line));
    ESP_LOGI(TAG, "%s", line);
}

const char* reset_reason_name(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:
            return "power on";
        case ESP_RST_EXT:
            return "external pin";
        case ESP_RST_SW:
            return "restart by the firmware";
        case ESP_RST_PANIC:
            return "panic";
        case ESP_RST_INT_WDT:
            return "interrupt watchdog";
        case ESP_RST_TASK_WDT:
            return "task watchdog";
        case ESP_RST_WDT:
            return "watchdog";
        case ESP_RST_DEEPSLEEP:
            return "deep sleep";
        case ESP_RST_BROWNOUT:
            return "BROWNOUT: the supply fell too low";
        case ESP_RST_SDIO:
            return "SDIO";
        case ESP_RST_USB:
            return "USB";
        case ESP_RST_JTAG:
            return "JTAG";
        case ESP_RST_EFUSE:
            return "eFuse error";
        case ESP_RST_PWR_GLITCH:
            return "POWER GLITCH";
        case ESP_RST_CPU_LOCKUP:
            return "CPU lockup";
        default:
            return "unknown";
    }
}

void start_chip_sensor() {
    // The driver logs its range with a degree sign, and the serial log takes plain ASCII only: its warnings only.
    esp_log_level_set("temperature_sensor", ESP_LOG_WARN);
    const temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(kChipRangeMinC, kChipRangeMaxC);
    esp_err_t err = temperature_sensor_install(&config, &s_chip_sensor);
    if (err == ESP_OK) {
        err = temperature_sensor_enable(s_chip_sensor);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "chip temperature sensor failed: %s", esp_err_to_name(err));
        if (s_chip_sensor != nullptr) {
            temperature_sensor_uninstall(s_chip_sensor);
        }
        s_chip_sensor = nullptr;
    }
}

void read_health(const State& state, LabHealth& health) {
    health.up_s = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
    health.heap = esp_get_free_heap_size();
    health.heap_low = esp_get_minimum_free_heap_size();
    float celsius = 0.0F;
    health.chip_read = s_chip_sensor != nullptr && temperature_sensor_get_celsius(s_chip_sensor, &celsius) == ESP_OK;
    if (health.chip_read) {
        health.chip_centi = static_cast<int32_t>(std::lround(celsius * 100.0F));
        health.chip.note(health.chip_centi);
    }
    int32_t box = 0;
    health.box_read = rtc_read_temperature_centi(box);
    if (health.box_read) {
        health.box_centi = box;
        health.box.note(box);
    }
    health.model_ma = s_model_ma.load(std::memory_order_relaxed);
    health.model_top_ma = s_model_top_ma.load(std::memory_order_relaxed);
    const Snapshot now = state.snapshot();
    health.rail_on = now.rail_on;
    health.leds = now.led_count;
}

// The health line and the TLS line once a minute, and the TLS line between whenever its peak has grown by a
// kilobyte or an ask has failed.
void lab_task(void* arg) {
    const State& state = *static_cast<const State*>(arg);
    LabHealth health;
    uint32_t since_health_ms = kLabHealthPeriodMs;
    uint32_t told_peak = 0;
    uint32_t told_failed = 0;
    while (true) {
        LabTls tls = read_tls();
        // The mark last, so a restart in between leaves none rather than half a count.
        s_kept_tls.magic = 0;
        s_kept_tls.tls = tls;
        s_kept_tls.magic = kKeptMagic;
        const bool grown = tls.peak >= told_peak + kLabTlsPeakStepBytes || tls.failed != told_failed;
        if (since_health_ms >= kLabHealthPeriodMs) {
            since_health_ms = 0;
            read_health(state, health);
            char line[200];
            lab_format_health(health, line, sizeof(line));
            ESP_LOGI(TAG, "%s", line);
            log_tls(tls);
            told_peak = tls.peak;
            told_failed = tls.failed;
        } else if (grown) {
            log_tls(tls);
            told_peak = tls.peak;
            told_failed = tls.failed;
        }
        vTaskDelay(pdMS_TO_TICKS(kLabTlsLookMs));
        since_health_ms += kLabTlsLookMs;
    }
}

}  // namespace

void lab_store_current(uint32_t model_ma) {
    s_model_ma.store(model_ma, std::memory_order_relaxed);
    if (model_ma > s_model_top_ma.load(std::memory_order_relaxed)) {
        s_model_top_ma.store(model_ma, std::memory_order_relaxed);
    }
}

void lab_count_tls() {
    mbedtls_platform_set_calloc_free(counting_calloc, counting_free);
}

void lab_start(State& state) {
    const esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGW(TAG, "Lab build: started by %s", reset_reason_name(reason));
    // Only after a restart by the firmware, as an update's is: power lost leaves that memory holding anything.
    if (reason == ESP_RST_SW && s_kept_tls.magic == kKeptMagic) {
        char line[200];
        lab_format_tls(s_kept_tls.tls, line, sizeof(line));
        ESP_LOGI(TAG, "before the restart, %s", line);
    }
    s_kept_tls.magic = 0;
    start_chip_sensor();
    // Below the screen and the buttons, which share the core: a line a minute is all it does.
    if (xTaskCreate(lab_task, "lab", kTaskStackBytes, &state, tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "no lab task");
    }
}

#else

void lab_store_current(uint32_t) {}

void lab_count_tls() {}

void lab_start(State&) {}

#endif

}  // namespace garland
