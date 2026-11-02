/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The portal API of the settings: Wi-Fi, the schedule, the time and the screen
 * Contributors: None
 */

#include "portal_settings.h"

#include <cJSON.h>

#include <cstring>

#include "esp_log.h"
#include "garland_state.h"
#include "network.h"
#include "network_logic.h"
#include "portal_common.h"
#include "portal_info.h"
#include "rtc.h"
#include "schedule.h"
#include "scheduler.h"
#include "settings.h"

namespace garland {
namespace {

const char* const TAG = "portal";

constexpr size_t kMaxScheduleRequestBytes = 6144;  // A full list of the longest actions fits.

const char* scan_state_id(ScanState state) {
    switch (state) {
        case ScanState::Running:
            return "running";
        case ScanState::Done:
            return "done";
        case ScanState::Failed:
            return "failed";
        default:
            return "idle";
    }
}

const char* clock_source_id(ClockSource source) {
    switch (source) {
        case ClockSource::Module:
            return "module";
        case ClockSource::Ntp:
            return "ntp";
        case ClockSource::Browser:
            return "browser";
        case ClockSource::Manual:
            return "manual";
        case ClockSource::None:
            break;
    }
    return "none";
}

struct SaverName {
    const char* id;
    SaverKind kind;
};

constexpr SaverName kSaverNames[] = {
    {"none", SaverKind::None},
    {"starfield", SaverKind::Starfield},
    {"flying-clock", SaverKind::FlyingClock},
    {"breathing-dot", SaverKind::BreathingDot},
    {"black-screen", SaverKind::BlackScreen},
};

bool is_backlight_percent(const cJSON* item) {
    return cJSON_IsNumber(item) && item->valuedouble == item->valueint && backlight_percent_is_valid(item->valueint);
}

}  // namespace

esp_err_t portal_handle_wifi_status(httpd_req_t* req) {
    const NetworkStatus status = network_status();
    const JsonPtr root(cJSON_CreateObject());
    if (root == nullptr) {
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }
    char ssid[33];
    network_ssid(ssid, sizeof(ssid));
    char address[16];
    portal_format_ip(status.ip, address);
    char hostname[kMaxHostnameLength + 7];
    portal_format_hostname(hostname);
    cJSON_AddBoolToObject(root.get(), "configured", status.has_credentials);
    cJSON_AddBoolToObject(root.get(), "connected", status.connected);
    cJSON_AddBoolToObject(root.get(), "setup_network", status.access_point_on);
    cJSON_AddStringToObject(root.get(), "ssid", ssid);
    cJSON_AddStringToObject(root.get(), "ip", address);
    cJSON_AddStringToObject(root.get(), "hostname", hostname);
    return send_json(req, root.get());
}

// Body: {"ssid": "...", "password": "..."}. An empty ssid forgets the network.
esp_err_t portal_handle_wifi_set(httpd_req_t* req) {
    const JsonPtr root = portal_read_json(req);
    if (root == nullptr) {
        return ESP_OK;
    }
    const cJSON* ssid = cJSON_GetObjectItemCaseSensitive(root.get(), "ssid");
    const cJSON* password = cJSON_GetObjectItemCaseSensitive(root.get(), "password");
    if (!cJSON_IsString(ssid) || (password != nullptr && !cJSON_IsString(password))) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, "needs ssid and password strings");
    }
    const char* password_text = password != nullptr ? password->valuestring : "";
    const size_t ssid_length = std::strlen(ssid->valuestring);
    if (ssid_length != 0 && !wifi_credentials_are_valid(ssid_length, std::strlen(password_text))) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "name must be 1-32 bytes, password empty or 8-63 characters");
    }
    const esp_err_t err = network_set_credentials(ssid->valuestring, password_text);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving Wi-Fi failed: %s", esp_err_to_name(err));
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not apply");
    }
    return portal_handle_wifi_status(req);
}

