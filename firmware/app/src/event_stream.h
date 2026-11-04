/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Hardware-free framing of the push updates, as Server-Sent Events
 * Contributors: None
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace garland {

// An event of its own, which a reader may pass over, as one it does not know: a page's script hears it,
// where a comment never reaches it, and can tell a stream fallen silent from one with nothing to say.
inline constexpr char kEventKeepAlive[] = "event: alive\ndata: {}\n\n";
constexpr uint32_t kEventKeepAliveMs = 15000;
// "event: state", one line of JSON as data, and the blank line that ends it.
constexpr size_t kEventFrameBytes = 448;

// One event in out, and its length, or 0 when it would not fit or data holds a line break, which
// would end its data early.
inline size_t event_frame(char* out, size_t size, const char* name, const char* data) {
    if (std::strchr(data, '\n') != nullptr || std::strchr(data, '\r') != nullptr) {
        return 0;
    }
    const int length = std::snprintf(out, size, "event: %s\ndata: %s\n\n", name, data);
    return length < 0 || static_cast<size_t>(length) >= size ? 0 : static_cast<size_t>(length);
}

}  // namespace garland
