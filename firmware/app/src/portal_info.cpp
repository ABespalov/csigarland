/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: What the portal reports about the board, its addresses and the next action
 * Contributors: None
 */

#include "portal_info.h"

#include <cstdio>
#include <cstring>

#include "effect_catalog.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "garland_state.h"
#include "network.h"
#include "ota.h"
#include "esp_log.h"
#include "portal_common.h"
#include "settings.h"
#include "portal_tls.h"
#include "scheduler.h"
#include "scheduler_clock.h"

namespace garland {
namespace {

const char* const TAG = "portal";

esp_err_t send_restarting(httpd_req_t* req, bool restarting) {
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, restarting ? "{\"restarting\":true}" : "{\"restarting\":false}");
}

}  // namespace

void portal_format_ip(uint32_t ip, char (&out)[16]) {
    out[0] = '\0';
    if (ip != 0) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&ip);
        snprintf(out, sizeof(out), "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
    }
}

void portal_format_hostname(char (&out)[kMaxHostnameLength + 7]) {
    snprintf(out, sizeof(out), "%s.local", network_hostname());
}

void portal_add_next_action(cJSON* root, const State& state) {
    cJSON* next = cJSON_AddObjectToObject(root, "next");
    if (next == nullptr) {
        return;
    }
    // The time of day known: the clock set and a saved zone read. Without it no next action is told.
    cJSON_AddBoolToObject(next, "clock", local_time_is_known(time_is_synced(), time_zone_ready()));
    // A schedule not read yet has no next action to tell, and is no empty one: said apart, new after 1.0.0.
    if (!schedule_is_read()) {
        cJSON_AddBoolToObject(next, "unread", true);
        return;
    }
    char when[32];
    ResolvedAction action;
    const Snapshot seen = state.snapshot();
    if (!schedule_next(when, sizeof(when), action, seen.playlist_size > 0, seen.catalog_revision)) {
        return;
    }
    cJSON_AddStringToObject(next, "at", when);
    if (action.power != PowerStep::None) {
        cJSON_AddStringToObject(next, "power", power_step_name(action.power));
    }
    if (action.brightness_percent != 0) {
        cJSON_AddNumberToObject(next, "brightness", action.brightness_percent);
    }
    if (action.speed_x4 != 0) {
        cJSON_AddNumberToObject(next, "speed", action.speed_x4 / 4.0);
    }
    if (action.playlist) {
        cJSON_AddBoolToObject(next, "playlist", true);
    } else if (action.effect >= 0) {
        cJSON_AddStringToObject(next, "effect_name", catalog_name(static_cast<size_t>(action.effect)).c_str());
    }
}

// Body: {"name": "my-garland"}. The portal's certificate holds the name and is made at a start, so
// a new name ends in one, though the root stays, as it vouches for all of .local. While on trial
// that would count against the new firmware, so it has to wait.
esp_err_t portal_handle_hostname_set(httpd_req_t* req) {
    const JsonPtr root = portal_read_json(req);
    if (root == nullptr) {
        return ESP_OK;
    }
    const cJSON* name = cJSON_GetObjectItemCaseSensitive(root.get(), "name");
    if (!cJSON_IsString(name) || !hostname_is_valid(name->valuestring)) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "the name must be 1-32 lowercase letters, digits or hyphens, with no hyphen at either end");
    }
    if (std::strcmp(name->valuestring, network_hostname()) == 0) {
        return send_restarting(req, false);
    }
    if (ota_on_trial()) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, "the firmware is still on trial: try again in a minute");
    }
    const esp_err_t err = network_set_hostname(name->valuestring);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving the name failed: %s", esp_err_to_name(err));
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not save");
    }
    const esp_err_t sent = send_restarting(req, true);
    ota_restart_soon();
    return sent;
}

// Body: {"domain": "garland.example.com"}, or empty for none. The keys stay; new certificates are
// made at a start, so this ends in one.
esp_err_t portal_handle_certificate_domain_set(httpd_req_t* req) {
    const JsonPtr root = portal_read_json(req);
    if (root == nullptr) {
        return ESP_OK;
    }
    const cJSON* domain = cJSON_GetObjectItemCaseSensitive(root.get(), "domain");
    if (!cJSON_IsString(domain) || !tls_domain_is_valid(domain->valuestring)) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, "one domain, such as garland.example.com, one fixed IP address, or nothing");
    }
    if (ota_on_trial()) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, "the firmware is still on trial: try again in a minute");
    }
    const esp_err_t err = settings_save_tls_domain(domain->valuestring);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving the certificate's domain failed: %s", esp_err_to_name(err));
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not save");
    }
    const esp_err_t sent = send_restarting(req, true);
    ota_restart_soon();
    return sent;
}

