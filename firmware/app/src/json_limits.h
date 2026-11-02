/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Keeps JSON from outside from nesting deep enough to overflow a task's stack
 * Contributors: None
 */

#pragma once

#include <cJSON.h>

namespace garland {

// cJSON recurses for each level, at 48 bytes a level on this chip, and allows 1000 of them. So a
// body of 192 opening brackets would take more stack than the plain server has. Nothing here
// goes past an effect's colour inside a gradient inside a layer, five levels down.
constexpr int kMaxJsonDepth = 8;

inline bool json_depth_is_safe(const char* text) {
    int depth = 0;
    bool in_string = false;
    for (const char* c = text; *c != '\0'; ++c) {
        if (in_string) {
            if (*c == '\\' && c[1] != '\0') {
                ++c;
            } else if (*c == '"') {
                in_string = false;
            }
        } else if (*c == '"') {
            in_string = true;
        } else if (*c == '[' || *c == '{') {
            if (++depth > kMaxJsonDepth) {
                return false;
            }
        } else if (*c == ']' || *c == '}') {
            --depth;
        }
    }
    return true;
}

// As cJSON_Parse, but too deep counts as not valid, and so does anything after the value but
// whitespace: cJSON alone would take {"brightness":25}garbage for its first part.
inline cJSON* json_parse_shallow(const char* text) {
    return json_depth_is_safe(text) ? cJSON_ParseWithOpts(text, nullptr, 1) : nullptr;
}

// A request's body of length bytes. One with a NUL in it would be judged by what comes before.
inline cJSON* json_parse_body(const char* text, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (text[i] == '\0') {
            return nullptr;
        }
    }
    return json_parse_shallow(text);
}

}  // namespace garland
