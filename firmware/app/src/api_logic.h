/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Hardware-free part of the public API: what /api/v1/ says, and how it reads the log
 * Contributors: None
 *
 * See docs/stubs/PUBLIC-API.md. Under /api/v1/ a route and a field keep their meaning for good.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace garland {

constexpr int kApiVersion = 1;
constexpr char kApiPrefix[] = "/api/v1/";

constexpr bool api_is_public(const char* uri) {
    for (size_t i = 0; kApiPrefix[i] != '\0'; ++i) {
        if (uri[i] != kApiPrefix[i]) {
            return false;
        }
    }
    return true;
}

// For a program: stable, where the message beside it is for a person.
constexpr const char* api_error_code(int http_status) {
    switch (http_status) {
        case 400:
            return "bad_request";
        case 401:
            return "not_paired";
        case 404:
            return "not_found";
        case 405:
            return "method_not_allowed";
        case 408:
            return "timeout";
        case 409:
            return "conflict";
        case 413:
            return "too_large";
        case 429:
            return "try_later";
        case 500:
            return "internal";
        default:
            return "failed";
    }
}

// The same request path, with no query.
inline size_t api_path_length(const char* uri) {
    const char* const query = std::strchr(uri, '?');
    return query != nullptr ? static_cast<size_t>(query - uri) : std::strlen(uri);
}

// What to send of a log that has taken in `total` bytes so far and still holds the last `kept`, to
// a reader who has had up to `after`. Counts wrap as the garland runs for months.
struct LogWindow {
    size_t skip;      // Of what is kept, the bytes the reader has had.
    uint32_t lost;    // Bytes gone before the reader came back for them.
};

// The open /trial-log carries the kept log only while the firmware is on trial. With HTTPS down and
// the trial over, the status line is the whole answer: once the buffer has slid, its oldest line is
// some later line of a log a paired client started, and that route is open.
constexpr bool trial_log_sends_kept_log(bool on_trial) {
    return on_trial;
}

constexpr LogWindow log_window(uint32_t after, uint32_t total, size_t kept) {
    const uint32_t new_bytes = total - after;
    // Ahead of the log, as after a restart: this is a new log, so all of it.
    if (new_bytes > 0x80000000u) {
        return {0, 0};
    }
    if (new_bytes > kept) {
        return {0, new_bytes - static_cast<uint32_t>(kept)};
    }
    return {kept - new_bytes, 0};
}

}  // namespace garland
