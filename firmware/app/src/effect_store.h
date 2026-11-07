/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Keeps uploaded effects in their own flash partition
 * Contributors: None
 */

#pragma once

#include <cstddef>

#include "effect_catalog.h"

namespace garland {

// Opens the partition and puts what it holds in the catalog. A board that took this firmware
// over the air still has the old layout, with no such partition: previews work, saving does not.
void effect_store_start();

// Where an upload's file goes: kMaxEffectFileBytes and its terminator. One request at a time.
char* effect_store_body();

// Reads again the effects saved before whose reading failed at the start, putting each read in the catalog,
// through effect_store_body(): before a request's body goes there. True when any was read, and the catalog
// may have changed.
bool effect_store_read_unread();
// While any is unread, one of them may hold an id not known, and come back after an upload under it is
// deleted, as a second copy: such an upload is refused.
bool effect_store_unread_may_hold(const char* id);
// None is left unread, nor the list of them: an id the catalog lacks is then no uploaded effect.
bool effect_store_all_read();

// These read the file from effect_store_body() and return the problem as text, or nullptr.
const char* effect_store_save(const char* id);
const char* effect_store_preview();

// An uploaded effect's file, in effect_store_body() until the next request, or nullptr.
const char* effect_store_read(const char* id);

// A built-in effect goes back to its own definition, any other leaves the catalog.
const char* effect_store_delete(const char* id);

struct EffectStoreStatus {
    bool available = false;
    size_t entries_used = 0;
    size_t entries_total = 0;
};
EffectStoreStatus effect_store_status();

}  // namespace garland
