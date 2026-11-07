/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The portal API of the settings: Wi-Fi, the schedule, the time, the screen and the power budget
 * Contributors: None
 */

#include "portal_settings.h"

#include <cJSON.h>

#include <cstring>

#include "esp_log.h"
#include "garland_state.h"
#include "looks.h"
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
    {"scheduler24", SaverKind::Scheduler24},
};

bool is_backlight_percent(const cJSON* item) {
    return cJSON_IsNumber(item) && item->valuedouble == item->valueint && backlight_percent_is_valid(item->valueint);
}

// A whole number in the body under name that valid takes, into out.
template <typename Value>
bool read_whole(const cJSON* root, const char* name, bool (*valid)(int), Value& out) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsNumber(item) || item->valuedouble != item->valueint || !valid(item->valueint)) {
        return false;
    }
    out = static_cast<Value>(item->valueint);
    return true;
}

esp_err_t send_power_unread(httpd_req_t* req) {
    return portal_send_status(req, "503 Service Unavailable", "The power settings could not be read yet: ask again");
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

static esp_err_t send_schedule_unread(httpd_req_t* req) {
    return portal_send_status(req, "503 Service Unavailable", "The schedule could not be read yet: the garland tries again every minute");
}

esp_err_t portal_handle_schedule_get(httpd_req_t* req) {
    if (!schedule_is_read()) {
        return send_schedule_unread(req);
    }
    static ScheduleEntry s_entries[kMaxScheduleEntries];
    const size_t count = schedule_copy(s_entries);
    const JsonPtr root(cJSON_CreateObject());
    cJSON* entries = schedule_to_json(s_entries, count);
    if (root == nullptr || entries == nullptr) {
        cJSON_Delete(entries);
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }
    cJSON_AddItemToObject(root.get(), "entries", entries);
    // New after 1.0.0: the effects the actions name that this garland lacks.
    cJSON* missing = schedule_missing_json(s_entries, count);
    if (missing == nullptr || !cJSON_AddItemToObject(root.get(), "missing", missing)) {
        cJSON_Delete(missing);
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }
    cJSON_AddNumberToObject(root.get(), "max", kMaxScheduleEntries);
    cJSON_AddNumberToObject(root.get(), "revision", schedule_revision());
    cJSON_AddNumberToObject(root.get(), "schema", kScheduleSchema);
    portal_add_next_action(root.get(), *static_cast<const State*>(req->user_ctx));
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
    if (!schedule_is_read()) {
        return send_schedule_unread(req);
    }
    const cJSON* revision = cJSON_GetObjectItemCaseSensitive(root.get(), "revision");
    if (cJSON_IsNumber(revision) && revision->valuedouble != static_cast<double>(schedule_revision())) {
        return portal_send_status(req, "409 Conflict", "The schedule was changed elsewhere. It has been read again: make your change once more");
    }
    static ScheduleEntry s_entries[kMaxScheduleEntries];
    static ScheduleEntry s_held[kMaxScheduleEntries];
    const size_t held = schedule_copy(s_held);
    // A client of 1.0.0 sends the list as it read it, without the steps it does not know.
    const cJSON* schema = cJSON_GetObjectItemCaseSensitive(root.get(), "schema");
    if ((!cJSON_IsNumber(schema) || schema->valueint < kScheduleSchema) && schedule_has_newer_steps(s_held, held)) {
        return portal_send_status(req, "409 Conflict", "The schedule holds steps this program does not know: update it, or change the schedule on the portal");
    }
    size_t count = 0;
    const char* error = nullptr;
    if (!schedule_from_json(cJSON_GetObjectItemCaseSensitive(root.get(), "entries"), s_entries, count, error, s_held, held)) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, error);
    }
    const esp_err_t err = schedule_replace(s_entries, count);
    if (err == ESP_ERR_INVALID_STATE) {
        return send_schedule_unread(req);
    }
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
    auto& state = *static_cast<State*>(req->user_ctx);
    // Settings not read are the defaults, which a form would save back, and a backup keep, as the owner's.
    if (!looks_display_read(state)) {
        return portal_send_status(req, "503 Service Unavailable", "The screen settings could not be read yet: ask again");
    }
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
    // Whether the clock module answered at the start, which Scheduler24 needs.
    cJSON_AddBoolToObject(root.get(), "clock_module", state.snapshot().clock_module);
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
    auto& state = *static_cast<State*>(req->user_ctx);
    // Every part is saved, and one not read yet would go to the default the page was shown.
    if (!looks_display_read(state)) {
        return portal_send_status(req, "503 Service Unavailable", "The screen settings could not be read yet: ask again");
    }
    // Chosen only with the clock module; one chosen before the module went stays, as the form sends it back.
    if (found->kind == SaverKind::Scheduler24 && !state.snapshot().clock_module && state.saver_kind() != SaverKind::Scheduler24) {
        return portal_send_status(req, "409 Conflict", "Scheduler24 needs the clock module");
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
    state.set_backlight_levels(settings.screen_percent, settings.saver_percent);
    // A new type or delay restarts the idle count; a new level alone should show on what
    // is on the screen right now.
    if (settings.saver != state.saver_kind() || settings.saver_delay_seconds != state.saver_delay_seconds()) {
        state.set_saver(settings.saver, settings.saver_delay_seconds);
    }
    return portal_handle_display_get(req);
}