// The search runs in the background: POST starts one, GET reports how it is going.
esp_err_t portal_handle_wifi_scan(httpd_req_t* req) {
    static ScanEntry s_entries[kMaxScanEntries];
    if (req->method == HTTP_POST) {
        network_scan_start();
    }
    size_t count = 0;
    const ScanState state = network_scan_results(s_entries, count);
    const JsonPtr root(cJSON_CreateObject());
    cJSON* networks = root != nullptr ? cJSON_AddArrayToObject(root.get(), "networks") : nullptr;
    if (networks == nullptr) {
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }
    cJSON_AddStringToObject(root.get(), "state", scan_state_id(state));
    for (size_t i = 0; i < count; ++i) {
        cJSON* item = cJSON_CreateObject();
        if (item == nullptr) {
            break;
        }
        cJSON_AddStringToObject(item, "ssid", s_entries[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", s_entries[i].rssi);
        cJSON_AddBoolToObject(item, "open", s_entries[i].open);
        cJSON_AddItemToArray(networks, item);
    }
    return send_json(req, root.get());
}

esp_err_t portal_handle_schedule_get(httpd_req_t* req) {
    static ScheduleEntry s_entries[kMaxScheduleEntries];
    const size_t count = schedule_copy(s_entries);
    const JsonPtr root(cJSON_CreateObject());
    cJSON* entries = schedule_to_json(s_entries, count);
    if (root == nullptr || entries == nullptr) {
        cJSON_Delete(entries);
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }
    cJSON_AddItemToObject(root.get(), "entries", entries);
    cJSON_AddNumberToObject(root.get(), "max", kMaxScheduleEntries);
    cJSON_AddNumberToObject(root.get(), "revision", schedule_revision());
    portal_add_next_action(root.get());
    return send_json(req, root.get());
}

// Body: {"entries": [...], "revision": 7}. Replaces the whole list, or changes nothing. The
// revision, if given, is of the list the change was made to, so that another page's is not undone.
// The portal's page always gives it; a script that leaves it out means to replace whatever is there.
esp_err_t portal_handle_schedule_set(httpd_req_t* req) {
    // Static: too large for the server's stack, and the server handles one request at a time.
    static char s_body[kMaxScheduleRequestBytes + 1];
    const JsonPtr root = portal_read_json(req, s_body, kMaxScheduleRequestBytes);
    if (root == nullptr) {
        return ESP_OK;
    }
    const cJSON* revision = cJSON_GetObjectItemCaseSensitive(root.get(), "revision");
    if (cJSON_IsNumber(revision) && revision->valuedouble != static_cast<double>(schedule_revision())) {
        return portal_send_status(req, "409 Conflict", "The schedule was changed elsewhere. It has been read again: make your change once more");
    }
    static ScheduleEntry s_entries[kMaxScheduleEntries];
    size_t count = 0;
    const char* error = nullptr;
    if (!schedule_from_json(cJSON_GetObjectItemCaseSensitive(root.get(), "entries"), s_entries, count, error)) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, error);
    }
    const esp_err_t err = schedule_replace(s_entries, count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving the schedule failed: %s", esp_err_to_name(err));
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not save");
    }
    ESP_LOGI(TAG, "schedule saved: %u actions", static_cast<unsigned>(count));
    return portal_handle_schedule_get(req);
}

esp_err_t portal_handle_time_get(httpd_req_t* req) {
    const JsonPtr root(cJSON_CreateObject());
    if (root == nullptr) {
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }
    char now[24];
    time_format_local(now, sizeof(now));
    char zone[49];
    time_zone_get(zone, sizeof(zone));
    cJSON_AddBoolToObject(root.get(), "synced", time_is_synced());
    cJSON_AddStringToObject(root.get(), "now", now);
    cJSON_AddBoolToObject(root.get(), "zone_set", time_zone_is_set());
    cJSON_AddStringToObject(root.get(), "zone", zone);
    const RtcStatus rtc = time_rtc_status();
    cJSON* module = cJSON_AddObjectToObject(root.get(), "rtc");
    cJSON_AddBoolToObject(module, "present", rtc.present);
    cJSON_AddBoolToObject(module, "trusted", rtc.trusted);
    cJSON_AddStringToObject(module, "now", rtc.now);
    cJSON_AddStringToObject(module, "source", clock_source_id(rtc.source));
    return send_json(req, root.get());
}

// Body: {"source": "ntp"}, {"source": "browser", "epoch": 1793469933} or
// {"source": "manual", "local": "2026-10-31T18:05"}, in the garland's time zone.
esp_err_t portal_handle_rtc_set(httpd_req_t* req) {
    const JsonPtr root = portal_read_json(req);
    if (root == nullptr) {
        return ESP_OK;
    }
    if (!rtc_present()) {
        return portal_send_status(req, "409 Conflict", "No clock module");
    }
    const cJSON* source = cJSON_GetObjectItemCaseSensitive(root.get(), "source");
    const cJSON* epoch = cJSON_GetObjectItemCaseSensitive(root.get(), "epoch");
    const cJSON* local = cJSON_GetObjectItemCaseSensitive(root.get(), "local");
    if (!cJSON_IsString(source)) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, "needs a source");
    }
    bool done = false;
    if (std::strcmp(source->valuestring, "ntp") == 0) {
        if (!network_status().connected) {
            return portal_send_status(req, "409 Conflict", "Not on the home network, so no internet time");
        }
        time_sync_now();
        done = true;
    } else if (std::strcmp(source->valuestring, "browser") == 0 && cJSON_IsNumber(epoch)) {
        int64_t seconds = 0;
        if (!rtc_epoch_from_number(epoch->valuedouble, seconds)) {
            return portal_send_err(req, HTTPD_400_BAD_REQUEST, "not a time this clock can hold");
        }
        done = time_set_rtc(seconds, ClockSource::Browser);
    } else if (std::strcmp(source->valuestring, "manual") == 0 && cJSON_IsString(local)) {
        LocalDateTime parsed;
        if (!rtc_parse_local(local->valuestring, parsed)) {
            return portal_send_err(req, HTTPD_400_BAD_REQUEST, "not a date and time");
        }
        done = time_set_rtc_local(parsed, ClockSource::Manual);
    } else {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, "unknown source");
    }
    if (!done) {
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "the clock module did not take that time");
    }
    return portal_handle_time_get(req);
}

