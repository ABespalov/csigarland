/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Watches the portal's sessions, and shuts down one whose request does not come in time
 * Contributors: None
 */

#pragma once

#include <cstdint>

#include "esp_err.h"
#include "esp_http_server.h"

namespace garland {

// A request's headers have this long, from the session's opening or the end of the request before it.
// ESP-IDF reads them before any route, with a timeout for each read alone, so headers sent a byte every
// few seconds would hold the server's one task for as long as they kept coming.
constexpr int64_t kHeaderDeadlineUs = 10 * 1000 * 1000;

// Starts the task that looks at the sessions. False if it could not: sessions then have the server's own
// timeouts alone, and everything else works as ever.
bool portal_watch_start();

// The servers' open_fn and close_fn. On HTTPS a session opens once its handshake, which has its own limit,
// is done. close_fn closes the socket, as the server leaves that to it once one is set.
esp_err_t portal_watch_open(httpd_handle_t server, int sockfd);
void portal_watch_close(httpd_handle_t server, int sockfd);

// The moment, in esp_timer's microseconds, by which what req's session waits for must have come, 0 for
// none. Past it the connection is shut down, which ends a read blocked on it, a TLS record's among them.
void portal_watch_deadline(httpd_req_t* req, int64_t deadline_us);

// A route has the request: no deadline but its readers' own, until it ends, when the next request's
// headers are timed.
void portal_watch_route_begins(httpd_req_t* req);
void portal_watch_route_ends(httpd_req_t* req);

// The server whose sessions are watched in their handshake too, before its open_fn: ESP-IDF does that
// handshake first, in the server's one task, and a client sending it a byte now and then holds that task.
// Such a session is found in the server's own list of clients, and shut down past kHandshakeDeadlineUs.
// Five seconds: a handshake is a few records, and fifteen let a client drip one byte at a time while the
// server's one task did nothing else. No phone on a slow link was found to need the longer one.
constexpr int64_t kHandshakeDeadlineUs = 5 * 1000 * 1000;
void portal_watch_handshakes(httpd_handle_t server);

// The session goes to another task for good, as a stream does: no deadline of the portal's applies to it.
void portal_watch_hand_off(httpd_req_t* req);

}  // namespace garland
