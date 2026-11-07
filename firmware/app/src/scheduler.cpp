/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The task that runs scheduled actions, and saves and brings back what the owner chose
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
#include "looks.h"
#include "nvs.h"
#include "ota.h"
#include "playlist_resolver.h"
#include "portal_tls.h"
#include "rtc.h"
#include "rtc_logic.h"
#include "scheduler_clock.h"
#include "settings.h"

namespace garland {
namespace {

const char* const TAG = "schedule";


constexpr uint32_t kStackBytes = 4096;
constexpr UBaseType_t kPriority = tskIDLE_PRIORITY + 2;
constexpr uint32_t kPollMs = 1000;

std::array<ScheduleEntry, kMaxScheduleEntries> s_entries{};
size_t s_entry_count = 0;
uint32_t s_schedule_revision = 0;
// Whether the list above is the one NVS holds. Until it is, it is no empty list: nothing of it runs, the
// portal shows and saves none of it, and it is read again every minute.
std::atomic<bool> s_schedule_read{false};
// The last minute the scheduler has done, for the timeline: a minute begun and not done yet is still ahead
// of what the state says, and its step is still to come.
std::atomic<int64_t> s_done_minute{-1};
// The first time known since the start, and the moment of the last scheduled step done, both 0 for none:
// what a save of the settings is dated by, see remember_settings.
std::atomic<int64_t> s_first_known_epoch{0};
std::atomic<int64_t> s_last_step_epoch{0};
// The first minute whose step waits for the playlist to be read, -1 for none: a save of what the steps
// before it did is dated just before it, so that a start in between still has it to do. See save_epoch.
std::atomic<int64_t> s_steps_wait_from{-1};
// The start's replay, at a start or again after a late read, not yet begun: its steps wait too, so a save
// before it, as of a playlist resumed while the schedule cannot be read, is dated before the first of them,
// and neither the replay nor the next start passes them by. The scheduler's task alone reads and sets it.
bool s_replay_pending = true;
// A restart asked for, and what came of saving the settings ahead of it, see scheduler_save_before_restart.
enum class RestartSave : uint8_t { None, Asked, Held, Failed };
std::atomic<RestartSave> s_restart_save{RestartSave::None};
// What was saved, as the scheduler last answered Held: a restart holds it to what is chosen at that moment.
std::atomic<uint64_t> s_restart_held{0};
// And the moment of the owner's last choice it was saved with: a choice since, the same value again among
// them, is newer than what was saved.
std::atomic<uint32_t> s_restart_held_choice{0};
State* s_restart_state = nullptr;
constexpr uint32_t kRestartSaveStepMs = 50;
// What was remembered is there and could not be read at the start: nothing is saved over it until it is.
std::atomic<bool> s_remembered_unread{false};
constexpr uint32_t kRememberedRetryMs = 5000;
constexpr uint32_t kScheduleRetryMs = 60000;
SemaphoreHandle_t s_lock = nullptr;
StaticSemaphore_t s_lock_storage;
TaskHandle_t s_task = nullptr;
int64_t s_choice_epoch = kNoChoiceYet;
// What flash held of the remembered choice at the start, as start_scheduler took it.
Remembered s_saved_at_start;
// What the next action was last found to be. Finding it can take a thousand times what a page
// needs to read it, and the scheduler looks each minute. It stands until that action has come, or
// the list, the zone or the clock changes: see next_action_still_holds.
struct NextActionCache {
    int64_t minute = -1;
    bool found = false;
    int64_t action_minute = -1;
    ResolvedAction action;
    // The catalog it was worked out with: an effect uploaded since may be one a step names.
    uint16_t catalog = 0;
};
NextActionCache s_next;

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

// As the summary tells it: a step that only starts an empty playlist does nothing, and is no action soon.
// Worked out once a minute, or when the list or the playlist's having effects changes, as it is asked
// every second and a walk past such steps can take a day of conversions.
struct SoonCache {
    int64_t minute = -1;  // -1 for nothing worked out, as after a change of zone or clock.
    uint32_t revision = 0;
    bool ready = false;
    bool soon = false;
};
SoonCache s_seen;  // Under s_lock.

// What the summary last told, for its minute, list, catalog and playlist: a walk past steps that only
// start an empty playlist can take a week of conversions, and every page that asks would take it again.
struct SummaryCache {
    int64_t minute = -1;
    uint32_t revision = 0;
    bool ready = false;
    int64_t action_minute = -1;
    ResolvedAction action;
};
SummaryCache s_summary;  // Under s_lock; forgotten with the next action, see hold_to_catalog.

// The next step within a day, as the Scheduler24 screensaver tells it, for its minute, list and playlist:
// asked every second, and a walk past steps that only start an empty playlist can take a day of conversions.
struct SaverCache {
    int64_t minute = -1;
    uint32_t revision = 0;
    bool ready = false;
    int64_t step_minute = -1;
    ResolvedAction action;
};
SaverCache s_saver_step;  // Under s_lock; forgotten with the next action.
// The step last done since the start, on the uptime clock, for the same screensaver. Under s_lock.
bool s_step_done = false;
uint32_t s_step_done_ms = 0;
ResolvedAction s_step_done_action;

// Under s_lock. Forgets what was worked out with another catalog.
void hold_to_catalog(uint16_t catalog) {
    if (s_next.catalog != catalog) {
        s_next.catalog = catalog;
        s_next.minute = -1;
        s_seen.minute = -1;
        s_summary.minute = -1;
        s_saver_step.minute = -1;
    }
}

bool action_comes_soon(int64_t now_minute, bool playlist_ready, uint16_t catalog) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    hold_to_catalog(catalog);
    refresh_next_action(now_minute);
    if (s_seen.minute != now_minute || s_seen.revision != s_schedule_revision || s_seen.ready != playlist_ready) {
        int64_t minute = s_next.found ? s_next.action_minute : -1;
        if (minute >= 0 && !s_next.action.with_playlist(playlist_ready).does_something()) {
            ResolvedAction action;
            minute = next_doing_minute(s_entries.data(), s_entry_count, minute, now_minute + kActionSoonMinutes, to_local_minute, playlist_ready,
                                       action);
        }
        s_seen.minute = now_minute;
        s_seen.revision = s_schedule_revision;
        s_seen.ready = playlist_ready;
        s_seen.soon = action_is_soon(minute, now_minute);
    }
    const bool soon = s_seen.soon;
    xSemaphoreGive(s_lock);
    return soon;
}

void forget_next_action() {
    if (s_lock != nullptr) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_next.minute = -1;
        s_seen.minute = -1;
        s_summary.minute = -1;
        s_saver_step.minute = -1;
        xSemaphoreGive(s_lock);

    }
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
void apply(State& state, const ResolvedAction& resolved) {
    if (!resolved.any) {
        return;
    }
    // A step starting the playlist with none to start is as if it were not: see merge_entry. Both tracks
    // go, and apply_scheduled takes one by the list it reads once.
    const auto order = [](PowerStep step) {
        return step == PowerStep::None ? kPowerKeep : step == PowerStep::Rise ? kPowerRise : step == PowerStep::On ? kPowerOn : kPowerOff;
    };
    if (!state.apply_scheduled(resolved.brightness_percent, resolved.speed_x4, resolved.effect, order(resolved.power), resolved.playlist,
                               resolved.effect_without_playlist, order(resolved.power_without_playlist))) {
        ESP_LOGI(TAG, "the owner chose while this was worked out, so it stands");
        return;
    }
    s_last_step_epoch.store(static_cast<int64_t>(std::time(nullptr)));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_step_done = true;
    s_step_done_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    s_step_done_action = resolved.with_playlist(state.snapshot().playlist_size > 0);
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "applied: power=%d bright=%u speed=%u effect=%d playlist=%d", static_cast<int>(resolved.power),
             resolved.brightness_percent, resolved.speed_x4, resolved.effect, resolved.playlist ? 1 : 0);
}

