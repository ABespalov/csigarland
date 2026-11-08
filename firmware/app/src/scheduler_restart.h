/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Between the scheduler's task and a restart: what the task saved of the owner's choice ahead of it
 * Contributors: None
 */

#pragma once

#include <cstdint>

#include "effect_memory.h"
#include "garland_state.h"

namespace garland {

// The state a restart holds what was saved to, before the scheduler's task starts.
void restart_save_bind(State& state);
// Whether a restart has asked for a save, which the scheduler's pass then answers.
bool restart_save_asked();
// The pass's answer: saved, when what is chosen now, held, is what flash holds, as of the moment of the owner's
// last choice the pass took before saving, choice_seen.
void restart_save_answer(bool saved, const Remembered& held, uint64_t choice_seen);

// Of the scheduler's task, for the side that waits: wakes it to save, and says whether it was started, and whether
// the caller is that task.
void scheduler_wake_to_save();
bool scheduler_task_started();
bool scheduler_task_is_current();

}  // namespace garland
