/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The portal API of the garland itself: its state, its LEDs and its effects
 * Contributors: None
 */

#include "portal_effects.h"

#include "playlist_logic.h"

#include <cJSON.h>

#include <cstdio>
#include <cstring>

#include "effect_catalog.h"
#include "effect_store.h"
#include "esp_log.h"
#include "esp_system.h"
#include "garland_state.h"
#include "looks.h"
#include "network_logic.h"
#include "nvs.h"
#include "playlist_resolver.h"
#include "portal_common.h"
#include "schedule.h"
#include "scheduler.h"
#include "settings.h"

namespace garland {
namespace {

const char* const TAG = "portal";

constexpr size_t kEffectJsonBytes = 224;  // The longest id, name and group with the JSON around them.
constexpr size_t kEffectQueryBytes = 64;
constexpr size_t kJsonChunkBytes = 1400;  // About one TCP segment.

const char* probe_id(ProbeStatus status) {
    switch (status) {
        case ProbeStatus::Present:
            return "present";
        case ProbeStatus::Absent:
            return "absent";
        default:
            return "unchecked";
    }
}

esp_err_t send_state(httpd_req_t* req, const State& state) {
    char body[kStateJsonBytes];
    const int length = portal_state_json(state, body, sizeof(body));
    if (length < 0) {
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "state too long");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, length);
}

}  // namespace

int portal_state_json(const State& state, char* out, size_t size) {
    // Before the snapshot, so that a step done after it is at worst still to come by this, never done while
    // the state does not show it yet.
    const int64_t steps_done_until = schedule_steps_done_until();
    const Snapshot s = state.snapshot();
    char speed[kSpeedTextBytes];
    speed_to_text(s.speed_x4, speed);
    const int length = snprintf(
        out, size,
        "{\"power\":%s,\"brightness\":%u,\"effect\":\"%s\",\"effect_name\":\"%s\",\"speed\":%s,\"playlist\":%s,"
        "\"playlist_at\":%u,\"playlist_size\":%u,\"steps_done_until\":%lld,"
        "\"leds\":{\"setting\":%u,\"count\":%u,\"kept\":%u,\"max\":%d,\"probe\":\"%s\",\"estimate\":%u}}",
        s.power_requested ? "true" : "false", s.brightness_percent, catalog_id(s.effect).c_str(),
        catalog_name(s.effect).c_str(), speed, s.playlist ? "true" : "false", s.playlist_at, s.playlist_size,
        static_cast<long long>(steps_done_until),
        s.led_setting, s.led_count, s.led_kept, CSIGARLAND_MAX_LEDS, probe_id(s.probe), s.led_estimate);
    return length < 0 || static_cast<size_t>(length) >= size ? -1 : length;
}

namespace {

// A long list sent in chunks costs no memory for its whole length. Each chunk is three
// writes to the socket, and small writes wait for each other's acknowledgement, so items
// are gathered first: per item that took seconds over a weak link.
class ChunkedJson {
public:
    explicit ChunkedJson(httpd_req_t* req) : req_(req) { httpd_resp_set_type(req, "application/json"); }

    void add(const char* text) {
        const size_t length = std::strlen(text);
        if (used_ + length > sizeof(s_buffer)) {
            flush();
        }
        std::memcpy(s_buffer + used_, text, length);
        used_ += length;
    }

    esp_err_t finish() {
        flush();
        return httpd_resp_send_chunk(req_, nullptr, 0);
    }

private:
    void flush() {
        if (used_ > 0) {
            httpd_resp_send_chunk(req_, s_buffer, used_);
            used_ = 0;
        }
    }