// The settings, the ranges each takes, and what they come to on the chain in use: the limit, what it
// draws all at full white, and how many LEDs full white leaves undimmed.
esp_err_t portal_handle_led_power_get(httpd_req_t* req) {
    auto& state = *static_cast<State*>(req->user_ctx);
    if (!looks_power_read(state)) {
        return send_power_unread(req);
    }
    const JsonPtr root(cJSON_CreateObject());
    if (root == nullptr) {
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }
    const PowerSettings power = state.power();
    const uint16_t leds = state.snapshot().led_count;
    cJSON_AddNumberToObject(root.get(), "supply_ma", power.supply_ma);
    cJSON_AddNumberToObject(root.get(), "led_white_ma", power.white_ma);
    cJSON_AddNumberToObject(root.get(), "led_idle_ma", power.idle_ma);
    cJSON_AddNumberToObject(root.get(), "margin_percent", power.margin_percent);
    cJSON_AddNumberToObject(root.get(), "board_ma", kBoardReserveMa);
    cJSON_AddNumberToObject(root.get(), "limit_ma", power_limit_ma(power));
    cJSON_AddNumberToObject(root.get(), "leds", leds);
    cJSON_AddNumberToObject(root.get(), "peak_ma", peak_ma(power, leds));
    cJSON_AddNumberToObject(root.get(), "undimmed_leds", undimmed_leds(power));
    cJSON_AddNumberToObject(root.get(), "supply_min", kSupplyMinMa);
    cJSON_AddNumberToObject(root.get(), "supply_max", kSupplyMaxMa);
    cJSON_AddNumberToObject(root.get(), "led_white_min", kLedWhiteMinMa);
    cJSON_AddNumberToObject(root.get(), "led_white_max", kLedWhiteMaxMa);
    cJSON_AddNumberToObject(root.get(), "led_idle_max", kLedIdleMaxMa);
    cJSON_AddNumberToObject(root.get(), "margin_min", kMarginMinPercent);
    cJSON_AddNumberToObject(root.get(), "margin_max", kMarginMaxPercent);
    return send_json(req, root.get());
}

// Body: {"supply_ma": 2000, "led_white_ma": 40, "led_idle_ma": 1, "margin_percent": 20}, all four.
esp_err_t portal_handle_led_power_set(httpd_req_t* req) {
    const JsonPtr root = portal_read_json(req);
    if (root == nullptr) {
        return ESP_OK;
    }
    PowerSettings power;
    if (!read_whole(root.get(), "supply_ma", supply_ma_is_valid, power.supply_ma)) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, "supply_ma must be 500 to 20000");
    }
    if (!read_whole(root.get(), "led_white_ma", led_white_ma_is_valid, power.white_ma)) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, "led_white_ma must be 5 to 100");
    }
    if (!read_whole(root.get(), "led_idle_ma", led_idle_ma_is_valid, power.idle_ma)) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, "led_idle_ma must be 0 to 5");
    }
    if (!read_whole(root.get(), "margin_percent", margin_percent_is_valid, power.margin_percent)) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, "margin_percent must be 5 to 20");
    }
    auto& state = *static_cast<State*>(req->user_ctx);
    const esp_err_t err = looks_save_power(state, power);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving the power settings failed: %s", esp_err_to_name(err));
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not save");
    }
    ESP_LOGI(TAG, "power: supply %u mA, LED %u + %u mA, margin %u%%, limit %lu mA", power.supply_ma, power.white_ma,
             power.idle_ma, power.margin_percent, static_cast<unsigned long>(power_limit_ma(power)));
    return portal_handle_led_power_get(req);
}

}  // namespace garland
