/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: NTP time, the time zone, and the task that runs scheduled actions
 * Contributors: None
 */

#include "scheduler.h"

#include <sys/time.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "effect_catalog.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ota.h"
#include "portal_tls.h"
#include "rtc.h"
#include "rtc_logic.h"
#include "settings.h"

namespace garland {
namespace {

const char* const TAG = "schedule";

const char kNtpServer[] = "pool.ntp.org";
const char kDefaultZone[] = "UTC0";
constexpr size_t kZoneBytes = 49;

constexpr uint32_t kStackBytes = 4096;
constexpr UBaseType_t kPriority = tskIDLE_PRIORITY + 2;
constexpr uint32_t kPollMs = 1000;
constexpr uint32_t kNtpIntervalMs = 3600 * 1000;

std::atomic<bool> s_synced{false};
std::atomic<bool> s_ntp_arrived{false};
std::atomic<ClockSource> s_clock_source{ClockSource::None};
std::atomic<bool> s_zone_set{false};
char s_zone[kZoneBytes] = "";
// Its words, as a page may read them while they are set. Made with them, before any page.
SemaphoreHandle_t s_zone_lock = nullptr;
StaticSemaphore_t s_zone_lock_storage;

constexpr int64_t kRtcReadBackSeconds = 2;
// Seven registers go over the bus one acknowledged byte at a time, so setting them can stop half
// way with the module looking perfectly well. This says so across a restart until it is seen done.
std::atomic<bool> s_rtc_unfinished{false};
SemaphoreHandle_t s_rtc_lock = nullptr;
StaticSemaphore_t s_rtc_lock_storage;

std::array<ScheduleEntry, kMaxScheduleEntries> s_entries{};
size_t s_entry_count = 0;
uint32_t s_schedule_revision = 0;
SemaphoreHandle_t s_lock = nullptr;
StaticSemaphore_t s_lock_storage;
TaskHandle_t s_task = nullptr;
int64_t s_choice_epoch = kNoChoiceYet;
// What the next action was last found to be. Finding it can take a thousand times what a page
// needs to read it, and the scheduler looks each minute. It stands until that action has come, or
// the list, the zone or the clock changes: see next_action_still_holds.
struct NextActionCache {
    int64_t minute = -1;
    bool found = false;
    int64_t action_minute = -1;
    ResolvedAction action;
};
NextActionCache s_next;

void apply_zone(const char* zone) {
    setenv("TZ", zone, 1);
    tzset();
}

// In the network's task, which must not wait on the I2C bus: the scheduler task takes it on.
void on_time_synced(timeval*) {
    s_ntp_arrived.store(true);
    if (!s_synced.exchange(true)) {
        ESP_LOGI(TAG, "time synced");
    }
}

LocalMinute to_local_minute(int64_t minutes_since_epoch);

// Under s_lock.
void refresh_next_action(int64_t now_minute) {
    if (next_action_still_holds(s_next.minute, s_next.action_minute, now_minute)) {
        return;
    }
    s_next.minute = now_minute;
    s_next.action_minute = next_action_minute(s_entries.data(), s_entry_count, now_minute, to_local_minute);
    s_next.found = s_next.action_minute >= 0;
    if (s_next.found) {
        s_next.action = resolve_minutes(s_entries.data(), s_entry_count, s_next.action_minute, s_next.action_minute, to_local_minute);
    }
}

bool action_comes_soon(int64_t now_minute) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    refresh_next_action(now_minute);
    const bool soon = action_is_soon(s_next.action_minute, now_minute);
    xSemaphoreGive(s_lock);
    return soon;
}

void forget_next_action() {
    if (s_lock != nullptr) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_next.minute = -1;
        xSemaphoreGive(s_lock);
    }
}