uint64_t pack_remembered(const Remembered& r) {
    return uint64_t{r.effect} | uint64_t{r.brightness_percent} << 16 | uint64_t{r.speed_x4} << 24 | uint64_t{r.playing} << 32 |
           uint64_t{r.playlist} << 33;
}

// Whether what is chosen now is what the scheduler last said it saved: an Off of the countdown after that
// answer is not in it. The effect as the scheduler remembers it, which a preview or the playlist moving on
// leaves as it was.
bool restart_held_matches_now() {
    const Snapshot now = s_restart_state->snapshot();
    const uint64_t held = s_restart_held.load();
    const uint16_t effect = !catalog_is_preview(now.effect) && !now.playlist ? now.effect : static_cast<uint16_t>(held & 0xFFFF);
    return pack_remembered(remembered_from(now, effect)) == held && s_restart_state->owner_choice_ms() == s_restart_held_choice.load();
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
    // An update being received has the garland first, and the next answer of NTP looks again.
    if (!ota_reserve_restart()) {
        return;
    }
    ESP_LOGW(TAG, "the certificate is running out: restarting to renew it");
    if (portal_tls_prepare_renewal(now)) {
        ota_restart_soon();
    } else {
        ota_release_restart();
    }
}

// Here rather than where they change, as the buttons, portal and schedule all change them. A
// preview is gone at the next start, so the effect before it stays the one to remember, and
// everything else is saved as ever: above all a turn off.
uint32_t uptime_ms();

