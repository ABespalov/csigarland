/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Hardware-free playlist logic: its JSON, the text it is saved as, and its effects by position
 * Contributors: None
 */

#pragma once

#include <cJSON.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "effect_catalog.h"
#include "effect_memory.h"
#include "effect_spec.h"
#include "garland_state.h"
#include "light_fade.h"

namespace garland {

// The effects by id, so that a change in the catalog cannot point the list at another effect.
struct PlaylistSettings {
    char effects[kMaxPlaylistEffects][kMaxEffectIdLength + 1] = {};
    size_t count = 0;
    uint16_t dwell_seconds = kPlaylistDwellDefaultSeconds;
    uint16_t cross_fade_ms = kCrossFadeDefaultMs;
};

// "60;1000;aurora,fire": what NVS holds. Room for the longest list.
constexpr size_t kPlaylistTextBytes = 16 + kMaxPlaylistEffects * (kMaxEffectIdLength + 1);

inline void playlist_to_text(const PlaylistSettings& playlist, char* out, size_t out_size) {
    int used = std::snprintf(out, out_size, "%u;%u;", static_cast<unsigned>(playlist.dwell_seconds),
                             static_cast<unsigned>(playlist.cross_fade_ms));
    for (size_t i = 0; i < playlist.count && used > 0 && static_cast<size_t>(used) < out_size; ++i) {
        used += std::snprintf(out + used, out_size - static_cast<size_t>(used), "%s%s", i == 0 ? "" : ",", playlist.effects[i]);
    }
}

// Whether a saved text names any effect: an empty list is saved as "60;1000;", which names none.
inline bool playlist_text_has_effects(const char* text) {
    const char* const first = std::strchr(text, ';');
    const char* const second = first != nullptr ? std::strchr(first + 1, ';') : nullptr;
    return second != nullptr && second[1] != '\0';
}

// The playlist as NVS keeps it: its text, a NUL, and, when it was left playing, the values of remembered
// it was left playing with. One blob, so the list and whether it plays are written together or not at all:
// a list emptied or replaced takes its marks with it in the same write. Up to two marks, remembered as it
// was and as it is about to be: the record is written first, so a power cut before remembered follows
// leaves a mark for the value still there. A mark counts only while remembered is still those bytes,
// which 1.0.0, knowing nothing of the playlist, may change, and only for a list that names an effect.
constexpr size_t kPlaylistMarks = 2;
constexpr size_t kPlaylistRecordBytes = kPlaylistTextBytes + kPlaylistMarks * sizeof(StoredSettings);

// Into out, of out_size, the record of text, marked as playing with each of marks, of mark_count, if the
// list names an effect. Its length, 0 when it does not fit.
inline size_t playlist_record_make(const char* text, const StoredSettings* marks, size_t mark_count, uint8_t* out, size_t out_size) {
    const size_t length = std::strlen(text) + 1;
    const size_t count = playlist_text_has_effects(text) && marks != nullptr ? (mark_count < kPlaylistMarks ? mark_count : kPlaylistMarks) : 0;
    if (length + count * sizeof(StoredSettings) > out_size) {
        return 0;
    }
    std::memcpy(out, text, length);
    if (count != 0) {
        std::memcpy(out + length, marks, count * sizeof(StoredSettings));
    }
    return length + count * sizeof(StoredSettings);
}

// The text of a record into text, of text_size, and in resumes whether it was left playing with remembered
// as it is now, which may be null for none. False for a record that is not one.
inline bool playlist_record_read(const uint8_t* record, size_t bytes, char* text, size_t text_size, const StoredSettings* remembered,
                                 bool& resumes) {
    const auto* const end = static_cast<const uint8_t*>(std::memchr(record, 0, bytes));
    if (end == nullptr) {
        return false;
    }
    const size_t length = static_cast<size_t>(end - record) + 1;
    const size_t rest = bytes - length;
    if (length > text_size || rest % sizeof(StoredSettings) != 0 || rest / sizeof(StoredSettings) > kPlaylistMarks) {
        return false;
    }
    std::memcpy(text, record, length);
    resumes = false;
    for (size_t at = length; remembered != nullptr && at < bytes; at += sizeof(StoredSettings)) {
        resumes = resumes || std::memcmp(record + at, remembered, sizeof(StoredSettings)) == 0;
    }
    resumes = resumes && playlist_text_has_effects(text);
    return true;
}
// Whether the list names id.
inline bool playlist_names(const PlaylistSettings& playlist, const char* id) {
    for (size_t i = 0; i < playlist.count; ++i) {
        if (std::strcmp(playlist.effects[i], id) == 0) {
            return true;
        }
    }
    return false;
}

// What a saved text says, keeping the defaults for what it does not. An effect this catalog lacks is
// kept by its id, as one of an effects pack updated since, or an upload deleted and coming back: it is
// played once it is there, see playlist_positions, and a change of the rest of the list keeps it.
inline PlaylistSettings playlist_from_text(const char* text) {
    PlaylistSettings playlist;
    char* end = nullptr;
    const long dwell = std::strtol(text, &end, 10);
    if (end == text || *end != ';') {
        return playlist;
    }
    const char* rest = end + 1;
    const long fade = std::strtol(rest, &end, 10);
    if (end == rest || *end != ';') {
        return playlist;
    }
    if (playlist_dwell_is_valid(static_cast<int>(dwell))) {
        playlist.dwell_seconds = static_cast<uint16_t>(dwell);
    }
    if (cross_fade_ms_is_valid(static_cast<int>(fade))) {
        playlist.cross_fade_ms = static_cast<uint16_t>(fade);
    }
    text = end + 1;
    while (*text != '\0' && playlist.count < kMaxPlaylistEffects) {
        const char* comma = std::strchr(text, ',');
        const size_t length = comma != nullptr ? static_cast<size_t>(comma - text) : std::strlen(text);
        if (length > 0 && length <= kMaxEffectIdLength) {
            char* id = playlist.effects[playlist.count];
            std::memcpy(id, text, length);
            id[length] = '\0';
            if (!playlist_names(playlist, id)) {
                ++playlist.count;
            }
        }
        text += length;
        if (*text == ',') {
            ++text;
        }
    }
    return playlist;
}

// Body: {"effects": ["aurora", ...], "dwell": 60, "cross_fade": 1000}. Effects must be in the catalog, or
// in saved, the list there now, which may name one this catalog lacks: an editor that changes the dwell
// sends it back, and it stays. Each once. All or nothing: false points error at what is wrong.
inline bool playlist_from_json(const cJSON* root, PlaylistSettings& out, const char*& error, const PlaylistSettings* saved = nullptr) {
    PlaylistSettings playlist;
    const cJSON* effects = cJSON_GetObjectItemCaseSensitive(root, "effects");
    const cJSON* dwell = cJSON_GetObjectItemCaseSensitive(root, "dwell");
    const cJSON* fade = cJSON_GetObjectItemCaseSensitive(root, "cross_fade");
    if (!cJSON_IsArray(effects) || cJSON_GetArraySize(effects) > static_cast<int>(kMaxPlaylistEffects)) {
        error = "effects must be a list of up to 16";
        return false;
    }
    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, effects) {
        if (!cJSON_IsString(item) || std::strlen(item->valuestring) > kMaxEffectIdLength ||
            (catalog_find(item->valuestring) < 0 && (saved == nullptr || !playlist_names(*saved, item->valuestring)))) {
            error = "an effect is not in the catalog or not ready";
            return false;
        }
        for (size_t i = 0; i < playlist.count; ++i) {
            if (std::strcmp(playlist.effects[i], item->valuestring) == 0) {
                error = "an effect is in the list twice";
                return false;
            }
        }
        std::snprintf(playlist.effects[playlist.count++], kMaxEffectIdLength + 1, "%s", item->valuestring);
    }
    if (dwell != nullptr) {
        if (!cJSON_IsNumber(dwell) || dwell->valuedouble != dwell->valueint || !playlist_dwell_is_valid(dwell->valueint)) {
            error = "dwell must be 10 to 3600 seconds";
            return false;
        }
        playlist.dwell_seconds = static_cast<uint16_t>(dwell->valueint);
    }
    if (fade != nullptr) {
        if (!cJSON_IsNumber(fade) || fade->valuedouble != fade->valueint || !cross_fade_ms_is_valid(fade->valueint)) {
            error = "cross_fade must be 0 to 5000 ms";
            return false;
        }
        playlist.cross_fade_ms = static_cast<uint16_t>(fade->valueint);
    }
    out = playlist;
    return true;
}

