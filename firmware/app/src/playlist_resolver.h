/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Keeps the state's playlist resolved from the list saved in NVS, also after a failed read
 * Contributors: None
 */

#pragma once

#include <cstdint>

#include "garland_state.h"
#include "playlist_logic.h"

namespace garland {

// The list lives in NVS by id, the state holds it by catalog position. Resolves it again from NVS, as at a
// start or after the catalog changed, trying three times. A reading that still fails leaves the list
// stale: playlist_retry_if_stale tries again later, and a list read meanwhile is applied first.
void playlist_resolve(State& state);

// For the scheduler's task, every look round: tries again, at most every few seconds, while stale.
void playlist_retry_if_stale(State& state, uint32_t now_ms);

// A handler that has just read the saved list whole: applied to the state first if it was stale, so that
// what it answers or plays is what the state holds.
void playlist_apply_if_stale(State& state, const PlaylistSettings& read);

// A list just saved, applied to the state as it was saved: no older reading can come after it.
void playlist_apply_saved(State& state, const PlaylistSettings& saved);

bool playlist_is_stale();

// At a start, for a list left playing: resumed now if the list was read, else once it is, unless the owner
// has chosen something by then, which outranks what was left.
void playlist_resume_when_read(State& state);

// At a start whose reading of whether the list was left playing failed: that is no "not playing", and it
// is read again until it is known, and then played on as playlist_resume_when_read does.
void playlist_playing_unknown();

// Whether the mark of playing the playlist waits on something: it could not be read yet, or it was read as
// playing and waits for the list to resume it. Until it no longer does, a save that did not choose what
// plays keeps the mark as saved, and the resume decides it.
bool playlist_mark_is_pending();

// Whether the list was left playing, read with what was remembered: playing, it resumes once the list is
// read; a reading that fails is no "not playing", and is tried again, as the list is.
void playlist_restore_playing(State& state);

}  // namespace garland
