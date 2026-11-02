/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Home Wi-Fi, the setup access point, captive DNS and mDNS
 * Contributors: None
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "garland_state.h"
#include "network_logic.h"

namespace garland {

struct NetworkStatus {
    bool has_credentials;
    bool connected;
    bool access_point_on;
    uint32_t ip;  // Network byte order, 0 when not connected.
};

enum class ScanState : uint8_t { Idle, Running, Done, Failed };

esp_err_t network_start(State& state);

NetworkStatus network_status();

// The saved home network's name; the password never leaves network.cpp and settings.
void network_ssid(char* out, size_t out_size);

// Saves the home network and connects to it. An empty ssid forgets it.
esp_err_t network_set_credentials(const char* ssid, const char* password);

// The name the garland answers to, without .local.
const char* network_hostname();

// Saves a name hostname_is_valid accepts, for the restart that follows: the name in use stays until it.
esp_err_t network_set_hostname(const char* name);

// Starts a search for networks in the background, so the access point and the portal
// stay responsive. Does nothing if one is running. The results stay until the next search.
void network_scan_start();

// out holds kMaxScanEntries. count is set only when the state is Done.
ScanState network_scan_results(ScanEntry* out, size_t& count);

}  // namespace garland