// The wall-clock second of the owner's last choice, from the uptime it was made at: right once the clock
// is set, whenever the choice was made.
int64_t epoch_of_last_choice(const State& state) {
    const auto wall_ms = [] {
        timeval now = {};
        gettimeofday(&now, nullptr);
        return static_cast<int64_t>(now.tv_sec) * 1000 + now.tv_usec / 1000;
    };
    return latest_wall_ms_of_uptime(state.owner_choice_ms(), uptime_ms, wall_ms) / 1000;
}

// The first time known since the start, taken the first time it is asked for with the clock set, by a
// save as by the scheduler's own pass: a save dated by it before the pass took it would else go by 0.
int64_t note_first_known_epoch() {
    int64_t known = s_first_known_epoch.load();
    if (known == 0) {
        known = static_cast<int64_t>(std::time(nullptr));
        s_first_known_epoch.store(known);
    }
    return known;
}

// The first minute a replay not yet begun goes from, by the choice saved, or -1 for none.
int64_t replay_start(int64_t first_known_epoch, int64_t now) {
    const int64_t first = s_choice_epoch == 0 ? first_known_epoch / 60 + 1 : replay_first_minute(s_choice_epoch, now);
    return first >= 0 ? std::max(first, now - kReplayMinutes + 1) : first;
}

// The first minute whose step waits, for the playlist or for the replay, -1 for none: see save_epoch.
int64_t first_minute_waiting(int64_t first_known_epoch) {
    const int64_t held = s_steps_wait_from.load();
    const int64_t now = now_in_minutes();
    const int64_t first = s_replay_pending ? replay_start(first_known_epoch, now) : -1;
    return first < 0 || first > now || (held >= 0 && held < first) ? held : first;
}

void remember_settings(const State& state, MemorySaveTimer& timer, uint16_t& real_effect) {
    // A save writes every field, and the ones not read yet would be lost to what the state holds.
    if (s_remembered_unread.load()) {
        return;
    }
    const Snapshot now = state.snapshot();
    // An effect the playlist moved on to is none of the owner's choosing: what is remembered, and when it
    // was chosen, stay as they were when the playlist began, so that a scheduled step since still counts.
    if (!catalog_is_preview(now.effect) && !now.playlist) {
        real_effect = now.effect;
    }
    const Remembered current = remembered_from(now, real_effect);
    const auto now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    // During an update nothing is saved, as it has the flash; once it is installed, or a restart is asked
    // for, what is chosen is saved at once, as the countdown's Off that came during the update.
    const bool restarting = now.update_percent == kUpdateRestarting || s_restart_save.load() != RestartSave::None;
    if ((now.update_percent != kNoUpdate && !restarting) || !(restarting ? !timer.holds(current) : timer.should_save(current, now_ms))) {
        return;
    }
    // When it was chosen, see save_epoch. The first time known is taken here if the pass has not yet.
    int64_t chosen_at = 0;
    if (time_is_synced()) {
        const int64_t choice = state.owner_chose() ? epoch_of_last_choice(state) : -1;
        const int64_t first_known = note_first_known_epoch();
        chosen_at = save_epoch(static_cast<int64_t>(std::time(nullptr)), choice, first_known, s_last_step_epoch.load(),
                               first_minute_waiting(first_known));
    }
    // While the mark of playing waits, unread or read and waiting for the list to resume it, a save the
    // owner or a step did not make about what plays, of the power, brightness or speed alone among them,
    // keeps the mark there: written as stopped, it would end a playlist still to resume.
    const bool unknown = playlist_mark_is_pending() && !state.owner_chose_play() && !state.scheduled_play();
    const PlaylistMark mark = unknown ? PlaylistMark::AsSaved : now.playlist ? PlaylistMark::Playing : PlaylistMark::Stopped;
    const esp_err_t err = settings_save_remembered(catalog_id(real_effect).c_str(), now.brightness_percent, now.speed_x4, now.power_chosen,
                                                   chosen_at, mark);
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
    // The playlist as it was saved with the value this replaces, read with it under one lock.
    const esp_err_t err = settings_save_remembered(stored.effect_id, stored.brightness_percent, stored.speed_x4, stored.playing != 0, first_known_epoch,
                                                   PlaylistMark::AsSaved);
    if (err == ESP_OK) {
        s_choice_epoch = first_known_epoch;
    } else {
        ESP_LOGW(TAG, "saving the time of the choice failed: %s", esp_err_to_name(err));
    }
}