// "revision" of a body, if it holds one: a client that read the list gives the revision it read, and the
// list is replaced only if it is still that one. A client that means to replace whatever is there gives none.
inline bool playlist_revision_from_json(const cJSON* root, bool& given, uint32_t& revision, const char*& error) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, "revision");
    given = item != nullptr;
    if (!given) {
        return true;
    }
    if (!cJSON_IsNumber(item) || item->valuedouble < 0 || item->valuedouble > 4294967295.0 ||
        item->valuedouble != static_cast<double>(static_cast<uint32_t>(item->valuedouble))) {
        error = "revision must be the whole number the list was read with";
        return false;
    }
    revision = static_cast<uint32_t>(item->valuedouble);
    return true;
}

// {"effects", "missing", "dwell", "cross_fade", "playing", "max", "revision"}: missing, new after 1.0.0
// with the list kept by id, names those of effects this catalog lacks, which are not played until they
// are there. nullptr when out of memory.
inline cJSON* playlist_to_json(const PlaylistSettings& playlist, bool playing, uint32_t revision) {
    cJSON* root = cJSON_CreateObject();
    cJSON* effects = cJSON_CreateArray();
    cJSON* missing = cJSON_CreateArray();
    if (root == nullptr || effects == nullptr || missing == nullptr) {
        cJSON_Delete(root);
        cJSON_Delete(effects);
        cJSON_Delete(missing);
        return nullptr;
    }
    // Whole or not at all: a list short of one effect, for want of memory, would read as the list, and a
    // backup or an editor would keep it so.
    bool whole = cJSON_AddItemToObject(root, "effects", effects);
    if (!whole) {
        cJSON_Delete(effects);
    }
    const bool held = whole && cJSON_AddItemToObject(root, "missing", missing);
    if (!held) {
        cJSON_Delete(missing);
    }
    whole = held;
    for (size_t i = 0; whole && i < playlist.count; ++i) {
        cJSON* id = cJSON_CreateString(playlist.effects[i]);
        whole = id != nullptr && cJSON_AddItemToArray(effects, id);
        if (!whole) {
            cJSON_Delete(id);
        }
        if (whole && catalog_find(playlist.effects[i]) < 0) {
            cJSON* gone = cJSON_CreateString(playlist.effects[i]);
            whole = gone != nullptr && cJSON_AddItemToArray(missing, gone);
            if (!whole) {
                cJSON_Delete(gone);
            }
        }
    }
    whole = whole && cJSON_AddNumberToObject(root, "dwell", playlist.dwell_seconds) != nullptr &&
            cJSON_AddNumberToObject(root, "cross_fade", playlist.cross_fade_ms) != nullptr && cJSON_AddBoolToObject(root, "playing", playing) != nullptr &&
            cJSON_AddNumberToObject(root, "max", kMaxPlaylistEffects) != nullptr && cJSON_AddNumberToObject(root, "revision", revision) != nullptr;
    if (!whole) {
        cJSON_Delete(root);
        return nullptr;
    }
    return root;
}

// The list by catalog position, for the lighting task. Effects gone from the catalog are left out.
inline size_t playlist_positions(const PlaylistSettings& playlist, uint16_t* out) {
    size_t count = 0;
    for (size_t i = 0; i < playlist.count; ++i) {
        const int index = catalog_find(playlist.effects[i]);
        if (index >= 0) {
            out[count++] = static_cast<uint16_t>(index);
        }
    }
    return count;
}

// Hands the list to the state, as the lighting task reads it there.
inline void playlist_apply(State& state, const PlaylistSettings& playlist) {
    uint16_t positions[kMaxPlaylistEffects] = {};
    const size_t count = playlist_positions(playlist, positions);
    state.set_playlist(positions, count, playlist.dwell_seconds);
    state.set_cross_fade_ms(playlist.cross_fade_ms);
}

}  // namespace garland