    // Static: the server handles one request at a time. Holds at least one whole item.
    static char s_buffer[kJsonChunkBytes];
    httpd_req_t* req_;
    size_t used_ = 0;
};

char ChunkedJson::s_buffer[kJsonChunkBytes];

// The page knows the list already, so a change answers with no more than that it was made.
esp_err_t send_effect_change(httpd_req_t* req, const char* body) {
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

// The same, once the catalog itself changed, by an upload or a deletion: the next action is worked out
// again, and the playlist resolved again from the ids it was saved by, as an effect uploaded may be one
// it names, which a start left out while it was missing. A reading that fails is tried again until it
// is, and until then GET and Play of the playlist resolve it first.
esp_err_t send_catalog_change(httpd_req_t* req, const char* body) {
    auto& state = *static_cast<State*>(req->user_ctx);
    state.note_catalog_changed();
    playlist_resolve(state);
    return send_effect_change(req, body);
}

int read_effect_index(const cJSON* root) {
    const cJSON* id = cJSON_GetObjectItemCaseSensitive(root, "id");
    return cJSON_IsString(id) ? catalog_find(id->valuestring) : -1;
}

esp_err_t save_favorites(State& state, const EffectMask& favorites) {
    static char s_text[kFavoritesTextBytes];
    // Nothing is saved or changed for a list too long to save whole: kept in part, the rest would be
    // gone at the next start, with nothing said.
    if (!favorites_to_text(favorites, s_text, sizeof(s_text))) {
        ESP_LOGW(TAG, "too many favourites to save");
        return ESP_ERR_INVALID_SIZE;
    }
    const esp_err_t err = settings_save_favorites(s_text);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving favourites failed: %s", esp_err_to_name(err));
        return err;
    }
    state.set_favorites(favorites);
    return ESP_OK;
}

// The id, from "?id=...". On failure the error response is already sent.
bool read_id_query(httpd_req_t* req, char (&id)[kMaxEffectIdLength + 1]) {
    char query[kEffectQueryBytes];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "id", id, sizeof(id)) == ESP_OK) {
        return true;
    }
    portal_send_err(req, HTTPD_400_BAD_REQUEST, "needs an id of 1-32 characters");
    return false;
}

bool is_scheduled(const char* id) {
    static ScheduleEntry s_entries[kMaxScheduleEntries];
    const size_t count = schedule_copy(s_entries);
    for (size_t i = 0; i < count; ++i) {
        if (std::strcmp(s_entries[i].effect, id) == 0) {
            return true;
        }
    }
    return false;
}

// Whether the list saved names id, by its id: the state's positions may be stale, see playlist_resolver.h.
// False with nothing named when none was saved; unreadable when one may be there and was not read.
enum class Named { No, Yes, Unreadable };
Named playlist_saved_names(const char* id) {
    static char s_text[kPlaylistTextBytes];
    const esp_err_t err = settings_read_playlist(s_text, sizeof(s_text));
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return Named::No;
    }
    if (err != ESP_OK) {
        return Named::Unreadable;
    }
    return playlist_names(playlist_from_text(s_text), id) ? Named::Yes : Named::No;
}

}  // namespace

esp_err_t portal_handle_state(httpd_req_t* req) {
    return send_state(req, *static_cast<const State*>(req->user_ctx));
}

// Body: {"brightness": 40, "speed": 1.5}, either or both. In standby too, as the buttons do not
// allow: it turns nothing on, and the next turn on shows it.
esp_err_t portal_handle_mode_set(httpd_req_t* req) {
    const JsonPtr root = portal_read_json(req);
    if (root == nullptr) {
        return ESP_OK;
    }
    const cJSON* brightness = cJSON_GetObjectItemCaseSensitive(root.get(), "brightness");
    const cJSON* speed = cJSON_GetObjectItemCaseSensitive(root.get(), "speed");
    if (brightness == nullptr && speed == nullptr) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, "needs a brightness or a speed");
    }
    uint8_t percent = 0;
    if (brightness != nullptr) {
        if (!cJSON_IsNumber(brightness) || brightness->valuedouble != brightness->valueint ||
            !brightness_percent_is_valid(brightness->valueint)) {
            return portal_send_err(req, HTTPD_400_BAD_REQUEST, "brightness must be 1 to 100");
        }
        percent = static_cast<uint8_t>(brightness->valueint);
    }
    uint8_t speed_x4 = 0;
    if (speed != nullptr) {
        speed_x4 = cJSON_IsNumber(speed) ? manual_speed_x4_from_value(speed->valuedouble) : 0;
        if (speed_x4 == 0) {
            return portal_send_err(req, HTTPD_400_BAD_REQUEST, "speed must be 0.25 to 5 in steps of 0.25");
        }
    }
    auto& state = *static_cast<State*>(req->user_ctx);
    state.note_owner_choice();
    state.apply_action(percent, speed_x4, -1, -1);
    return send_state(req, state);
}