// Open to anyone: it is public by nature, and it is needed before a browser can trust the rest.
esp_err_t portal_handle_root_certificate(httpd_req_t* req) {
    const TlsIdentity* const identity = portal_tls_identity_loaded();
    if (identity == nullptr) {
        return portal_send_err(req, HTTPD_404_NOT_FOUND, "no certificate");
    }
    httpd_resp_set_type(req, "application/x-x509-ca-cert");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"csigarland-root.crt\"");
    return httpd_resp_sendstr(req, identity->root_certificate);
}

esp_err_t portal_handle_info(httpd_req_t* req) {
    const Snapshot s = static_cast<const State*>(req->user_ctx)->snapshot();
    const JsonPtr root(cJSON_CreateObject());
    if (root == nullptr) {
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }
    esp_chip_info_t chip = {};
    esp_chip_info(&chip);
    char revision[32];
    snprintf(revision, sizeof(revision), "ESP32-C6 v%d.%d", chip.revision / 100, chip.revision % 100);
    cJSON_AddStringToObject(root.get(), "model", "Waveshare ESP32-C6-LCD-1.3");
    cJSON_AddStringToObject(root.get(), "chip", revision);
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_text[18];
    snprintf(mac_text, sizeof(mac_text), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(root.get(), "mac", mac_text);

    cJSON_AddNumberToObject(root.get(), "ram_total", static_cast<double>(heap_caps_get_total_size(MALLOC_CAP_INTERNAL)));
    cJSON_AddNumberToObject(root.get(), "ram_free", static_cast<double>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    cJSON_AddNumberToObject(root.get(), "ram_lowest", static_cast<double>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
    uint32_t flash_bytes = 0;
    esp_flash_get_size(nullptr, &flash_bytes);
    cJSON_AddNumberToObject(root.get(), "flash_total", flash_bytes);
    cJSON_AddNumberToObject(root.get(), "firmware_slot", static_cast<double>(ota_slot_bytes()));
    cJSON_AddNumberToObject(root.get(), "firmware_used", static_cast<double>(ota_running_image_bytes()));

    const NetworkStatus status = network_status();
    char address[16];
    portal_format_ip(status.ip, address);
    char hostname[kMaxHostnameLength + 7];
    portal_format_hostname(hostname);
    cJSON_AddStringToObject(root.get(), "ip", address);
    cJSON_AddBoolToObject(root.get(), "setup_network", status.access_point_on);
    cJSON_AddStringToObject(root.get(), "hostname", hostname);
    const TlsIdentity* const tls = portal_tls_identity_loaded();
    // What runs, not what could: certificates can be in place with the server failed.
    cJSON_AddBoolToObject(root.get(), "https", portal_https_running());
    cJSON_AddBoolToObject(root.get(), "led_output", !s.output_failed);
    if (tls != nullptr) {
        cJSON_AddStringToObject(root.get(), "certificate_names", tls->names);
        cJSON_AddStringToObject(root.get(), "certificate_domain", tls->root_domain);
        cJSON_AddStringToObject(root.get(), "root_fingerprint", tls->root_fingerprint);
        cJSON_AddNumberToObject(root.get(), "certificate_until", static_cast<double>(tls->not_after));
    }

    int effects = 0;
    int favorites = 0;
    for (size_t i = 0; i < catalog_positions(); ++i) {
        if (!catalog_id(i).empty() && !catalog_is_preview(i)) {
            ++effects;
            favorites += s.favorites.test(i) ? 1 : 0;
        }
    }
    cJSON_AddNumberToObject(root.get(), "effects", effects);
    cJSON_AddNumberToObject(root.get(), "favorites", favorites);
    cJSON_AddBoolToObject(root.get(), "power", s.power_requested);
    cJSON_AddStringToObject(root.get(), "effect_name", catalog_name(s.effect).c_str());
    cJSON_AddNumberToObject(root.get(), "brightness", s.brightness_percent);
    cJSON_AddNumberToObject(root.get(), "speed", s.speed_x4 / 4.0);
    portal_add_next_action(root.get(), *static_cast<const State*>(req->user_ctx));
    return send_json(req, root.get());
}

}  // namespace garland