uint32_t uptime_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

// What was remembered, read now that it can be: put on the state as at a start, unless the owner chose
// since, which is newer, and saves go on from it. True when the start's replay is to be done again: worked
// out with no choice known, it took only the steps after the first time known, and those that came due with
// no power, after the choice now read, are still to do. Steps done meanwhile come after that choice, so the
// replay does them again, on top of what was remembered: one of the speed alone sets no power, and must not
// keep the rest from coming back.
bool read_remembered_late(State& state, MemorySaveTimer& timer, uint16_t& real_effect) {
    StoredSettings remembered;
    const esp_err_t read = settings_read_remembered(remembered);
    if (read != ESP_OK && read != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "reading the remembered choice failed again: %s", esp_err_to_name(read));
        return false;
    }
    bool untouched = !state.owner_chose();
    if (read == ESP_OK) {
        // The time of the record as flash holds it, whatever is put on the state, until a save replaces it:
        // a time given now to a choice that has one would move it past the steps it missed.
        s_choice_epoch = remembered.choice_epoch;
        const Snapshot before = state.snapshot();
        untouched = untouched && remembered_apply(state, remembered);
        // Whatever the owner chose since: only a choice of what plays keeps the playlist from coming back, as
        // the resume itself says; one of the power, brightness or speed alone leaves it to.
        const bool plays_on = playlist_restore_playing(state);
        // What flash holds, not what the state holds, whether or not it was put on: a choice made meanwhile,
        // as a turn off, has to stay a change to save, at once if it is one of those. The owner's choice is
        // newer, and stays a choice to save.
        const int index = catalog_find(remembered.effect_id);
        const uint16_t saved_effect = index >= 0 ? static_cast<uint16_t>(index) : real_effect;
        real_effect = untouched ? saved_effect : real_effect;
        timer.saved(Remembered{
            saved_effect,
            remembered_brightness_is_valid(remembered.brightness_percent) ? remembered.brightness_percent : before.brightness_percent,
            manual_speed_x4_is_valid(remembered.speed_x4) ? remembered.speed_x4 : before.speed_x4,
            remembered.playing != 0,
            plays_on,
        });
        if (state.owner_chose()) {
            timer.note_choice();
        }
        ESP_LOGI(TAG, "the remembered choice was read late%s", untouched ? ", and taken" : ", after a choice since");
    } else {
        s_choice_epoch = kNoChoiceYet;
    }
    s_remembered_unread.store(false);
    return untouched;
}

// A look round comes up to a second later, and a step can fall due in between, so it takes the
// choice's own moment to tell which came first.
int64_t minute_of_last_choice(const State& state) {
    return epoch_of_last_choice(state) / 60;
}

void wake_to_save() {
    if (s_task != nullptr) {
        xTaskNotifyGive(s_task);
    }
}

