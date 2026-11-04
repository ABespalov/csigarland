/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The portal API of the playlist and of the schedule's next 24 hours
 * Contributors: None
 */

#pragma once

#include "esp_http_server.h"

namespace garland {

// These take the State as the request's user_ctx.
esp_err_t portal_handle_playlist_get(httpd_req_t* req);
esp_err_t portal_handle_playlist_set(httpd_req_t* req);
esp_err_t portal_handle_playlist_play(httpd_req_t* req);

esp_err_t portal_handle_schedule_timeline(httpd_req_t* req);

}  // namespace garland