// Body: {"on": true}. Turning on keeps the effect, brightness and speed as they were.
esp_err_t portal_handle_power(httpd_req_t* req) {
    const JsonPtr root = portal_read_json(req);
    if (root == nullptr) {
        return ESP_OK;
    }
    const cJSON* on = cJSON_GetObjectItemCaseSensitive(root.get(), "on");
    if (!cJSON_IsBool(on)) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, "needs on, true or false");
    }
    auto& state = *static_cast<State*>(req->user_ctx);
    state.note_owner_choice();
    state.set_power(cJSON_IsTrue(on));
    return send_state(req, state);
}

// Body: {"count": N}, where 0 returns to automatic detection.
esp_err_t portal_handle_set_leds(httpd_req_t* req) {
    const JsonPtr root = portal_read_json(req);
    if (root == nullptr) {
        return ESP_OK;
    }
    const cJSON* count = cJSON_GetObjectItemCaseSensitive(root.get(), "count");
    const bool is_whole = cJSON_IsNumber(count) && count->valuedouble == count->valueint;
    if (!is_whole || !led_setting_is_valid(count->valueint, CSIGARLAND_MAX_LEDS)) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, "count out of range");
    }
    const auto setting = static_cast<uint16_t>(count->valueint);
    auto& state = *static_cast<State*>(req->user_ctx);
    const esp_err_t err = looks_save_led_count(state, setting);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving the LED count failed: %s", esp_err_to_name(err));
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not save");
    }
    ESP_LOGI(TAG, "LED count setting: %u", setting);
    return send_state(req, state);
}

esp_err_t portal_handle_effects(httpd_req_t* req) {
    auto& state = *static_cast<State*>(req->user_ctx);
    // Favourites not read are the defaults, which a page would save back, and a backup keep, as the owner's.
    if (!looks_favorites_read(state)) {
        return portal_send_status(req, "503 Service Unavailable", "The favourites could not be read yet: ask again");
    }
    const Snapshot s = state.snapshot();
    ChunkedJson json(req);
    json.add("{\"effects\":[");
    bool first = true;
    for (size_t i = 0; i < catalog_positions(); ++i) {
        const CatalogText id = catalog_id(i);
        if (id.empty() || catalog_is_preview(i)) {
            continue;
        }
        char item[kEffectJsonBytes];
        snprintf(item, sizeof(item),
                 "%s{\"id\":\"%s\",\"name\":\"%s\",\"group\":\"%s\",\"favorite\":%s,\"active\":%s,\"uploaded\":%s,\"built_in\":%s}",
                 first ? "" : ",", id.c_str(), catalog_name(i).c_str(), catalog_group(i), s.favorites.test(i) ? "true" : "false",
                 s.effect == i ? "true" : "false", catalog_is_uploaded(i) ? "true" : "false",
                 catalog_is_built_in(i) ? "true" : "false");
        json.add(item);
        first = false;
    }
    json.add("]}");
    return json.finish();
}

// Effects marked todo in the catalog: names only, nothing to play yet.
esp_err_t portal_handle_planned(httpd_req_t* req) {
    ChunkedJson json(req);
    json.add("{\"planned\":[");
    for (size_t i = 0; i < catalog_planned_size(); ++i) {
        char item[kEffectJsonBytes];
        snprintf(item, sizeof(item), "%s{\"id\":\"%s\",\"name\":\"%s\",\"group\":\"%s\",\"todo\":true}", i > 0 ? "," : "",
                 catalog_planned_id(i), catalog_planned_name(i), catalog_planned_group(i));
        json.add(item);
    }
    json.add("]}");
    return json.finish();
}

