/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The portal API of the playlist and of the schedule's next 24 hours
 * Contributors: None
 */

#include "portal_playlist.h"

#include <cJSON.h>

#include <ctime>

#include "esp_log.h"
#include "garland_state.h"
#include "nvs.h"
#include "playlist_logic.h"
#include "playlist_resolver.h"
#include "portal_common.h"
#include "schedule.h"
#include "scheduler.h"
#include "settings.h"

namespace garland {
namespace {

const char* const TAG = "portal";

constexpr size_t kMaxPlaylistRequestBytes = 1024;  // Sixteen of the longest ids, and the rest.

esp_err_t send_playlist(httpd_req_t* req, const State& state, const PlaylistSettings& playlist, uint32_t revision) {
    const JsonPtr root(playlist_to_json(playlist, state.snapshot().playlist, revision));
    if (root == nullptr) {
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }
    return send_json(req, root.get());
}

// What NVS holds, or the defaults when none was ever saved: the list itself lives there, and the state
// only by position. False, with nothing sent, when one may be there and could not be read: that is no
// empty list, which a backup would keep, and a page save over the real one.
bool read_saved_playlist(PlaylistSettings& playlist, uint32_t& revision) {
    static char s_text[kPlaylistTextBytes];
    const esp_err_t err = settings_read_playlist(s_text, sizeof(s_text), &revision);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "reading the playlist failed: %s", esp_err_to_name(err));
        return false;
    }
    playlist = err == ESP_OK ? playlist_from_text(s_text) : PlaylistSettings{};
    return true;
}

esp_err_t send_unreadable(httpd_req_t* req) {
    return portal_send_status(req, "503 Service Unavailable", "The playlist could not be read: ask again");
}

}  // namespace

esp_err_t portal_handle_playlist_get(httpd_req_t* req) {
    static PlaylistSettings s_playlist;
    uint32_t revision = 0;
    if (!read_saved_playlist(s_playlist, revision)) {
        return send_unreadable(req);
    }
    // What it answers is what the state plays: one resolved stale is resolved from this reading first.
    auto& state = *static_cast<State*>(req->user_ctx);
    playlist_apply_if_stale(state, s_playlist);
    return send_playlist(req, state, s_playlist, revision);
}

// Body: {"effects": [...], "dwell": 60, "cross_fade": 1000, "revision": 7}. Saved first, so a failed save
// changes nothing. A list left empty stops the playlist. With the revision the list was read with, it is
// replaced only if no other page or program replaced it since: 409 if one did.
esp_err_t portal_handle_playlist_set(httpd_req_t* req) {
    // Static: too large for the server's stack, and the server handles one request at a time.
    static char s_body[kMaxPlaylistRequestBytes + 1];
    const JsonPtr root = portal_read_json(req, s_body, kMaxPlaylistRequestBytes);
    if (root == nullptr) {
        return ESP_OK;
    }
    // The list there now: an effect it names that this catalog lacks may be sent back, and is kept.
    static PlaylistSettings s_saved;
    uint32_t saved_revision = 0;
    if (!read_saved_playlist(s_saved, saved_revision)) {
        return send_unreadable(req);
    }
    static PlaylistSettings s_playlist;
    const char* error = nullptr;
    bool given = false;
    uint32_t expected = 0;
    if (!playlist_from_json(root.get(), s_playlist, error, &s_saved) || !playlist_revision_from_json(root.get(), given, expected, error)) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, error);
    }
    static char s_text[kPlaylistTextBytes];
    playlist_to_text(s_playlist, s_text, sizeof(s_text));
    uint32_t revision = 0;
    const esp_err_t err = settings_save_playlist(s_text, given ? &expected : nullptr, &revision);
    if (err == ESP_ERR_INVALID_STATE) {
        return portal_send_status(req, "409 Conflict", "The playlist was changed elsewhere: read it again");
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving the playlist failed: %s", esp_err_to_name(err));
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not save");
    }
    auto& state = *static_cast<State*>(req->user_ctx);
    playlist_apply_saved(state, s_playlist);
    ESP_LOGI(TAG, "playlist saved: %u effects", static_cast<unsigned>(s_playlist.count));
    // What was saved, as sent, with no reading back that could fail.
    return send_playlist(req, state, s_playlist, revision);
}

// Body: {"play": true}. Starting turns the garland on at the list's first effect; stopping keeps
// what plays. A choice of the owner's, so the schedule's steps from before it yield to it. The list is
// read for the answer before anything changes, so an answer of 503 means nothing was done.
esp_err_t portal_handle_playlist_play(httpd_req_t* req) {
    const JsonPtr root = portal_read_json(req);
    if (root == nullptr) {
        return ESP_OK;
    }
    const cJSON* play = cJSON_GetObjectItemCaseSensitive(root.get(), "play");
    if (!cJSON_IsBool(play)) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, "play must be true or false");
    }
    static PlaylistSettings s_playlist;
    uint32_t revision = 0;
    if (!read_saved_playlist(s_playlist, revision)) {
        return send_unreadable(req);
    }
    auto& state = *static_cast<State*>(req->user_ctx);
    playlist_apply_if_stale(state, s_playlist);
    if (!state.play_playlist(cJSON_IsTrue(play))) {
        return portal_send_status(req, "409 Conflict", "The playlist is empty");
    }
    return send_playlist(req, state, s_playlist, revision);
}

// {"synced", "now", "events"}: what the schedule does in the next 24 hours, see schedule_timeline, and the
// garland's own time, from which a client draws the day, as its own clock may differ.
esp_err_t portal_handle_schedule_timeline(httpd_req_t* req) {
    // Static: too large for the server's stack, and the server handles one request at a time.
    static TimelineEvent s_events[kMaxTimelineEvents];
    // What each step comes to on this garland: one starting an empty playlist turns nothing on.
    const bool ready = static_cast<const State*>(req->user_ctx)->snapshot().playlist_size > 0;
    int64_t now = static_cast<int64_t>(std::time(nullptr));
    const int count = schedule_timeline_now(s_events, kMaxTimelineEvents, ready, &now);
    if (count == -2) {
        return portal_send_status(req, "503 Service Unavailable", "The schedule could not be read yet: the garland tries again every minute");
    }
    const JsonPtr root(cJSON_CreateObject());
    cJSON* events = timeline_to_json(s_events, count > 0 ? static_cast<size_t>(count) : 0);
    if (root == nullptr || events == nullptr) {
        cJSON_Delete(events);
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }
    // Whole, or an error: a part left out for want of memory would read as an answer that it is not.
    // The array is the caller's until the object takes it.
    // Whether it is lit, read after the walk, new after 1.0.0: the day and this are one snapshot. A step
    // the scheduler does between them is in the day still to come, and already in this, which draws the
    // same; read before the walk, a step done in between would be in neither.
    const bool lit = static_cast<const State*>(req->user_ctx)->snapshot().power_requested;
    if (cJSON_AddBoolToObject(root.get(), "synced", count >= 0) == nullptr ||
        cJSON_AddNumberToObject(root.get(), "now", static_cast<double>(now)) == nullptr ||
        cJSON_AddBoolToObject(root.get(), "power", lit) == nullptr ||
        !cJSON_AddItemToObject(root.get(), "events", events)) {
        cJSON_Delete(events);
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }
    return send_json(req, root.get());
}

}  // namespace garland