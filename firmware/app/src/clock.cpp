/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: NTP time, the time zone, and the optional clock module
 * Contributors: None
 */

#include <sys/time.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "rtc.h"
#include "rtc_logic.h"
#include "scheduler.h"
#include "scheduler_clock.h"
#include "settings.h"

namespace garland {
namespace {

const char* const TAG = "clock";

const char kNtpServer[] = "pool.ntp.org";
const char kDefaultZone[] = "UTC0";
constexpr size_t kZoneBytes = 49;
constexpr uint32_t kNtpIntervalMs = 3600 * 1000;
std::atomic<bool> s_synced{false};
std::atomic<bool> s_ntp_arrived{false};
std::atomic<ClockSource> s_clock_source{ClockSource::None};
std::atomic<bool> s_zone_set{false};
char s_zone[kZoneBytes] = "";
// Its words, as a page may read them while they are set. Made with them, before any page.
SemaphoreHandle_t s_zone_lock = nullptr;
StaticSemaphore_t s_zone_lock_storage;
// A zone may be saved and could not be read: UTC meanwhile, which is no zone of the owner's, so no browser
// is asked to set one, and it is read again every few seconds. Only the scheduler's task retries.
std::atomic<bool> s_zone_unread{false};
uint32_t s_zone_tried_ms = 0;
constexpr int kZoneReadAttempts = 3;
constexpr uint32_t kZoneReadPauseMs = 50;
constexpr uint32_t kZoneRetryMs = 5000;

constexpr int64_t kRtcReadBackSeconds = 2;
// Seven registers go over the bus one acknowledged byte at a time, so setting them can stop half
// way with the module looking perfectly well. This says so across a restart until it is seen done.
std::atomic<bool> s_rtc_unfinished{false};
SemaphoreHandle_t s_rtc_lock = nullptr;
StaticSemaphore_t s_rtc_lock_storage;

void apply_zone(const char* zone) {
    setenv("TZ", zone, 1);
    tzset();
}

// The saved zone onto the clock while it is unread, under its lock, so a zone the owner sets meanwhile, which
// is newer, is never replaced: true once it is read, or found to be none, which leaves UTC.
bool read_zone() {
    static char s_read[kZoneBytes];
    const StoredRead read = settings_read_timezone(s_read, sizeof(s_read));
    if (read == StoredRead::Unreadable) {
        return false;
    }
    const bool saved = read == StoredRead::Loaded && time_zone_is_plausible(s_read);
    xSemaphoreTake(s_zone_lock, portMAX_DELAY);
    if (s_zone_unread.load()) {
        if (saved) {
            std::snprintf(s_zone, sizeof(s_zone), "%s", s_read);
            apply_zone(s_zone);
        }
        s_zone_set.store(saved);
        s_zone_unread.store(false);
    }
    xSemaphoreGive(s_zone_lock);
    return true;
}

// In the network's task, which must not wait on the I2C bus: the scheduler task takes it on.
void on_time_synced(timeval*) {
    s_ntp_arrived.store(true);
    if (!s_synced.exchange(true)) {
        ESP_LOGI(TAG, "time synced");
    }
}

void set_system_clock(int64_t epoch) {
    const timeval now = {static_cast<time_t>(epoch), 0};
    settimeofday(&now, nullptr);
    schedule_forget_next_action();
}

// Holds the module for one whole change of the time: reading it, choosing, writing it and the
// system clock. So a time set by hand never lands between NTP's look and its write, to be put back.
class RtcGuard {
public:
    RtcGuard() : lock_(s_rtc_lock) {
        if (lock_ != nullptr) {
            xSemaphoreTake(lock_, portMAX_DELAY);
        }
    }
    ~RtcGuard() {
        if (lock_ != nullptr) {
            xSemaphoreGive(lock_);
        }
    }
    RtcGuard(const RtcGuard&) = delete;
    RtcGuard& operator=(const RtcGuard&) = delete;

private:
    SemaphoreHandle_t lock_;
};

// Marked as under way until it reads back as what was set. The caller holds RtcGuard.
bool write_rtc_checked(int64_t epoch) {
    bool done = settings_save_rtc_unfinished(true) == ESP_OK;
    if (done) {
        s_rtc_unfinished.store(true);
        int64_t read_back = 0;
        bool trusted = false;
        done = rtc_write(epoch) && rtc_read(read_back, trusted) && trusted && std::llabs(read_back - epoch) <= kRtcReadBackSeconds;
    }
    if (done) {
        // Not done until saved: the next start with no network would not believe the module.
        done = settings_save_rtc_unfinished(false) == ESP_OK;
        s_rtc_unfinished.store(!done);
    }
    return done;
}

// NTP is the better clock, so the module follows it when it has drifted or lost time. The time is
// taken under the guard, after any setting by hand that came first.
void pass_ntp_time_to_rtc_now() {
    const RtcGuard guard;
    const auto ntp_epoch = static_cast<int64_t>(std::time(nullptr));
    int64_t rtc_epoch = 0;
    bool trusted = false;
    if (!rtc_read(rtc_epoch, trusted) || s_rtc_unfinished.load()) {
        trusted = false;
    }
    if (rtc_needs_correction(trusted, rtc_epoch, ntp_epoch)) {
        if (!write_rtc_checked(ntp_epoch)) {
            ESP_LOGW(TAG, "setting the clock module failed");
            return;
        }
        ESP_LOGI(TAG, "clock module set from NTP");
    }
    s_clock_source.store(ClockSource::Ntp);
}

}  // namespace

bool time_take_ntp_arrival() {
    return s_ntp_arrived.exchange(false);
}

void time_pass_ntp_to_rtc() {
    pass_ntp_time_to_rtc_now();
}

void time_load_zone() {
    s_zone_lock = xSemaphoreCreateMutexStatic(&s_zone_lock_storage);
    std::snprintf(s_zone, sizeof(s_zone), "%s", kDefaultZone);
    apply_zone(s_zone);
    s_zone_unread.store(true);
    for (int attempt = 0; attempt < kZoneReadAttempts && !read_zone(); ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(kZoneReadPauseMs));
    }
    if (s_zone_unread.load()) {
        ESP_LOGE(TAG, "the time zone could not be read: UTC meanwhile, read again every few seconds, and not set from a browser");
    }
}

