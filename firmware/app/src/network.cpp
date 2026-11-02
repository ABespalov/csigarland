/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Home Wi-Fi, the setup access point, captive DNS and mDNS
 * Contributors: None
 *
 * SSIDs and passwords are never logged: the log carries ASCII only, an SSID can hold
 * any bytes, and a password has no business in a log at all.
 */

#include "network.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "network_logic.h"
#include "scheduler.h"
#include "settings.h"

namespace garland {
namespace {

const char* const TAG = "network";

// Written at startup and then only by the web server's one task, which is also who reads it.
char s_hostname[kMaxHostnameLength + 1] = "csigarland";
constexpr uint8_t kApChannel = 1;
constexpr uint8_t kApMaxClients = 3;
constexpr std::array<uint8_t, 4> kApAddress = {192, 168, 4, 1};

constexpr uint32_t kManagerStackBytes = 4096;
constexpr uint32_t kDnsStackBytes = 3072;
constexpr UBaseType_t kTaskPriority = tskIDLE_PRIORITY + 2;
constexpr uint32_t kRetryPeriodS = 10;
constexpr uint32_t kDnsPort = 53;
constexpr size_t kDnsBufferBytes = 256;
constexpr uint8_t kStationRetriesPerAccessPoint = 3;
constexpr uint32_t kScanChannelMinMs = 120;
constexpr uint32_t kScanChannelMaxMs = 300;  // About 4 s over the 13 channels.
constexpr int64_t kScanWindowUs = 20'000'000;

State* s_state = nullptr;
std::atomic<bool> s_has_credentials{false};
std::atomic<bool> s_connected{false};
std::atomic<bool> s_access_point_on{true};
std::atomic<bool> s_scanning{false};
std::atomic<ScanState> s_scan_state{ScanState::Idle};
std::atomic<bool> s_scan_in_driver{false};
std::atomic<bool> s_scan_done{false};
std::atomic<bool> s_scan_status{false};
// Written by the manager task while s_scan_state is Running, and read only once it is Done.
int s_scan_attempts = 0;
bool s_scan_quieted = false;
int64_t s_scan_started_us = 0;
size_t s_scan_count = 0;
std::array<ScanEntry, kMaxScanEntries> s_scan_entries{};
std::atomic<uint32_t> s_ip{0};
std::atomic<int64_t> s_state_changed_us{0};
// Touched only by the portal's handlers and at startup, before they can run.
WifiCredentials s_credentials{};

void mark_state_change() {
    s_state_changed_us.store(esp_timer_get_time());
}

void on_wifi_event(void*, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_has_credentials.load()) {
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        s_scan_status.store(static_cast<const wifi_event_sta_scan_done_t*>(data)->status == 0);
        s_scan_done.store(true);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_connected.exchange(false)) {
            ESP_LOGW(TAG, "home network lost");
            mark_state_change();
        }
        s_ip.store(0);
        s_state->set_station_ip(0);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto* event = static_cast<const ip_event_got_ip_t*>(data);
        const uint32_t ip = event->ip_info.ip.addr;
        s_ip.store(ip);
        s_state->set_station_ip(ip);
        s_connected.store(true);
        mark_state_change();
        ESP_LOGI(TAG, "home network connected, address " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

esp_err_t apply_station_config() {
    wifi_config_t config = {};
    std::memcpy(config.sta.ssid, s_credentials.ssid, std::strlen(s_credentials.ssid));
    std::memcpy(config.sta.password, s_credentials.password, std::strlen(s_credentials.password));
    // A network can have several access points under one name, and the default takes the
    // first it hears. Looking at every channel takes a few seconds and finds the strongest.
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    // One failed attempt at the strongest would otherwise settle on a far weaker one for as
    // long as the connection lasts, and a slow link makes the whole portal slow.
    config.sta.failure_retry_cnt = kStationRetriesPerAccessPoint;
    return esp_wifi_set_config(WIFI_IF_STA, &config);
}

void finish_scan(ScanState state) {
    s_scan_state.store(state);
    s_scanning.store(false);
}

void collect_scan_results(uint16_t found) {
    static std::array<wifi_ap_record_t, kMaxScanEntries> s_records;
    uint16_t wanted = static_cast<uint16_t>(s_records.size());
    if (esp_wifi_scan_get_ap_records(&wanted, s_records.data()) != ESP_OK) {
        esp_wifi_clear_ap_list();
        ESP_LOGW(TAG, "scan results could not be read");
        finish_scan(ScanState::Failed);
        return;
    }
    for (size_t i = 0; i < wanted; ++i) {
        std::snprintf(s_scan_entries[i].ssid, sizeof(s_scan_entries[i].ssid), "%s",
                      reinterpret_cast<const char*>(s_records[i].ssid));
        s_scan_entries[i].rssi = s_records[i].rssi;
        s_scan_entries[i].open = s_records[i].authmode == WIFI_AUTH_OPEN;
    }
    s_scan_count = tidy_scan_entries(s_scan_entries.data(), wanted);
    ESP_LOGI(TAG, "scan found %u, listing %u, attempt %d", found, static_cast<unsigned>(s_scan_count), s_scan_attempts);
    finish_scan(ScanState::Done);
}

// One step of a search, once a second. The driver can refuse a scan or end it empty while
// the radio is busy, so a bad answer is tried again within kScanWindowUs rather than
// reported as no networks.
void run_scan_step() {
    if (s_scan_done.exchange(false)) {
        s_scan_in_driver.store(false);
        const bool status_ok = s_scan_status.load();
        uint16_t found = 0;
        esp_wifi_scan_get_ap_num(&found);
        const ScanStep step = scan_step_after_done(status_ok, found, s_scan_attempts);
        if (step == ScanStep::Accept) {
            collect_scan_results(found);
            return;
        }
        esp_wifi_clear_ap_list();
        ESP_LOGW(TAG, "scan ended with status %d and %u networks, attempt %d", status_ok, found, s_scan_attempts);
        if (step == ScanStep::Fail) {
            finish_scan(ScanState::Failed);
            return;
        }
    }
    // Before anything else: a scan whose end never comes must not hold everything off for good.
    if (esp_timer_get_time() - s_scan_started_us > kScanWindowUs) {
        ESP_LOGW(TAG, "scan gave up after %d attempts", s_scan_attempts);
        if (s_scan_in_driver.exchange(false)) {
            esp_wifi_scan_stop();
            esp_wifi_clear_ap_list();
        }
        finish_scan(ScanState::Failed);
        return;
    }
    if (s_scan_in_driver.load()) {
        return;
    }
    // A station still trying to connect refuses to scan, and stopping it can itself end a
    // scan that starts right away, so the scan waits for the next step. A station that has
    // nothing to stop must be left alone.
    if (s_has_credentials.load() && !s_connected.load() && !s_scan_quieted) {
        s_scan_quieted = true;
        esp_wifi_disconnect();
        return;
    }
    wifi_scan_config_t config = {};
    config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    config.scan_time.active.min = kScanChannelMinMs;
    config.scan_time.active.max = kScanChannelMaxMs;
    const esp_err_t err = esp_wifi_scan_start(&config, false);
    if (err == ESP_OK) {
        ++s_scan_attempts;
        s_scan_in_driver.store(true);
    } else {
        ESP_LOGW(TAG, "scan refused: %s", esp_err_to_name(err));
    }
}

// Runs the access point policy and retries the home network. The event handler
// only records what happened, so all mode changes stay in this one place.
void manager_task(void*) {
    uint32_t seconds_since_retry = 0;
    bool was_connected = false;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        // Running is stored last when a scan starts, after the fields it reads.
        if (s_scan_state.load() == ScanState::Running) {
            run_scan_step();
        }
        const bool connected = s_connected.load();
        if (connected && !was_connected) {
            time_sync_on_connected();
        }
        was_connected = connected;
        const bool has_credentials = s_has_credentials.load();
        const uint32_t seconds_in_state =
            static_cast<uint32_t>((esp_timer_get_time() - s_state_changed_us.load()) / 1000000);
        const bool want_ap = access_point_should_run(has_credentials, connected, seconds_in_state, s_access_point_on.load());
        if (want_ap != s_access_point_on.load() && !s_scanning.load()) {
            const esp_err_t err = esp_wifi_set_mode(want_ap ? WIFI_MODE_APSTA : WIFI_MODE_STA);
            if (err == ESP_OK) {
                s_access_point_on.store(want_ap);
                s_state->set_setup_network(want_ap);
                ESP_LOGI(TAG, "setup network %s", want_ap ? "on" : "off");
            } else {
                ESP_LOGW(TAG, "mode change failed: %s", esp_err_to_name(err));
            }
        }
        if (has_credentials && !connected && !s_scanning.load() && ++seconds_since_retry >= kRetryPeriodS) {
            seconds_since_retry = 0;
            esp_wifi_connect();
        }
    }
}

// Answers on the access point's address only, so it cannot affect the home network.
void dns_task(void*) {
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket failed");
        vTaskDelete(nullptr);
        return;
    }
    sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_port = htons(kDnsPort);
    std::memcpy(&local.sin_addr, kApAddress.data(), kApAddress.size());
    if (bind(sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        ESP_LOGE(TAG, "DNS bind failed");
        close(sock);
        vTaskDelete(nullptr);
        return;
    }

    uint8_t query[kDnsBufferBytes];
    uint8_t reply[kDnsBufferBytes];
    while (true) {
        sockaddr_in client = {};
        socklen_t client_length = sizeof(client);
        const int received = recvfrom(sock, query, sizeof(query), 0, reinterpret_cast<sockaddr*>(&client),
                                      &client_length);
        if (received <= 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        const size_t length =
            build_dns_reply(query, static_cast<size_t>(received), kApAddress, reply, sizeof(reply));
        if (length > 0) {
            sendto(sock, reply, length, 0, reinterpret_cast<sockaddr*>(&client), client_length);
        }
    }
}

esp_err_t start_mdns() {
    ESP_RETURN_ON_ERROR(mdns_init(), TAG, "mDNS init");
    ESP_RETURN_ON_ERROR(mdns_hostname_set(s_hostname), TAG, "mDNS hostname");
    ESP_RETURN_ON_ERROR(mdns_instance_name_set("csiGarland"), TAG, "mDNS name");
    ESP_RETURN_ON_ERROR(mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0), TAG, "mDNS service");
    // For a program to find this garland by: internal/api/discover.go asks for it, and then asks
    // /api/v1/about what answered.
    ESP_RETURN_ON_ERROR(mdns_service_add(s_hostname, "_csigarland", "_tcp", 443, nullptr, 0), TAG, "mDNS service");
    return ESP_OK;
}

}  // namespace

esp_err_t network_start(State& state) {
    s_state = &state;
    mark_state_change();
    const bool has_credentials = settings_load_wifi(s_credentials);
    s_has_credentials.store(has_credentials);
    char saved_name[sizeof(s_hostname)] = "";
    if (settings_load_hostname(saved_name, sizeof(saved_name)) && hostname_is_valid(saved_name)) {
        std::memcpy(s_hostname, saved_name, sizeof(s_hostname));
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    ESP_RETURN_ON_FALSE(esp_netif_create_default_wifi_ap() != nullptr, ESP_FAIL, TAG, "AP netif");
    esp_netif_t* station = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(station != nullptr, ESP_FAIL, TAG, "station netif");
    ESP_RETURN_ON_ERROR(esp_netif_set_hostname(station, s_hostname), TAG, "hostname");
    ESP_RETURN_ON_ERROR(time_sync_init(), TAG, "NTP");

    const wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "Wi-Fi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "Wi-Fi storage");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, nullptr, nullptr),
                        TAG, "Wi-Fi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, nullptr, nullptr),
                        TAG, "IP events");

    wifi_config_t ap = {};
    std::memcpy(ap.ap.ssid, kApSsid, sizeof(kApSsid) - 1);
    ap.ap.ssid_len = sizeof(kApSsid) - 1;
    std::memcpy(ap.ap.password, settings_ap_password(), kApPasswordLength);
    ap.ap.channel = kApChannel;
    ap.ap.max_connection = kApMaxClients;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;

    // The access point can only be configured while it is enabled.
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "Wi-Fi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), TAG, "AP config");
    if (has_credentials) {
        ESP_RETURN_ON_ERROR(apply_station_config(), TAG, "station config");
    }
    // The default of 40 MHz gains nothing at the setup network's short range, crowds a
    // shared band, and costs the station its 11ax mode.
    for (const wifi_interface_t interface : {WIFI_IF_AP, WIFI_IF_STA}) {
        const esp_err_t bandwidth = esp_wifi_set_bandwidth(interface, WIFI_BW20);
        if (bandwidth != ESP_OK) {
            ESP_LOGW(TAG, "bandwidth not set: %s", esp_err_to_name(bandwidth));
        }
    }
    // A board that knows its network does not start the access point just to stop it: the
    // manager task raises it if the network has not answered within the grace period.
    if (has_credentials) {
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Wi-Fi mode");
        s_access_point_on.store(false);
    }
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi start");
    // Mains powered, and with power save each request waits for the next beacon.
    const esp_err_t power_save = esp_wifi_set_ps(WIFI_PS_NONE);
    if (power_save != ESP_OK) {
        ESP_LOGW(TAG, "power save still on: %s", esp_err_to_name(power_save));
    }
    state.set_setup_network(!has_credentials);

    const esp_err_t mdns_err = start_mdns();
    if (mdns_err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS unavailable: %s", esp_err_to_name(mdns_err));
    }
    ESP_RETURN_ON_FALSE(xTaskCreate(manager_task, "net_manager", kManagerStackBytes, nullptr, kTaskPriority, nullptr) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "network task");
    // The setup network's login window only: the home network and the portal do without it.
    if (xTaskCreate(dns_task, "captive_dns", kDnsStackBytes, nullptr, kTaskPriority, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "no captive DNS task");
    }
    return ESP_OK;
}

NetworkStatus network_status() {
    return NetworkStatus{s_has_credentials.load(), s_connected.load(), s_access_point_on.load(), s_ip.load()};
}

const char* network_hostname() {
    return s_hostname;
}

// Saved for the restart that follows, which takes it everywhere at once. The name in use stays as it
// is until then: other requests read it meanwhile, and would catch it half written.
esp_err_t network_set_hostname(const char* name) {
    ESP_RETURN_ON_ERROR(settings_save_hostname(name), TAG, "save name");
    ESP_LOGI(TAG, "name %s is saved for the next start", name);
    return ESP_OK;
}

void network_ssid(char* out, size_t out_size) {
    std::snprintf(out, out_size, "%s", s_credentials.ssid);
}

esp_err_t network_set_credentials(const char* ssid, const char* password) {
    if (ssid[0] == '\0') {
        ESP_RETURN_ON_ERROR(settings_clear_wifi(), TAG, "clear");
        s_has_credentials.store(false);
        s_credentials = WifiCredentials{};
        esp_wifi_disconnect();
        mark_state_change();
        ESP_LOGI(TAG, "home network forgotten");
        return ESP_OK;
    }
    WifiCredentials updated = {};
    std::snprintf(updated.ssid, sizeof(updated.ssid), "%s", ssid);
    std::snprintf(updated.password, sizeof(updated.password), "%s",
                  wifi_keeps_saved_password(ssid, password, s_credentials.ssid) ? s_credentials.password : password);
    ESP_RETURN_ON_ERROR(settings_save_wifi(updated), TAG, "save");
    s_credentials = updated;
    s_has_credentials.store(true);
    esp_wifi_disconnect();
    ESP_RETURN_ON_ERROR(apply_station_config(), TAG, "station config");
    mark_state_change();
    return esp_wifi_connect();
}

void network_scan_start() {
    if (s_scanning.exchange(true)) {
        return;
    }
    s_scan_attempts = 0;
    s_scan_quieted = false;
    s_scan_done.store(false);
    s_scan_in_driver.store(false);
    s_scan_started_us = esp_timer_get_time();
    // Last: the manager task starts on this, and finds the fields above already set.
    s_scan_state.store(ScanState::Running);
    ESP_LOGI(TAG, "scan requested");
}

ScanState network_scan_results(ScanEntry* out, size_t& count) {
    const ScanState state = s_scan_state.load();
    if (state == ScanState::Done) {
        count = s_scan_count;
        std::copy_n(s_scan_entries.begin(), count, out);
    }
    return state;
}

}  // namespace garland
