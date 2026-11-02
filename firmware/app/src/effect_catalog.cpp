/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The effects built in and uploaded, and the favourites list format
 * Contributors: None
 */

#include "effect_catalog.h"

#include <array>
#include <atomic>
#include <cstring>

#include "effect_pack.h"

#if __has_include("freertos/FreeRTOS.h")
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace garland {
namespace {

constexpr size_t kPositions = kMaxCatalogEffects;
static_assert(kMaxPackEffects + kMaxUploadedEffects + 1 == kPositions, "every position has its use");

// All a board without a sound pack can play, so that it still lights and keeps its portal.
constexpr char kFallbackId[] = "warm-white";
constexpr char kFallbackGroup[] = "Basic";
constexpr char kFallbackName[] = "Warm white";
constexpr char kFallbackEffect[] =
    R"({"format":"csigarland.effect/1","name":"Warm white","layers":[{"fx":"solid","col":["FFB070"]}]})";

// Left to zeroed RAM: with an initial value, as an EffectSpec's defaults give it, an array
// would take its whole size in the firmware image as well. Each one is assigned before use.
template <size_t N>
union EffectBuffers {
    EffectBuffers() {}
    std::array<EffectSpec, N> items;
};

struct PlannedEntry {
    const char* id = nullptr;
    const char* name = nullptr;
    const char* group = nullptr;
};

// All of this points into the pack.
struct PackEntry {
    const char* id;
    const char* name;
    const char* group;
    const char* json;
};

std::array<PackEntry, kMaxPackEffects> s_entries;
std::array<PlannedEntry, kMaxPlannedEffects> s_planned;
size_t s_planned_count = 0;
// Each uploaded effect and the preview, and one spare to fill before switching to it.
EffectBuffers<kMaxUploadedEffects + 2> s_pool;
// An uploaded effect, or nullptr where the pack's stands or nothing does.
std::array<std::atomic<const EffectSpec*>, kPositions> s_positions;
std::array<std::atomic<uint32_t>, kPositions> s_revisions;
// Tasks now reading the pool. They only count themselves, so the lighting task never waits for
// a task of lower priority; the one who waits is whoever is about to use a buffer again.
std::atomic<uint32_t> s_readers{0};
EffectMask s_default_favorites;
size_t s_count = 0;

class PoolRead {
public:
    PoolRead() { s_readers.fetch_add(1); }
    ~PoolRead() { s_readers.fetch_sub(1); }
    PoolRead(const PoolRead&) = delete;
    PoolRead& operator=(const PoolRead&) = delete;
};

// A tick, as a reader may be of lower priority than the task waiting for it.
void let_readers_finish() {
#if __has_include("freertos/FreeRTOS.h")
    vTaskDelay(1);
#endif
}

size_t preview_position() {
    return s_count + kMaxUploadedEffects;
}

CatalogText text_of(const char* source) {
    CatalogText out;
    for (size_t i = 0; i + 1 < sizeof(out.text) && source[i] != '\0'; ++i) {
        out.text[i] = source[i];
    }
    return out;
}

// Only ever called from one task at a time: at startup, then by the web server.
EffectSpec& free_buffer() {
    for (EffectSpec& buffer : s_pool.items) {
        bool used = false;
        for (const std::atomic<const EffectSpec*>& position : s_positions) {
            used = used || position.load() == &buffer;
        }
        if (!used) {
            return buffer;
        }
    }
    // Cannot happen: at most kMaxUploadedEffects and the preview are in use.
    return s_pool.items[0];
}

void place(size_t position, const EffectSpec& spec) {
    EffectSpec& buffer = free_buffer();
    // No position leads to it now, but a reader may have come by before it was switched away.
    while (s_readers.load() != 0) {
        let_readers_finish();
    }
    buffer = spec;
    s_positions[position].store(&buffer);
    s_revisions[position].fetch_add(1);
}

void report(CatalogErrorHandler on_error, const char* id, const char* error) {
    if (on_error != nullptr) {
        on_error(id, error);
    }
}

void take_record(const PackRecord& record, CatalogErrorHandler on_error) {
    if (!uploaded_id_is_valid(record.id)) {
        report(on_error, record.id, "not a valid id");
    } else if (record.kind == kPackKindPlanned) {
        if (s_planned_count < s_planned.size()) {
            s_planned[s_planned_count++] = {record.id, record.name, record.group};
        }
    } else if (s_count >= s_entries.size()) {
        report(on_error, record.id, "the catalog is full");
    } else {
        // Static: at startup only, and too large for the stack it runs on.
        static EffectSpec s_checked;
        const char* error = nullptr;
        s_checked = EffectSpec{};
        if (parse_effect(record.text, record.id, s_checked, error)) {
            s_entries[s_count] = {record.id, record.name, record.group, record.text};
            s_default_favorites.set(s_count, record.kind == kPackKindFavorite);
            ++s_count;
        } else {
            report(on_error, record.id, error);
        }
    }
}

}  // namespace

size_t catalog_load(const char* pack, size_t pack_bytes, CatalogErrorHandler on_error) {
    s_count = 0;
    s_planned_count = 0;
    s_default_favorites = EffectMask{};
    for (std::atomic<const EffectSpec*>& position : s_positions) {
        position.store(nullptr);
    }
    for (std::atomic<uint32_t>& revision : s_revisions) {
        revision.store(0);
    }
    const char* const problem = pack_check(pack, pack_bytes);
    if (problem != nullptr) {
        report(on_error, "pack", problem);
    } else {
        PackReader reader(pack, pack_bytes);
        PackRecord record;
        while (reader.next(record)) {
            take_record(record, on_error);
        }
    }
    if (s_count == 0) {
        take_record({kPackKindFavorite, kFallbackId, kFallbackGroup, kFallbackName, kFallbackEffect}, on_error);
    }
    return s_count;
}

size_t catalog_size() {
    return s_count;
}

size_t catalog_positions() {
    return preview_position() + 1;
}

CatalogText catalog_id(size_t index) {
    const PoolRead reading;
    const EffectSpec* const uploaded = s_positions[index].load();
    return text_of(uploaded != nullptr ? uploaded->id : index < s_count ? s_entries[index].id : "");
}

CatalogText catalog_name(size_t index) {
    const PoolRead reading;
    const EffectSpec* const uploaded = s_positions[index].load();
    return text_of(uploaded != nullptr ? uploaded->name : index < s_count ? s_entries[index].name : "");
}

uint32_t catalog_revision(size_t index) {
    return s_revisions[index].load();
}

bool catalog_copy_spec(size_t index, EffectSpec& out) {
    {
        const PoolRead reading;
        const EffectSpec* const uploaded = s_positions[index].load();
        if (uploaded != nullptr) {
            out = *uploaded;
            return true;
        }
    }
    // Aside, as a parse that runs out of memory must leave what the caller has. Static, as it is too
    // large for the Lighting task's stack, and that task is the only caller.
    static EffectSpec s_parsed;
    s_parsed = EffectSpec{};
    const char* error = nullptr;
    if (index >= s_count || !parse_effect(s_entries[index].json, s_entries[index].id, s_parsed, error)) {
        return false;
    }
    out = s_parsed;
    return true;
}

const char* catalog_built_in_file(size_t index) {
    return index < s_count ? s_entries[index].json : nullptr;
}

int catalog_find(const char* id) {
    if (id[0] == '\0') {
        return -1;
    }
    for (size_t i = 0; i < preview_position(); ++i) {
        if (std::strcmp(catalog_id(i).c_str(), id) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool uploaded_id_is_valid(const char* id) {
    const size_t length = std::strlen(id);
    if (length == 0 || length > kMaxEffectIdLength || std::strcmp(id, kPreviewEffectId) == 0) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        const char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return false;
        }
    }
    return true;
}

size_t catalog_uploaded_count() {
    size_t count = 0;
    for (size_t i = 0; i < preview_position(); ++i) {
        count += catalog_is_uploaded(i) ? 1 : 0;
    }
    return count;
}

bool catalog_can_install(const char* id) {
    const int position = catalog_find(id);
    if (position >= 0 && catalog_is_uploaded(static_cast<size_t>(position))) {
        return true;
    }
    return catalog_uploaded_count() < kMaxUploadedEffects;
}

int catalog_install(const EffectSpec& spec) {
    if (!catalog_can_install(spec.id)) {
        return -1;
    }
    int position = catalog_find(spec.id);
    for (size_t i = s_count; position < 0 && i < preview_position(); ++i) {
        if (catalog_id(i).empty()) {
            position = static_cast<int>(i);
        }
    }
    if (position < 0) {
        return -1;
    }
    place(static_cast<size_t>(position), spec);
    return position;
}

void catalog_remove(size_t position) {
    s_positions[position].store(nullptr);
    s_revisions[position].fetch_add(1);
}

size_t catalog_preview(const EffectSpec& spec) {
    place(preview_position(), spec);
    return preview_position();
}

bool catalog_is_preview(size_t position) {
    return position == preview_position();
}

bool catalog_is_uploaded(size_t position) {
    return !catalog_is_preview(position) && s_positions[position].load() != nullptr;
}

bool catalog_is_built_in(size_t position) {
    return position < s_count;
}

size_t catalog_planned_size() {
    return s_planned_count;
}

const char* catalog_planned_id(size_t index) {
    return s_planned[index].id;
}

const char* catalog_planned_name(size_t index) {
    return s_planned[index].name;
}

const char* catalog_planned_group(size_t index) {
    return s_planned[index].group;
}

// Whatever the owner uploaded, a replacement for a built-in effect too, which goes back when restored.
const char* catalog_group(size_t index) {
    return index < s_count && s_positions[index].load() == nullptr ? s_entries[index].group : "Uploaded";
}

EffectMask catalog_default_favorites() {
    // Should every marked effect fail to load, the button still needs something to cycle.
    return s_default_favorites.any() ? s_default_favorites : EffectMask::all(s_count);
}

EffectMask favorites_from_text(const char* text) {
    EffectMask mask;
    char id[kMaxEffectIdLength + 1];
    while (*text != '\0') {
        const char* comma = std::strchr(text, ',');
        const size_t length = comma != nullptr ? static_cast<size_t>(comma - text) : std::strlen(text);
        if (length > 0 && length < sizeof(id)) {
            std::memcpy(id, text, length);
            id[length] = '\0';
            const int index = catalog_find(id);
            if (index >= 0) {
                mask.set(static_cast<size_t>(index), true);
            }
        }
        text += length;
        if (*text == ',') {
            ++text;
        }
    }
    return mask;
}

void favorites_to_text(const EffectMask& mask, char* out, size_t out_size) {
    size_t used = 0;
    if (out_size > 0) {
        out[0] = '\0';
    }
    for (size_t i = 0; i < preview_position(); ++i) {
        if (!mask.test(i)) {
            continue;
        }
        const CatalogText id = catalog_id(i);
        const size_t length = std::strlen(id.c_str());
        const size_t separator = used > 0 ? 1 : 0;
        if (used + separator + length + 1 > out_size) {
            return;
        }
        if (separator != 0) {
            out[used++] = ',';
        }
        std::memcpy(out + used, id.c_str(), length + 1);
        used += length;
    }
}

}  // namespace garland
