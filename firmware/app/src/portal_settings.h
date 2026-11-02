/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The portal API of the settings: Wi-Fi, the schedule, the time and the screen
 * Contributors: None
 */

#pragma once

#include "esp_http_server.h"

namespace garland {

esp_err_t portal_handle_wifi_status(httpd_req_t* req);
esp_err_t portal_handle_wifi_set(httpd_req_t* req);
esp_err_t portal_handle_wifi_scan(httpd_req_t* req);
esp_err_t portal_handle_schedule_get(httpd_req_t* req);
esp_err_t portal_handle_schedule_set(httpd_req_t* req);
esp_err_t portal_handle_time_get(httpd_req_t* req);
esp_err_t portal_handle_time_set(httpd_req_t* req);
esp_err_t portal_handle_rtc_set(httpd_req_t* req);

// These two take the State as the request's user_ctx.
esp_err_t portal_handle_display_get(httpd_req_t* req);
esp_err_t portal_handle_display_set(httpd_req_t* req);

}  // namespace garland
