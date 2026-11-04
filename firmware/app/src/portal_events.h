/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Push updates: the garland's state sent to pages and programs as it changes
 * Contributors: None
 */

#pragma once

#include "esp_http_server.h"
#include "garland_state.h"

namespace garland {

// The task that sends the events. False if it could not start: the route then says so, and pages
// and programs go on asking now and then, as they did before it.
bool portal_events_start(State& state);

// GET /api/events and /api/v1/events: Server-Sent Events, `event: state` with what /api/state
// answers, at once and on every change, and a comment every 15 s. At most two at a time; a third,
// and any during an update, is refused, and a stream open when an update begins is ended.
esp_err_t portal_handle_events(httpd_req_t* req);

}  // namespace garland
