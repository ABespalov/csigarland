/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The portal API of the garland itself: its state, its LEDs and its effects
 * Contributors: None
 */

#pragma once

#include <cstddef>

#include "esp_http_server.h"
#include "garland_state.h"

namespace garland {

// The longest answer of /api/state, which the event stream sends too.
constexpr size_t kStateJsonBytes = 448;

// The state as /api/state answers it, in out: its length, or -1 if it did not fit.
int portal_state_json(const State& state, char* out, size_t size);

// Each takes the State as the request's user_ctx.
esp_err_t portal_handle_state(httpd_req_t* req);
esp_err_t portal_handle_mode_set(httpd_req_t* req);
esp_err_t portal_handle_set_leds(httpd_req_t* req);
esp_err_t portal_handle_power(httpd_req_t* req);
esp_err_t portal_handle_effect_read(httpd_req_t* req);
esp_err_t portal_handle_effects(httpd_req_t* req);
esp_err_t portal_handle_planned(httpd_req_t* req);
esp_err_t portal_handle_activate(httpd_req_t* req);
esp_err_t portal_handle_favorite(httpd_req_t* req);
esp_err_t portal_handle_effect_upload(httpd_req_t* req);
esp_err_t portal_handle_effect_preview(httpd_req_t* req);
esp_err_t portal_handle_effect_delete(httpd_req_t* req);
esp_err_t portal_handle_effect_store(httpd_req_t* req);

}  // namespace garland
