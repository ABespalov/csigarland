/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: What the portal's handlers share for answering in JSON
 * Contributors: None
 */

#pragma once

#include <cJSON.h>

#include <memory>

#include "esp_http_server.h"

namespace garland {

struct JsonDeleter {
    void operator()(cJSON* json) const { cJSON_Delete(json); }
};
using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

struct JsonTextDeleter {
    void operator()(char* text) const { cJSON_free(text); }
};

// Reads the body into a buffer of at least limit + 1 bytes, and ends it. On failure the
// error response is already sent.
bool portal_read_body(httpd_req_t* req, char* body, size_t limit);

// A request answered with its body, or the rest of it, left unread, as one refused: the server would read
// all of it before the next, for as long as the client sends it slowly, and it serves one request at a
// time. Marked here, the route that answered it ends its connection instead, see portal_end_of_request.
void portal_mark_body_unread(httpd_req_t* req);
// Every route's body is marked unread before its handler runs; a reader that took all of it says so here,
// and only then is the connection kept.
void portal_mark_body_read(httpd_req_t* req);

// What a route returns for req, whose answer result was: ESP_FAIL for one marked, which has the server
// close its connection and read no more of it.
esp_err_t portal_end_of_request(httpd_req_t* req, esp_err_t result);

// As portal_read_body, for a body read as text from then on: one holding a NUL anywhere is refused,
// as everything after it would go unread.
bool portal_read_text(httpd_req_t* req, char* body, size_t limit);

// What a handler fails with: plain text on the portal's own routes, and JSON on the public API.
esp_err_t portal_send_err(httpd_req_t* req, httpd_err_code_t error, const char* message);
// With a status line of its own, such as "409 Conflict".
esp_err_t portal_send_status(httpd_req_t* req, const char* status, const char* message);

// As portal_read_body, and parses it.
JsonPtr portal_read_json(httpd_req_t* req, char* body, size_t limit);

// The body of a short request as JSON. On failure the error response is already sent.
JsonPtr portal_read_json(httpd_req_t* req);

// The HTTPS server is up, rather than its certificates merely being in place.
bool portal_https_running();

inline esp_err_t send_json(httpd_req_t* req, const cJSON* json) {
    const std::unique_ptr<char, JsonTextDeleter> text(cJSON_PrintUnformatted(json));
    if (text == nullptr) {
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, text.get());
}

}  // namespace garland