void time_zone_retry_if_unread(uint32_t now_ms) {
    if (!s_zone_unread.load() || now_ms - s_zone_tried_ms < kZoneRetryMs) {
        return;
    }
    s_zone_tried_ms = now_ms;
    if (read_zone()) {
        ESP_LOGI(TAG, "the time zone was read late");
        schedule_forget_next_action();
    }
}

esp_err_t time_sync_init() {
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(kNtpServer);
    config.start = false;
    config.sync_cb = on_time_synced;
    const esp_err_t err = esp_netif_sntp_init(&config);
    if (err == ESP_OK) {
        esp_sntp_set_sync_interval(kNtpIntervalMs);
    }
    return err;
}

void time_load_from_rtc() {
    s_rtc_lock = xSemaphoreCreateMutexStatic(&s_rtc_lock_storage);
    if (!rtc_start()) {
        return;
    }
    const RtcGuard guard;
    s_rtc_unfinished.store(settings_load_rtc_unfinished());
    int64_t epoch = 0;
    bool trusted = false;
    if (rtc_read(epoch, trusted) && trusted && !s_rtc_unfinished.load()) {
        set_system_clock(epoch);
        s_synced.store(true);
        s_clock_source.store(ClockSource::Module);
        ESP_LOGI(TAG, "time taken from the clock module");
    } else {
        // Connecting to the home network starts NTP, whose first answer sets the module.
        ESP_LOGW(TAG, "the clock module lost time; waiting for NTP");
    }
}

RtcStatus time_rtc_status() {
    RtcStatus status;
    status.present = rtc_present();
    status.source = s_clock_source.load();
    int64_t epoch = 0;
    bool trusted = false;
    // Not between the registers of a setting under way.
    const RtcGuard guard;
    if (status.present && rtc_read(epoch, trusted)) {
        status.trusted = trusted && !s_rtc_unfinished.load();
        const auto seconds = static_cast<time_t>(epoch);
        tm local = {};
        localtime_r(&seconds, &local);
        std::strftime(status.now, sizeof(status.now), "%Y-%m-%d %H:%M:%S", &local);
    }
    return status;
}

bool time_set_rtc(int64_t epoch, ClockSource source) {
    const RtcGuard guard;
    if (!write_rtc_checked(epoch)) {
        return false;
    }
    set_system_clock(epoch);
    s_synced.store(true);
    s_clock_source.store(source);
    ESP_LOGI(TAG, "clock module set from the portal");
    return true;
}

bool time_set_rtc_local(const LocalDateTime& local, ClockSource source) {
    tm fields = {};
    fields.tm_year = local.year - 1900;
    fields.tm_mon = local.month - 1;
    fields.tm_mday = local.day;
    fields.tm_hour = local.hour;
    fields.tm_min = local.minute;
    fields.tm_sec = local.second;
    fields.tm_isdst = -1;
    return time_set_rtc(static_cast<int64_t>(std::mktime(&fields)), source);
}

void time_sync_now() {
    esp_sntp_restart();
}

void time_sync_on_connected() {
    const esp_err_t err = esp_netif_sntp_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NTP start failed: %s", esp_err_to_name(err));
    }
}

bool time_is_synced() {
    return s_synced.load();
}

bool time_zone_ready() {
    return !s_zone_unread.load();
}

bool time_zone_is_set() {
    // A zone not read yet may be the owner's: no page is to set the browser's over it.
    return s_zone_set.load() || s_zone_unread.load();
}

void time_zone_get(char* out, size_t out_size) {
    xSemaphoreTake(s_zone_lock, portMAX_DELAY);
    std::snprintf(out, out_size, "%s", s_zone);
    xSemaphoreGive(s_zone_lock);
}

esp_err_t time_zone_set(const char* zone) {
    ESP_RETURN_ON_ERROR(settings_save_timezone(zone), TAG, "save");
    xSemaphoreTake(s_zone_lock, portMAX_DELAY);
    std::snprintf(s_zone, sizeof(s_zone), "%s", zone);
    apply_zone(s_zone);
    // The owner's, and newer than any zone a late reading would bring.
    s_zone_set.store(true);
    s_zone_unread.store(false);
    xSemaphoreGive(s_zone_lock);
    schedule_forget_next_action();
    return ESP_OK;
}

void time_format_local(char* out, size_t out_size) {
    if (out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!s_synced.load()) {
        return;
    }
    const time_t now = std::time(nullptr);
    tm local = {};
    localtime_r(&now, &local);
    std::strftime(out, out_size, "%Y-%m-%d %H:%M", &local);
}

bool time_format_clock(char* out, size_t out_size) {
    // The screen's time of day: the owner's, not UTC in its place while a saved zone is not read. Uptime meanwhile.
    if (!local_time_is_known(s_synced.load(), !s_zone_unread.load())) {
        return false;
    }
    const time_t now = std::time(nullptr);
    tm local = {};
    localtime_r(&now, &local);
    return std::strftime(out, out_size, "%H:%M:%S", &local) != 0;
}

}  // namespace garland