// Body: {"id": "fireplace"}. Any effect, favourite or not.
esp_err_t portal_handle_activate(httpd_req_t* req) {
    const JsonPtr root = portal_read_json(req);
    if (root == nullptr) {
        return ESP_OK;
    }
    const int index = read_effect_index(root.get());
    if (index < 0) {
        return portal_send_err(req, HTTPD_404_NOT_FOUND, "unknown effect");
    }
    static_cast<State*>(req->user_ctx)->activate_effect(static_cast<uint16_t>(index));
    return send_effect_change(req, "{\"ok\":true}");
}

// Body: {"id": "fireplace", "favorite": false}.
esp_err_t portal_handle_favorite(httpd_req_t* req) {
    const JsonPtr root = portal_read_json(req);
    if (root == nullptr) {
        return ESP_OK;
    }
    const int index = read_effect_index(root.get());
    const cJSON* favorite = cJSON_GetObjectItemCaseSensitive(root.get(), "favorite");
    if (index < 0 || !cJSON_IsBool(favorite)) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, "needs a known id and a favorite flag");
    }
    auto& state = *static_cast<State*>(req->user_ctx);
    if (!looks_favorites_read(state)) {
        return portal_send_status(req, "503 Service Unavailable", "The favourites could not be read yet: ask again");
    }
    EffectMask updated = state.snapshot().favorites;
    updated.set(static_cast<size_t>(index), cJSON_IsTrue(favorite));
    if (!updated.any()) {
        // The EFFECT button has to have something to switch between.
        return portal_send_status(req, "409 Conflict", "At least one favourite is required");
    }
    const esp_err_t err = save_favorites(state, updated);
    if (err == ESP_ERR_INVALID_SIZE) {
        return portal_send_status(req, "409 Conflict", "Too many favourites to keep: unmark some first");
    }
    if (err != ESP_OK) {
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not save");
    }
    return send_effect_change(req, "{\"ok\":true}");
}

// Query: ?id=my-effect. The file of any effect as it stands on this garland, as a client needs to
// tune or preview one.
esp_err_t portal_handle_effect_read(httpd_req_t* req) {
    char id[kMaxEffectIdLength + 1];
    if (!read_id_query(req, id)) {
        return ESP_OK;
    }
    const int found = catalog_find(id);
    if (found < 0) {
        return portal_send_err(req, HTTPD_404_NOT_FOUND, "no such effect");
    }
    const auto position = static_cast<size_t>(found);
    const char* const file = catalog_is_uploaded(position) ? effect_store_read(id) : catalog_built_in_file(position);
    if (file == nullptr) {
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not read the effect");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, file);
}

// Body: the effect's file. Query: ?id=my-effect. Replaces an effect of the same id.
esp_err_t portal_handle_effect_upload(httpd_req_t* req) {
    char id[kMaxEffectIdLength + 1];
    if (!read_id_query(req, id)) {
        return ESP_OK;
    }
    // Before the body, which goes where they are read: those read now count, the upload taken or not.
    if (effect_store_read_unread()) {
        auto& state = *static_cast<State*>(req->user_ctx);
        state.note_catalog_changed();
        playlist_resolve(state);
    }
    if (effect_store_unread_may_hold(id)) {
        return portal_send_status(req, "503 Service Unavailable", "Uploaded effects saved before could not all be read yet, and one may have this id: ask again");
    }
    if (!portal_read_text(req, effect_store_body(), kMaxEffectFileBytes)) {
        return ESP_OK;
    }
    const char* const problem = effect_store_save(id);
    if (problem != nullptr) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, problem);
    }
    return send_catalog_change(req, "{\"ok\":true}");
}

