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
#include "network_logic.h"

namespace garland {

void portal_format_ip(uint32_t ip, char (&out)[16]);
void portal_format_hostname(char (&out)[kMaxHostnameLength + 7]);

// What the schedule does next, in the fields of an action, for the pages to describe.
void portal_add_next_action(cJSON* root);

esp_err_t portal_handle_info(httpd_req_t* req);
esp_err_t portal_handle_hostname_set(httpd_req_t* req);
esp_err_t portal_handle_certificate_domain_set(httpd_req_t* req);
esp_err_t portal_handle_root_certificate(httpd_req_t* req);

}  // namespace garland