void set_system_clock(int64_t epoch) {
    const timeval now = {static_cast<time_t>(epoch), 0};
    settimeofday(&now, nullptr);
    forget_next_action();
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
void pass_ntp_time_to_rtc() {
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

LocalMinute to_local_minute(int64_t minutes_since_epoch) {
    const time_t seconds = static_cast<time_t>(minutes_since_epoch * 60);
    tm local = {};
    localtime_r(&seconds, &local);
    return LocalMinute{static_cast<uint8_t>(local.tm_wday), static_cast<uint8_t>(local.tm_hour),
                       static_cast<uint8_t>(local.tm_min)};
}

int64_t now_in_minutes() {
    return static_cast<int64_t>(std::time(nullptr)) / 60;
}

ResolvedAction resolve_under_lock(int64_t first_minute, int64_t last_minute) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const ResolvedAction action =
        resolve_minutes(s_entries.data(), s_entry_count, first_minute, last_minute, to_local_minute);
    xSemaphoreGive(s_lock);
    return action;
}

// As one change, so the lighting task never sees part of it, and only if the owner has chosen
// nothing since this was worked out: that choice is newer than all of it.
void apply(State& state, const ResolvedAction& action) {
    if (!action.any) {
        return;
    }
    const int power = action.power == PowerStep::None ? -1 : action.power == PowerStep::On ? 1 : 0;
    if (!state.apply_scheduled(action.brightness_percent, action.speed_x4, action.effect, power)) {
        ESP_LOGI(TAG, "the owner chose while this was worked out, so it stands");
        return;
    }
    ESP_LOGI(TAG, "applied: power=%d bright=%u speed=%u effect=%d", static_cast<int>(action.power),
             action.brightness_percent, action.speed_x4, action.effect);
}

Remembered remembered_from(const Snapshot& now, uint16_t effect) {
    return Remembered{effect, now.brightness_percent, now.speed_x4, now.power_chosen};
}

// A garland with no clock module may run for years on a certificate made from the build's date,
// as NTP only ever answers after the portal has started, and one made under a clock module that
// ran ahead starts in the future. Once real time shows either, that time is kept and a restart
// makes new ones from it, after which they are not stale.
void renew_certificate_if_due(const State& state) {
    const auto now = static_cast<int64_t>(std::time(nullptr));
    if (ota_on_trial() || state.snapshot().update_percent != kNoUpdate || !portal_tls_renewal_due(now)) {
        return;
    }
    ESP_LOGW(TAG, "the certificate is running out: restarting to renew it");
    if (portal_tls_prepare_renewal(now)) {
        ota_restart_soon();
    }
}

// Here rather than where they change, as the buttons, portal and schedule all change them. A
// preview is gone at the next start, so the effect before it stays the one to remember, and
// everything else is saved as ever: above all a turn off.
void remember_settings(const State& state, MemorySaveTimer& timer, uint16_t& real_effect) {
    const Snapshot now = state.snapshot();
    if (!catalog_is_preview(now.effect)) {
        real_effect = now.effect;
    }
    const Remembered current = remembered_from(now, real_effect);
    const auto now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    if (now.update_percent != kNoUpdate || !timer.should_save(current, now_ms)) {
        return;
    }
    const int64_t chosen_at = s_synced.load() ? static_cast<int64_t>(std::time(nullptr)) : 0;
    const esp_err_t err = settings_save_remembered(catalog_id(real_effect).c_str(), now.brightness_percent, now.speed_x4, now.power_chosen, chosen_at);
    if (err == ESP_OK) {
        s_choice_epoch = chosen_at;
        timer.saved(current);
    } else {
        ESP_LOGW(TAG, "saving the settings failed: %s", esp_err_to_name(err));
        timer.retry_later(now_ms);
    }
}

void give_the_choice_a_time(int64_t first_known_epoch) {
    StoredSettings stored;
    if (!settings_load_remembered(stored)) {
        ESP_LOGW(TAG, "reading the choice to give it a time failed");
        return;
    }
    const esp_err_t err = settings_save_remembered(stored.effect_id, stored.brightness_percent, stored.speed_x4, stored.playing != 0, first_known_epoch);
    if (err == ESP_OK) {
        s_choice_epoch = first_known_epoch;
    } else {
        ESP_LOGW(TAG, "saving the time of the choice failed: %s", esp_err_to_name(err));
    }
}

uint32_t uptime_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

// A look round comes up to a second later, and a step can fall due in between, so it takes the
// choice's own moment to tell which came first.
int64_t minute_of_last_choice(const State& state) {
    const auto wall_ms = [] {
        timeval now = {};
        gettimeofday(&now, nullptr);
        return static_cast<int64_t>(now.tv_sec) * 1000 + now.tv_usec / 1000;
    };
    return latest_wall_ms_of_uptime(state.owner_choice_ms(), uptime_ms, wall_ms) / 60000;
}

void wake_to_save() {
    if (s_task != nullptr) {
        xTaskNotifyGive(s_task);
    }
}

void scheduler_task(void* arg) {
    State& state = *static_cast<State*>(arg);
    s_task = xTaskGetCurrentTaskHandle();
    state.set_power_listener(wake_to_save);
    state.set_choice_clock(uptime_ms);
    int64_t last_minute = -1;
    int64_t choice_minute = -1;
    int64_t first_known_epoch = 0;
    bool tried_choice_time = false;
    uint32_t choice_time_tried_ms = 0;
    uint16_t real_effect = state.snapshot().effect;
    MemorySaveTimer memory_timer(remembered_from(state.snapshot(), real_effect));
    while (true) {
        // A turn on or off is saved right away, not at the next look round.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kPollMs));
        if (state.take_owner_choice()) {
            choice_minute = minute_of_last_choice(state);
            memory_timer.note_choice();
        }
        remember_settings(state, memory_timer, real_effect);
        if (s_ntp_arrived.exchange(false)) {
            if (rtc_present()) {
                pass_ntp_time_to_rtc();
            }
            renew_certificate_if_due(state);
        }
        if (!s_synced.load()) {
            continue;
        }
        const int64_t now = now_in_minutes();
        state.set_action_soon(action_comes_soon(now));
        if (last_minute < 0) {
            // What the garland was left as has already come back, and outranks every step from
            // before it. Only those that came due with no power are still to do: without them it
            // could stay dark until the next "on" comes around tomorrow. A choice still waiting to
            // be saved is newer than all of them.
            const bool chosen_just_now = !memory_timer.holds(remembered_from(state.snapshot(), real_effect));
            const int64_t first = chosen_just_now ? -1 : replay_first_minute(s_choice_epoch, now);
            if (first >= 0) {
                apply(state, resolve_under_lock(first, now));
            }
            first_known_epoch = static_cast<int64_t>(std::time(nullptr));
        } else if (now > last_minute) {
            const int64_t first = catch_up_first_minute(last_minute, now, choice_minute);
            if (first <= now) {
                apply(state, resolve_under_lock(first, now));
            }
        }
        const bool holds = memory_timer.holds(remembered_from(state.snapshot(), real_effect));
        if (choice_time_is_due(s_choice_epoch, holds, tried_choice_time, uptime_ms() - choice_time_tried_ms)) {
            tried_choice_time = true;
            choice_time_tried_ms = uptime_ms();
            give_the_choice_a_time(first_known_epoch);
        }
        // A clock that stepped back simply waits; nothing fires twice for it.
        last_minute = last_minute < 0 ? now : std::max(last_minute, now);
        if (now < last_minute - kMaxCatchUpMinutes) {
            last_minute = now;
        }
    }
}

}  // namespace

