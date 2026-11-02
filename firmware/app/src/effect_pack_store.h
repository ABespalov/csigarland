/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Keeps the effects pack in two flash slots and switches between them safely
 * Contributors: None
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace garland {

struct PackView {
    const char* data = nullptr;
    size_t bytes = 0;
};

// Finds and maps the newest good pack, which stays mapped from then on. data is nullptr when
// there is none: the catalog then falls back on the one effect in the firmware.
PackView pack_store_open();

// The SHA-256 of the pack in use, in hex, or empty with none: what names its content.
const char* pack_store_active_digest();

// How large a pack a slot takes; 0 on a board without the slots.
size_t pack_store_capacity();

// A new pack goes to the slot not in use, and counts only from pack_store_commit() on, which
// the caller holds back until the rest of the update has passed its checks too. Each of these
// returns the problem as text, or nullptr.
const char* pack_store_begin(size_t bytes);
const char* pack_store_write(const uint8_t* data, size_t length);
// Reads back what reached the flash. With new firmware in the same update the pack is for that
// firmware to read, in a format this one may not know, so only must_be_readable looks inside.
const char* pack_store_check(const uint8_t* digest, bool must_be_readable);
const char* pack_store_commit();
// Undoes pack_store_commit() before any restart, for an update that failed after it.
const char* pack_store_revert();

}  // namespace garland