// Body: the effect's file. Plays it, saving nothing, until another effect is chosen.
esp_err_t portal_handle_effect_preview(httpd_req_t* req) {
    if (!portal_read_text(req, effect_store_body(), kMaxEffectFileBytes)) {
        return ESP_OK;
    }
    const char* const problem = effect_store_preview();
    if (problem != nullptr) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, problem);
    }
    const int position = static_cast<int>(catalog_positions()) - 1;
    static_cast<State*>(req->user_ctx)->activate_effect(static_cast<uint16_t>(position));
    return send_effect_change(req, "{\"ok\":true}");
}

// Query: ?id=my-effect. A built-in effect that was replaced gets its own definition back and
// keeps its place; any other leaves favourites, and the garland moves on if it was playing.
esp_err_t portal_handle_effect_delete(httpd_req_t* req) {
    char id[kMaxEffectIdLength + 1];
    if (!read_id_query(req, id)) {
        return ESP_OK;
    }
    const int found = catalog_find(id);
    if (found < 0 || !catalog_is_uploaded(static_cast<size_t>(found))) {
        return portal_send_err(req, HTTPD_404_NOT_FOUND, "no such uploaded effect");
    }
    const auto position = static_cast<size_t>(found);
    auto& state = *static_cast<State*>(req->user_ctx);
    const bool leaves_the_catalog = !catalog_is_built_in(position);
    // A schedule not read yet may name it.
    if (leaves_the_catalog && !schedule_is_read()) {
        return portal_send_status(req, "503 Service Unavailable", "The schedule could not be read yet, and may use this effect: ask again");
    }
    if (leaves_the_catalog && is_scheduled(id)) {
        return portal_send_status(req, "409 Conflict", "The schedule uses this effect");
    }
    const Named named = leaves_the_catalog ? playlist_saved_names(id) : Named::No;
    if (named == Named::Unreadable) {
        return portal_send_status(req, "503 Service Unavailable", "The playlist could not be read, and may use this effect: ask again");
    }
    if (named == Named::Yes) {
        return portal_send_status(req, "409 Conflict", "The playlist uses this effect");
    }
    // The file first: should that fail, nothing else has changed.
    const char* const problem = effect_store_delete(id);
    if (problem != nullptr) {
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, problem);
    }
    if (leaves_the_catalog) {
        // Favourites not read yet are only defaults, not to be saved over the list there, which, naming an
        // effect gone, is read as if it did not.
        const bool favorites_read = looks_favorites_read(state);
        const Snapshot now = state.snapshot();
        EffectMask updated = now.favorites;
        updated.set(position, false);
        if (now.favorites.test(position)) {
            const EffectMask kept = updated.any() ? updated : catalog_default_favorites();
            // A saved list naming a gone effect is read as if it did not, so failing costs nothing.
            state.set_favorites(kept);
            if (favorites_read) {
                save_favorites(state, kept);
            }
        }
        if (now.effect == position) {
            state.select_first_favorite();
        }
    }
    return send_catalog_change(req, "{\"ok\":true}");
}

esp_err_t portal_handle_effect_store(httpd_req_t* req) {
    const JsonPtr root(cJSON_CreateObject());
    if (root == nullptr) {
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }
    const EffectStoreStatus store = effect_store_status();
    cJSON_AddBoolToObject(root.get(), "available", store.available);
    cJSON_AddNumberToObject(root.get(), "uploaded", static_cast<double>(catalog_uploaded_count()));
    cJSON_AddNumberToObject(root.get(), "max", kMaxUploadedEffects);
    cJSON_AddNumberToObject(root.get(), "max_file_bytes", kMaxEffectFileBytes);
    cJSON_AddNumberToObject(root.get(), "entries_used", static_cast<double>(store.entries_used));
    cJSON_AddNumberToObject(root.get(), "entries_total", static_cast<double>(store.entries_total));
    cJSON_AddNumberToObject(root.get(), "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root.get(), "min_free_heap", esp_get_minimum_free_heap_size());
    return send_json(req, root.get());
}

}  // namespace garland