void time_load_zone() {
    s_zone_lock = xSemaphoreCreateMutexStatic(&s_zone_lock_storage);
    const bool saved = settings_load_timezone(s_zone, sizeof(s_zone)) && time_zone_is_plausible(s_zone);
    s_zone_set.store(saved);
    if (!saved) {
        std::snprintf(s_zone, sizeof(s_zone), "%s", kDefaultZone);
    }
    apply_zone(s_zone);
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

bool time_zone_is_set() {
    return s_zone_set.load();
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
    xSemaphoreGive(s_zone_lock);
    s_zone_set.store(true);
    forget_next_action();
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
    if (!s_synced.load()) {
        return false;
    }
    const time_t now = std::time(nullptr);
    tm local = {};
    localtime_r(&now, &local);
    return std::strftime(out, out_size, "%H:%M:%S", &local) != 0;
}

bool start_scheduler(State& state) {
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    s_entry_count = settings_load_schedule(s_entries.data(), s_entries.size());
    ESP_LOGI(TAG, "%u actions loaded", static_cast<unsigned>(s_entry_count));
    // From anywhere, so that a page left open across a restart does not match by chance.
    s_schedule_revision = esp_random();
    StoredSettings remembered;
    s_choice_epoch = settings_load_remembered(remembered) ? remembered.choice_epoch : kNoChoiceYet;
    return xTaskCreate(scheduler_task, "scheduler", kStackBytes, &state, kPriority, nullptr) == pdPASS;
}

bool schedule_next(char* when, size_t when_size, ResolvedAction& action) {
    if (!s_synced.load()) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    refresh_next_action(now_in_minutes());
    const bool found = s_next.found;
    const int64_t minute = s_next.action_minute;
    if (found) {
        action = s_next.action;
    }
    xSemaphoreGive(s_lock);
    if (!found) {
        return false;
    }
    const time_t seconds = static_cast<time_t>(minute * 60);
    tm local = {};
    localtime_r(&seconds, &local);
    return std::strftime(when, when_size, "%a %d %b, %H:%M", &local) != 0;
}

size_t schedule_copy(ScheduleEntry* out) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const size_t count = s_entry_count;
    std::copy_n(s_entries.begin(), count, out);
    xSemaphoreGive(s_lock);
    return count;
}

esp_err_t schedule_replace(const ScheduleEntry* entries, size_t count) {
    ESP_RETURN_ON_ERROR(settings_save_schedule(entries, count), TAG, "save");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    std::copy_n(entries, count, s_entries.begin());
    s_entry_count = count;
    s_next.minute = -1;
    ++s_schedule_revision;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

uint32_t schedule_revision() {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const uint32_t revision = s_schedule_revision;
    xSemaphoreGive(s_lock);
    return revision;
}

}  // namespace garland