// Body: {"zone": "CET-1CEST,M3.5.0,M10.5.0/3"}.
esp_err_t portal_handle_time_set(httpd_req_t* req) {
    const JsonPtr root = portal_read_json(req);
    if (root == nullptr) {
        return ESP_OK;
    }
    const cJSON* zone = cJSON_GetObjectItemCaseSensitive(root.get(), "zone");
    if (!cJSON_IsString(zone) || !time_zone_is_plausible(zone->valuestring)) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, "zone must be a POSIX TZ string");
    }
    const esp_err_t err = time_zone_set(zone->valuestring);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving the time zone failed: %s", esp_err_to_name(err));
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not save");
    }
    return portal_handle_time_get(req);
}

esp_err_t portal_handle_display_get(httpd_req_t* req) {
    const auto& state = *static_cast<const State*>(req->user_ctx);
    const JsonPtr root(cJSON_CreateObject());
    if (root == nullptr) {
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }
    for (const SaverName& name : kSaverNames) {
        if (name.kind == state.saver_kind()) {
            cJSON_AddStringToObject(root.get(), "saver", name.id);
        }
    }
    cJSON_AddNumberToObject(root.get(), "delay", state.saver_delay_seconds());
    cJSON_AddNumberToObject(root.get(), "delay_min", kSaverDelayMinSeconds);
    cJSON_AddNumberToObject(root.get(), "delay_max", kSaverDelayMaxSeconds);
    cJSON_AddNumberToObject(root.get(), "screen_brightness", state.screen_percent());
    cJSON_AddNumberToObject(root.get(), "saver_brightness", state.saver_percent());
    cJSON_AddNumberToObject(root.get(), "brightness_min", kBacklightMinPercent);
    return send_json(req, root.get());
}

// Body: {"saver": "starfield", "delay": 60, "screen_brightness": 100, "saver_brightness": 30}.
esp_err_t portal_handle_display_set(httpd_req_t* req) {
    const JsonPtr root = portal_read_json(req);
    if (root == nullptr) {
        return ESP_OK;
    }
    const cJSON* saver = cJSON_GetObjectItemCaseSensitive(root.get(), "saver");
    const cJSON* delay = cJSON_GetObjectItemCaseSensitive(root.get(), "delay");
    const SaverName* found = nullptr;
    for (const SaverName& name : kSaverNames) {
        if (cJSON_IsString(saver) && std::strcmp(saver->valuestring, name.id) == 0) {
            found = &name;
        }
    }
    if (found == nullptr) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, "unknown screensaver");
    }
    if (!cJSON_IsNumber(delay) || delay->valuedouble != delay->valueint || !saver_delay_is_valid(delay->valueint)) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, "delay must be 10 to 600 seconds");
    }
    const cJSON* screen = cJSON_GetObjectItemCaseSensitive(root.get(), "screen_brightness");
    const cJSON* saver_level = cJSON_GetObjectItemCaseSensitive(root.get(), "saver_brightness");
    if (!is_backlight_percent(screen) || !is_backlight_percent(saver_level)) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, "brightness must be 5 to 100 percent");
    }
    DisplaySettings settings;
    settings.saver = found->kind;
    settings.saver_delay_seconds = static_cast<uint16_t>(delay->valueint);
    settings.screen_percent = static_cast<uint8_t>(screen->valueint);
    settings.saver_percent = static_cast<uint8_t>(saver_level->valueint);
    const esp_err_t err = settings_save_display(settings);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving the screen settings failed: %s", esp_err_to_name(err));
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not save");
    }
    auto& state = *static_cast<State*>(req->user_ctx);
    state.set_backlight_levels(settings.screen_percent, settings.saver_percent);
    // A new type or delay restarts the idle count; a new level alone should show on what
    // is on the screen right now.
    if (settings.saver != state.saver_kind() || settings.saver_delay_seconds != state.saver_delay_seconds()) {
        state.set_saver(settings.saver, settings.saver_delay_seconds);
    }
    return portal_handle_display_get(req);
}

}  // namespace garland
