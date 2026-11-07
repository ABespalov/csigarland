/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The effects built in and uploaded, and the favourites list format
 * Contributors: None
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "effect_spec.h"
#include "garland_state.h"

namespace garland {

// Favourites travel as a bit per catalog position, so the catalog stays within it.
// All ids with their commas. A test checks that today's catalog fits; NVS takes up to 4000.
constexpr size_t kFavoritesTextBytes = 4000;

// Effects the owner has uploaded, new or replacing a built-in one, and the size of one file.
constexpr size_t kMaxUploadedEffects = 32;
constexpr size_t kMaxEffectFileBytes = 4096;
// The effects a pack may hold. The slots in partitions.csv have room for 512 of the size they are
// now, and the state's index and the favourites mask for what that takes with uploads and the
// preview; raising it to that needs a test of the memory it takes, not another flash by wire.
constexpr size_t kMaxPackEffects = 256;
// Every effect of the pack, then the uploads, then the preview.
constexpr size_t kMaxCatalogEffects = kMaxPackEffects + kMaxUploadedEffects + 1;
static_assert(kMaxCatalogEffects <= EffectMask::kBits, "every position has its bit");
constexpr size_t kMaxPlannedEffects = 32;
// What a preview plays under. Never a saved id, so it cannot be favoured or scheduled.
inline constexpr char kPreviewEffectId[] = "preview";

using CatalogErrorHandler = void (*)(const char* id, const char* error);

// Checks every effect of the pack once, keeping only pointers into it: the catalog can hold
// 159, and one takes 452 bytes parsed, so it is parsed again when it comes to play. The pack
// has to stay readable from then on. An effect this firmware cannot play is reported and
// left out, and with none at all, or no sound pack, the one effect in the firmware plays.
size_t catalog_load(const char* pack, size_t pack_bytes, CatalogErrorHandler on_error);

// The built-in effects, which come first: those of the pack.
size_t catalog_size();
// Every position an effect can be at. An empty one has an empty id, and a position never
// moves: favourites and the active effect stay right while effects come and go.
size_t catalog_positions();
// The caller's own copy of an id or a name: the memory of a replaced effect is used again, so
// nothing of the catalog's may be held on to. Passed straight to a call, it lasts that call.
struct CatalogText {
    char text[kMaxEffectIdLength + 1] = {};

    const char* c_str() const { return text; }
    bool empty() const { return text[0] == '\0'; }
};
static_assert(kMaxEffectIdLength == kMaxEffectNameLength, "one size holds both");

// Safe from any task, and none of these reads ever waits: an upload fills a spare copy, switches
// to it, and uses the old one again only when no reader can still be at it.
CatalogText catalog_id(size_t index);
CatalogText catalog_name(size_t index);
// Moves on every time the effect at this position is replaced or removed. Read it before the
// copy: a change in between then only brings one copy more.
uint32_t catalog_revision(size_t index);
// Parses the effect, or copies an uploaded one. False, leaving out as it was, for an empty position
// or a parse that ran out of memory.
bool catalog_copy_spec(size_t index, EffectSpec& out);
// A built-in effect's file, minified, in the mapped pack, or nullptr. Under an upload it still is the
// built-in one: the upload's own is in the store.
const char* catalog_built_in_file(size_t index);
int catalog_find(const char* id);

bool uploaded_id_is_valid(const char* id);
size_t catalog_uploaded_count();
bool catalog_can_install(const char* id);
// A new effect takes a free position, and one with a known id replaces it. Returns the
// position, or -1 with no room.
int catalog_install(const EffectSpec& spec);
void catalog_remove(size_t position);
size_t catalog_preview(const EffectSpec& spec);
bool catalog_is_preview(size_t position);
// Uploaded: lives in the store, and may be deleted. Built-in: comes back when it is.
bool catalog_is_uploaded(size_t position);
bool catalog_is_built_in(size_t position);

// Effects marked todo in catalog.json: known by name, not playable yet.
size_t catalog_planned_size();
const char* catalog_planned_id(size_t index);
const char* catalog_planned_name(size_t index);
const char* catalog_planned_group(size_t index);

// The group's display name, for both ready and planned effects.
const char* catalog_group(size_t index);

// The favourites marked in the catalog, for a board where the owner has chosen none yet.
EffectMask catalog_default_favorites();


// Saved by id, not position, so adding an effect to the catalog keeps the owner's choice.
EffectMask favorites_from_text(const char* text);
// Whether the text names an effect the catalog lacks, which favorites_from_text leaves out: one gone from a
// newer catalog, or an uploaded effect not read yet.
bool favorites_name_unknown(const char* text);
// False, with out empty, when the whole list does not fit: NVS keeps a string of 4000 bytes at most,
// about 120 of the longest ids, and a list kept in part would lose the rest unsaid.
[[nodiscard]] bool favorites_to_text(const EffectMask& mask, char* out, size_t out_size);

}  // namespace garland