// The schedule from NVS into the list, tried three times: an empty list in its place would run nothing,
// and be saved over. Once read, a mark of another list beside it is let go, see
// settings_tidy_schedule_rises.
bool load_schedule() {
    static std::array<ScheduleEntry, kMaxScheduleEntries> s_read{};
    size_t count = 0;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const esp_err_t err = settings_read_schedule(s_read.data(), s_read.size(), count);
        if (err == ESP_OK) {
            // The marks first, while the portal still refuses to save: a save in between would have its
            // own marks written over with those of the list read here.
            const esp_err_t tidy = settings_tidy_schedule_rises();
            if (tidy != ESP_OK) {
                ESP_LOGW(TAG, "the rise marks were left as they are: %s", esp_err_to_name(tidy));
            }
            xSemaphoreTake(s_lock, portMAX_DELAY);
            std::copy_n(s_read.begin(), count, s_entries.begin());
            s_entry_count = count;
            s_next.minute = -1;
            ++s_schedule_revision;
            s_schedule_read.store(true);
            xSemaphoreGive(s_lock);
            ESP_LOGI(TAG, "%u actions loaded", static_cast<unsigned>(count));
            return true;
        }
        ESP_LOGW(TAG, "reading the schedule failed: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGE(TAG, "the schedule could not be read: it is tried again every minute, and the portal holds it back until then");
    return false;
}

// The first minute from first to last whose step starts the playlist while the list is not read yet, and
// turns the garland on or leaves it, or -1: the minutes from it on wait for the list. One that turns it
// off in the same minute is done now, as off, the start of the playlist with it lost: what it comes to is
// known without the list. None waits while the list is read.
int64_t first_minute_held_by_playlist(int64_t first, int64_t last) {
    if (!playlist_is_stale()) {
        return -1;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t held = -1;
    for (int64_t minute = first; minute <= last && held < 0; ++minute) {
        const ResolvedAction action = resolve_minutes(s_entries.data(), s_entry_count, minute, minute, to_local_minute);
        if (action.playlist && action.power != PowerStep::Off) {
            held = minute;
        }
    }
    xSemaphoreGive(s_lock);
    return held;
}

// The minutes first to last done, but for a playlist not read yet, whose start nothing done now can stand
// for: what they come to without it, as a minute turning off does.
void apply_minutes(State& state, int64_t first, int64_t last) {
    const ResolvedAction action = resolve_under_lock(first, last);
    apply(state, playlist_is_stale() ? action.with_playlist(false) : action);
}

void scheduler_task(void* arg) {
    State& state = *static_cast<State*>(arg);
    s_task = xTaskGetCurrentTaskHandle();
    state.set_power_listener(wake_to_save);
    int64_t last_minute = -1;
    int64_t choice_minute = -1;
    int64_t first_known_epoch = 0;
    bool tried_choice_time = false;
    uint32_t choice_time_tried_ms = 0;
    // From what flash held, not the state now: the buttons started before this task, and a choice of
    // theirs is a change to save.
    uint16_t real_effect = s_saved_at_start.effect;
    MemorySaveTimer memory_timer(s_saved_at_start);
    uint32_t schedule_tried_ms = uptime_ms();
    uint32_t remembered_tried_ms = uptime_ms();
    // The start's replay, stopped before a step that waits for the playlist, goes on from there.
    int64_t replay_from = -1;

    while (true) {
        // A turn on or off is saved right away, not at the next look round.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kPollMs));
        playlist_retry_if_stale(state, uptime_ms());
        looks_retry_if_unread(state, uptime_ms());
        time_zone_retry_if_unread(uptime_ms());
        if (s_remembered_unread.load() && uptime_ms() - remembered_tried_ms >= kRememberedRetryMs) {
            remembered_tried_ms = uptime_ms();
            if (read_remembered_late(state, memory_timer, real_effect)) {
                last_minute = -1;
                replay_from = -1;
                s_replay_pending = true;
            }
        }
        if (!s_schedule_read.load() && uptime_ms() - schedule_tried_ms >= kScheduleRetryMs) {
            schedule_tried_ms = uptime_ms();
            load_schedule();
        }
        // Before the mark is taken: a choice after this is not in what this pass saves.
        const uint32_t choice_seen = state.owner_choice_ms();
        if (state.take_owner_choice()) {
            choice_minute = minute_of_last_choice(state);
            memory_timer.note_choice();
        }
        // Before a replay not begun too, as before a step waiting for the playlist: dated before its steps.
        remember_settings(state, memory_timer, real_effect);
        // A restart waits for this, a little: see scheduler_saved_before_restart.
        if (s_restart_save.load() != RestartSave::None) {
            const Remembered held = remembered_from(state.snapshot(), real_effect);
            if (memory_timer.holds(held)) {
                // The value first: a restart that sees Held reads the value it is of, or a newer one.
                s_restart_held.store(pack_remembered(held));
                s_restart_held_choice.store(choice_seen);
                s_restart_save.store(RestartSave::Held);
            } else {
                s_restart_save.store(RestartSave::Failed);
            }
        }
        if (time_take_ntp_arrival()) {
            if (rtc_present()) {
                time_pass_ntp_to_rtc();
            }
            renew_certificate_if_due(state);
        }
        if (!time_is_synced()) {
            continue;
        }
        const int64_t now = now_in_minutes();
        // The first time known since the start, whatever waits: a choice made with no clock counts as
        // made then, and steps after it still come.
        if (first_known_epoch == 0) {
            first_known_epoch = note_first_known_epoch();
        }

        const Snapshot seen = state.snapshot();
        state.set_action_soon(action_comes_soon(now, seen.playlist_size > 0, seen.catalog_revision));
        // Nothing is done of a schedule not read yet, nor while a saved time zone is not read: its steps are in
        // local time, and UTC meanwhile would do them hours off, a step from before the owner's choice among
        // them. The minutes wait, and once both are read they are done as at a start, the owner's choices
        // outranking the steps before them; a save meanwhile is dated before them, see first_minute_waiting.
        if (!s_schedule_read.load() || !time_zone_ready()) {
            continue;
        }
        // The last minute done this pass: before a step starting a playlist not read yet, which would come
        // to nothing and count as done, the minutes stop, and the rest wait for the list.
        int64_t done = now;
        if (last_minute < 0) {
            // What the garland was left as has already come back, and outranks every step from
            // before it. Only those that came due with no power are still to do: without them it
            // could stay dark until the next "on" comes around tomorrow. A choice saved with no clock
            // counts as made at the first time known; one made since the start, saved or still waiting
            // to be, is newer than the one saved, and the steps up to it yield to it.
            // Once begun, the replay goes on from where it stopped, whatever its own steps saved meanwhile
            // as the choice, which would put the bound past the steps still to do.
            int64_t first = replay_from >= 0 ? replay_from : replay_start(first_known_epoch, now);
            s_replay_pending = false;
            // No more than the day the replay ever takes, a clock set late or corrected in between too.
            if (first >= 0) {
                first = std::max(first, now - kReplayMinutes + 1);
            }
            const int64_t chosen = state.owner_chose() ? minute_of_last_choice(state) : -1;
            if (first >= 0 && chosen >= first) {
                first = chosen + 1;
            }
            const int64_t held = first >= 0 && first <= now ? first_minute_held_by_playlist(first, now) : -1;
            s_steps_wait_from.store(held);
            if (held >= 0) {
                // Done up to the step that waits for the list, and the rest of the replay from it once it
                // is read: caught up later instead, it would lose the day.
                if (first < held) {
                    apply_minutes(state, first, held - 1);
                }
                replay_from = held;
                continue;
            }
            if (first >= 0 && first <= now) {
                apply_minutes(state, first, now);
            }
        } else if (now > last_minute) {
            const int64_t first = catch_up_first_minute(last_minute, now, choice_minute);
            const int64_t held = first <= now ? first_minute_held_by_playlist(first, now) : -1;
            s_steps_wait_from.store(held);
            const int64_t until = held >= 0 ? held - 1 : now;
            if (first <= until) {
                apply_minutes(state, first, until);
            }
            done = held >= 0 ? std::max(last_minute, held - 1) : now;
        }
        // A choice not read yet has no time known to be missing: the record may hold one.
        const bool holds = !s_remembered_unread.load() && memory_timer.holds(remembered_from(state.snapshot(), real_effect));
        if (choice_time_is_due(s_choice_epoch, holds, tried_choice_time, uptime_ms() - choice_time_tried_ms)) {
            tried_choice_time = true;
            choice_time_tried_ms = uptime_ms();
            give_the_choice_a_time(first_known_epoch);
        }
        // A clock that stepped back simply waits; nothing fires twice for it.
        last_minute = last_minute < 0 ? done : std::max(last_minute, done);
        if (now < last_minute - kMaxCatchUpMinutes) {
            last_minute = now;
        }
        s_done_minute.store(last_minute);
    }
}

}  // namespace

void schedule_forget_next_action() {
    forget_next_action();
}

bool remembered_apply(State& state, const StoredSettings& remembered) {
    const uint8_t brightness = remembered_brightness_is_valid(remembered.brightness_percent) ? remembered.brightness_percent : 0;
    const uint8_t speed = manual_speed_x4_is_valid(remembered.speed_x4) ? remembered.speed_x4 : 0;
    return state.restore_unless_chosen(brightness, speed, catalog_find(remembered.effect_id), remembered.playing != 0 ? kPowerOn : kPowerOff);
}

uint32_t choice_clock_ms() {
    return uptime_ms();
}

bool start_scheduler(State& state, int64_t choice_epoch, const Remembered& saved, bool remembered_unread) {
    s_remembered_unread.store(remembered_unread);
    s_saved_at_start = saved;
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    // From anywhere, so that a page left open across a restart does not match by chance.
    s_schedule_revision = esp_random();
    load_schedule();
    s_choice_epoch = choice_epoch;
    s_restart_state = &state;
    return xTaskCreate(scheduler_task, "scheduler", kStackBytes, &state, kPriority, nullptr) == pdPASS;
}


bool schedule_next(char* when, size_t when_size, ResolvedAction& action, bool playlist_ready, uint16_t catalog) {
    if (!time_is_synced()) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const int64_t now_minute = now_in_minutes();
    hold_to_catalog(catalog);
    refresh_next_action(now_minute);
    if (s_summary.minute != now_minute || s_summary.revision != s_schedule_revision || s_summary.ready != playlist_ready) {
        int64_t minute = s_next.found ? s_next.action_minute : -1;
        ResolvedAction found;
        if (minute >= 0) {
            // As the step will come out, and as the timeline says it: see ResolvedAction::with_playlist. One
            // that only starts an empty playlist does nothing, and the next that does is the one to tell.
            found = s_next.action.with_playlist(playlist_ready);
            if (!found.does_something()) {
                // Within the week the summary speaks of, which next_action_minute walks too.
                minute = next_doing_minute(s_entries.data(), s_entry_count, minute, now_minute + 7 * 1440, to_local_minute, playlist_ready,
                                           found);
            }
        }
        s_summary = SummaryCache{now_minute, s_schedule_revision, playlist_ready, minute, found};
    }
    int64_t minute = s_summary.action_minute;
    action = s_summary.action;
    xSemaphoreGive(s_lock);
    if (minute < 0) {
        return false;
    }
    const time_t seconds = static_cast<time_t>(minute * 60);
    tm local = {};
    localtime_r(&seconds, &local);
    return std::strftime(when, when_size, "%a %d %b, %H:%M", &local) != 0;
}

bool schedule_next_step(bool playlist_ready, uint16_t catalog, int64_t& minute, ResolvedAction& action) {
    // Nothing is known of when while the clock, the list or the zone is not: steps are in local time.
    if (!time_is_synced() || !s_schedule_read.load() || !time_zone_ready()) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const int64_t now_minute = now_in_minutes();
    hold_to_catalog(catalog);
    refresh_next_action(now_minute);
    if (s_saver_step.minute != now_minute || s_saver_step.revision != s_schedule_revision || s_saver_step.ready != playlist_ready) {
        int64_t found = s_next.found ? s_next.action_minute : -1;
        ResolvedAction does;
        if (found >= 0) {
            // As it will come out: one that only starts an empty playlist does nothing, and is passed over.
            does = s_next.action.with_playlist(playlist_ready);
            if (!does.does_something()) {
                found = next_doing_minute(s_entries.data(), s_entry_count, found, now_minute + kActionSoonMinutes, to_local_minute,
                                          playlist_ready, does);
            }
        }
        s_saver_step = SaverCache{now_minute, s_schedule_revision, playlist_ready, action_is_soon(found, now_minute) ? found : -1, does};
    }
    minute = s_saver_step.step_minute;
    action = s_saver_step.action;
    xSemaphoreGive(s_lock);
    return minute >= 0;
}

bool schedule_last_step(uint32_t& at_ms, ResolvedAction& action) {
    if (s_lock == nullptr) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool done = s_step_done;
    at_ms = s_step_done_ms;
    action = s_step_done_action;
    xSemaphoreGive(s_lock);
    return done;
}

int schedule_timeline_now(TimelineEvent* out, size_t max_events, bool playlist_ready, int64_t* now_epoch) {
    if (!s_schedule_read.load()) {
        return -2;
    }
    if (!time_is_synced()) {
        return -1;
    }
    // A copy, as the walk takes a day of conversions and nothing else should wait for it. Static, off
    // the handler's stack: the portal runs one handler at a time.
    static std::array<ScheduleEntry, kMaxScheduleEntries> entries{};
    const size_t count = schedule_copy(entries.data());
    // One reading of the clock for the walk and for the answer: NTP may move it in between.
    const auto epoch = static_cast<int64_t>(std::time(nullptr));
    if (now_epoch != nullptr) {
        *now_epoch = epoch;
    }
    // From the last minute the scheduler has done, to a day from now: a minute begun and not done yet is
    // still to come, as are the few it catches up after waiting, and one done before a clock stepped
    // back is not to come again. Before its first pass, from the minute begun.
    const int64_t now_minute = epoch / 60;
    const int64_t done = s_done_minute.load();
    // A clock stepped back further than the scheduler waits for is taken as it will take it at its next
    // pass, from now, or the day would show empty until then.
    const int64_t after = done < 0 || done > now_minute + kMaxCatchUpMinutes ? now_minute - 1 : std::max(done, now_minute - kMaxCatchUpMinutes);
    return static_cast<int>(
        schedule_timeline(entries.data(), count, after, to_local_minute, out, max_events, playlist_ready, now_minute + kTimelineMinutes));
}

size_t schedule_copy(ScheduleEntry* out) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const size_t count = s_entry_count;
    std::copy_n(s_entries.begin(), count, out);
    xSemaphoreGive(s_lock);
    return count;
}

