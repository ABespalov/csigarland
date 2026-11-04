/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Keeps the state's playlist resolved from the list saved in NVS, also after a failed read
 * Contributors: None
 */

#include "playlist_resolver.h"

#include <atomic>
#include <mutex>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "settings.h"

namespace garland {
namespace {

const char* const TAG = "playlist";

constexpr int kReadAttempts = 3;
constexpr uint32_t kReadRetryMs = 50;
constexpr uint32_t kStaleRetryMs = 5000;

// One reading and its applying at a time, of the scheduler's task and the portal's: an older reading
// applied after a newer list would put the older back. Never the Lighting task's.
std::mutex s_lock;
std::atomic<bool> s_stale{false};
// A resume of a list left playing, waiting for the list to be read: under s_lock.
bool s_resume = false;
// Whether the list was left playing could not be read at the start: tried again as the list is. Under s_lock.
bool s_playing_unknown = false;
// How many effects the list holds as saved, those the catalog lacks among them. Under s_lock.
size_t s_saved_count = 0;
uint32_t s_last_try_ms = 0;
// Static: too large for a task's stack, and used only under s_lock.
char s_text[kPlaylistTextBytes];

// Under s_lock: a list saved with effects, none of which the catalog holds now, as after an update of the
// effects. A resume of it waits until one of them is back, as it plays then.
bool nothing_to_play_yet(const State& state) {
    return s_saved_count > 0 && state.snapshot().playlist_size == 0;
}

void apply_locked(State& state, const PlaylistSettings& playlist) {
    s_saved_count = playlist.count;
    playlist_apply(state, playlist);
}

// Under s_lock, once the state holds the list as saved: a resume that waited for it is done now, unless a
// choice of the owner's since the start outranks what was left, or the list has nothing here to play yet.
void read_now_locked(State& state) {
    s_stale.store(false);
    if (s_resume && !nothing_to_play_yet(state)) {
        s_resume = false;
        if (state.resume_playlist_unless_chosen()) {
            ESP_LOGI(TAG, "the playlist read late plays on, as it was left");
        }
    }
}

// Under s_lock: whether the list was left playing, read again. True once it is known.
bool read_playing_locked(State& state) {
    bool playing = false;
    const esp_err_t read = settings_read_playlist_playing(playing);
    if (read != ESP_OK) {
        ESP_LOGW(TAG, "reading whether the playlist played failed again: %s", esp_err_to_name(read));
        return false;
    }
    s_playing_unknown = false;
    if (playing) {
        s_resume = true;
        if (!s_stale.load()) {
            read_now_locked(state);
        }
    }
    return true;
}

// Under s_lock. True once the state holds the list NVS holds, or none was ever saved.
bool resolve_locked(State& state) {
    for (int attempt = 0; attempt < kReadAttempts; ++attempt) {
        const esp_err_t read = settings_read_playlist(s_text, sizeof(s_text));
        if (read == ESP_OK) {
            apply_locked(state, playlist_from_text(s_text));
        } else if (read == ESP_ERR_NVS_NOT_FOUND) {
            s_saved_count = 0;
        }
        if (read == ESP_OK || read == ESP_ERR_NVS_NOT_FOUND) {
            read_now_locked(state);
            return true;
        }
        ESP_LOGW(TAG, "reading the playlist failed: %s", esp_err_to_name(read));
        vTaskDelay(pdMS_TO_TICKS(kReadRetryMs));
    }
    s_stale.store(true);
    return false;
}

}  // namespace

void playlist_resolve(State& state) {
    const std::lock_guard<std::mutex> guard(s_lock);
    if (!resolve_locked(state)) {
        ESP_LOGE(TAG, "the playlist could not be read: it is tried again until it is");
    }
}

void playlist_retry_if_stale(State& state, uint32_t now_ms) {
    if (now_ms - s_last_try_ms < kStaleRetryMs) {
        return;
    }
    const std::lock_guard<std::mutex> guard(s_lock);
    if (!s_stale.load() && !s_playing_unknown) {
        return;
    }
    s_last_try_ms = now_ms;
    if (s_playing_unknown) {
        read_playing_locked(state);
    }
    if (s_stale.load() && resolve_locked(state)) {
        ESP_LOGI(TAG, "the playlist was read, and holds again as saved");
    }
}

void playlist_apply_if_stale(State& state, const PlaylistSettings& read) {
    const std::lock_guard<std::mutex> guard(s_lock);
    if (s_stale.load()) {
        apply_locked(state, read);
        read_now_locked(state);
    }
}

void playlist_apply_saved(State& state, const PlaylistSettings& saved) {
    const std::lock_guard<std::mutex> guard(s_lock);
    apply_locked(state, saved);
    read_now_locked(state);
}

bool playlist_is_stale() {
    return s_stale.load();
}

void playlist_playing_unknown() {
    const std::lock_guard<std::mutex> guard(s_lock);
    s_playing_unknown = true;
}

bool playlist_mark_is_pending() {
    const std::lock_guard<std::mutex> guard(s_lock);
    return s_playing_unknown || s_resume;
}

void playlist_restore_playing(State& state) {
    for (int attempt = 0; attempt < kReadAttempts; ++attempt) {
        bool playing = false;
        const esp_err_t read = settings_read_playlist_playing(playing);
        if (read == ESP_OK) {
            if (playing) {
                playlist_resume_when_read(state);
            }
            return;
        }
        ESP_LOGW(TAG, "reading whether the playlist played failed: %s", esp_err_to_name(read));
        vTaskDelay(pdMS_TO_TICKS(kReadRetryMs));
    }
    playlist_playing_unknown();
}

void playlist_resume_when_read(State& state) {
    const std::lock_guard<std::mutex> guard(s_lock);
    if (s_stale.load() || nothing_to_play_yet(state)) {
        s_resume = true;
    } else {
        state.resume_playlist();
    }
}

}  // namespace garland
