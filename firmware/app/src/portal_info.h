/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: What the portal reports about the board, its addresses and the next action
 * Contributors: None
 */

#pragma once

#include <cJSON.h>

#include <cstdint>

#include "esp_http_server.h"
#include "garland_state.h"
#include "network_logic.h"

namespace garland {

void portal_format_ip(uint32_t ip, char (&out)[16]);
void portal_format_hostname(char (&out)[kMaxHostnameLength + 7]);

// What the schedule does next, in the fields of an action, for the pages to describe, as it comes out
// with the playlist as it stands: a start of an empty one is no start.
void portal_add_next_action(cJSON* root, const State& state);

esp_err_t portal_handle_info(httpd_req_t* req);
esp_err_t portal_handle_hostname_set(httpd_req_t* req);
esp_err_t portal_handle_certificate_domain_set(httpd_req_t* req);
esp_err_t portal_handle_root_certificate(httpd_req_t* req);

}  // namespace garland