esp_err_t schedule_replace(const ScheduleEntry* entries, size_t count) {
    // A list not read yet is not replaced from a page that never saw it.
    if (!s_schedule_read.load()) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(settings_save_schedule(entries, count), TAG, "save");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    std::copy_n(entries, count, s_entries.begin());
    s_entry_count = count;
    s_next.minute = -1;
    ++s_schedule_revision;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

void scheduler_save_before_restart() {
    RestartSave none = RestartSave::None;
    s_restart_save.compare_exchange_strong(none, RestartSave::Asked);
    wake_to_save();
}

bool scheduler_saved_before_restart(uint32_t wait_ms) {
    if (s_task == nullptr) {
        // No scheduler, so nothing is remembered at all, and nothing is lost by the restart.
        return true;
    }
    // Its own task would wait for itself, which saved what it had a moment ago.
    if (xTaskGetCurrentTaskHandle() == s_task) {
        return false;
    }
    for (uint32_t waited = 0;; waited += kRestartSaveStepMs) {
        // Held of an earlier pass says nothing of a change since, so it counts only if it is what is chosen.
        if (s_restart_save.load() == RestartSave::Held && restart_held_matches_now()) {
            return true;
        }
        if (waited >= wait_ms) {
            return false;
        }
        wake_to_save();
        vTaskDelay(pdMS_TO_TICKS(kRestartSaveStepMs));
    }
}

int64_t schedule_steps_done_until() {
    const int64_t done = s_done_minute.load();
    return done < 0 ? 0 : (done + 1) * 60;
}

bool schedule_is_read() {
    return s_schedule_read.load();
}

uint32_t schedule_revision() {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const uint32_t revision = s_schedule_revision;
    xSemaphoreGive(s_lock);
    return revision;
}

}  // namespace garland
