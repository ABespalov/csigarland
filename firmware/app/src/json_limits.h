/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Keeps JSON from outside from nesting, or from holding so many values, that parsing it overflows a task
 * Contributors: None
 */

#pragma once

#include <cJSON.h>

#include <cstdint>
#include <cstring>

namespace garland {

// cJSON recurses for each level, at 48 bytes a level on this chip, and allows 1000 of them. So a
// body of 192 opening brackets would take more stack than the plain server has. Nothing here
// goes past an effect's colour inside a gradient inside a layer, five levels down.
constexpr int kMaxJsonDepth = 8;

// One cJSON node per value. A schedule of 32 actions, or an effect of 4 layers, stays under this. A body of
// nothing but numbers, as {"entries":[1,1,1,...]} in the 6 KB a schedule may send, does not: a few thousand
// nodes would take more heap than the server has free.
constexpr int kMaxJsonValues = 1024;

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

// How many values text holds, keys apart, and whether that stays under kMaxJsonValues. One this cannot
// follow is left to cJSON, which refuses it: only a count already over the limit is refused here.
inline bool json_values_are_bounded(const char* text) {
    int depth = 0;
    int values = 0;
    bool in_string = false;
    bool escape = false;
    bool in_object[kMaxJsonDepth + 1] = {};
    enum class At : uint8_t { Value, Key, Colon, Done };
    At at = At::Value;
    const auto count = [&]() { return ++values <= kMaxJsonValues; };
    for (const char* c = text; *c != '\0'; ++c) {
        if (in_string) {
            if (escape) {
                escape = false;
            } else if (*c == '\\') {
                escape = true;
            } else if (*c == '"') {
                in_string = false;
            }
            continue;
        }
        const unsigned char ch = static_cast<unsigned char>(*c);
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            continue;
        }
        switch (at) {
            case At::Key:
                if (ch == '"') {
                    in_string = true;
                    at = At::Colon;
                    break;
                }
                if (ch == '}' && depth > 0) {
                    --depth;
                    at = At::Done;
                    break;
                }
                return values <= kMaxJsonValues;
            case At::Colon:
                if (ch == ':') {
                    at = At::Value;
                    break;
                }
                return values <= kMaxJsonValues;
            case At::Done:
                if (ch == ',') {
                    at = depth > 0 && in_object[depth] ? At::Key : At::Value;
                    break;
                }
                if ((ch == '}' || ch == ']') && depth > 0 && (ch == '}') == in_object[depth]) {
                    --depth;
                    break;
                }
                return values <= kMaxJsonValues;
            case At::Value:
                if (ch == ']') {
                    if (depth > 0 && !in_object[depth]) {
                        --depth;
                        at = At::Done;
                        break;
                    }
                    return values <= kMaxJsonValues;
                }
                if (ch == '{') {
                    if (!count() || ++depth > kMaxJsonDepth) {
                        return false;
                    }
                    in_object[depth] = true;
                    at = At::Key;
                    break;
                }
                if (ch == '[') {
                    if (!count() || ++depth > kMaxJsonDepth) {
                        return false;
                    }
                    in_object[depth] = false;
                    break;
                }
                if (ch == '"') {
                    if (!count()) {
                        return false;
                    }
                    in_string = true;
                    at = At::Done;
                    break;
                }
                if (ch == '-' || (ch >= '0' && ch <= '9')) {
                    if (!count()) {
                        return false;
                    }
                    while (c[1] != '\0' && (c[1] == '.' || c[1] == '+' || c[1] == '-' || c[1] == 'e' || c[1] == 'E' ||
                                            (c[1] >= '0' && c[1] <= '9'))) {
                        ++c;
                    }
                    at = At::Done;
                    break;
                }
                if (ch == 't' || ch == 'f' || ch == 'n') {
                    if (!count()) {
                        return false;
                    }
                    const char* const word = ch == 't' ? "true" : ch == 'f' ? "false" : "null";
                    const size_t length = std::strlen(word);
                    if (std::strncmp(c, word, length) != 0) {
                        return values <= kMaxJsonValues;
                    }
                    c += length - 1;
                    at = At::Done;
                    break;
                }
                return values <= kMaxJsonValues;
        }
    }
    return values <= kMaxJsonValues;
}

// As cJSON_Parse, but too deep or too many values counts as not valid, and so does anything after the
// value but whitespace: cJSON alone would take {"brightness":25}garbage for its first part.
inline cJSON* json_parse_shallow(const char* text) {
    return json_depth_is_safe(text) && json_values_are_bounded(text) ? cJSON_ParseWithOpts(text, nullptr, 1) : nullptr;
}

// Whether a body of length bytes is text all the way: one with a NUL in it would be judged, and
// kept, by what comes before.
inline bool body_is_text(const char* text, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (text[i] == '\0') {
            return false;
        }
    }
    return true;
}

// A request's body of length bytes, see body_is_text.
inline cJSON* json_parse_body(const char* text, size_t length) {
    return body_is_text(text, length) ? json_parse_shallow(text) : nullptr;
}

}  // namespace garland
