/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Saving the owner's choice before a restart: asked for, answered by the scheduler's task, and waited for
 * Contributors: None
 */

#include "scheduler_restart.h"

#include <atomic>

#include "effect_catalog.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "scheduler.h"

namespace garland {
namespace {

// A restart asked for, and what came of saving the settings ahead of it, see scheduler_save_before_restart.
enum class RestartSave : uint8_t { None, Asked, Held, Failed };
std::atomic<RestartSave> s_restart_save{RestartSave::None};
// What was saved, as the scheduler last answered Held: a restart holds it to what is chosen at that moment.
std::atomic<uint64_t> s_restart_held{0};
// And the moment of the owner's last choice it was saved with: a choice since, the same value again among
// them, is newer than what was saved.
std::atomic<uint64_t> s_restart_held_choice{0};
State* s_restart_state = nullptr;
constexpr uint32_t kRestartSaveStepMs = 50;

uint64_t pack_remembered(const Remembered& r) {
    return uint64_t{r.effect} | uint64_t{r.brightness_percent} << 16 | uint64_t{r.speed_x4} << 24 | uint64_t{r.playing} << 32 |
           uint64_t{r.playlist} << 33;
}

// Whether what is chosen now is what the scheduler last said it saved: an Off of the countdown after that
// answer is not in it. The effect as the scheduler remembers it, which a preview or the playlist moving on
// leaves as it was.
bool restart_held_matches_now() {
    const Snapshot now = s_restart_state->snapshot();
    const uint64_t held = s_restart_held.load();
    const uint16_t effect = !catalog_is_preview(now.effect) && !now.playlist ? now.effect : static_cast<uint16_t>(held & 0xFFFF);
    return pack_remembered(remembered_from(now, effect)) == held && s_restart_state->owner_choice_ms() == s_restart_held_choice.load();
}

}  // namespace

void restart_save_bind(State& state) {
    s_restart_state = &state;
}

bool restart_save_asked() {
    return s_restart_save.load() != RestartSave::None;
}

void restart_save_answer(bool saved, const Remembered& held, uint64_t choice_seen) {
    if (!saved) {
        s_restart_save.store(RestartSave::Failed);
        return;
    }
    // The value first: a restart that sees Held reads the value it is of, or a newer one.
    s_restart_held.store(pack_remembered(held));
    s_restart_held_choice.store(choice_seen);
    s_restart_save.store(RestartSave::Held);
}

void scheduler_save_before_restart() {
    RestartSave none = RestartSave::None;
    s_restart_save.compare_exchange_strong(none, RestartSave::Asked);
    scheduler_wake_to_save();
}

bool scheduler_saved_before_restart(uint32_t wait_ms) {
    if (!scheduler_task_started()) {
        // No scheduler, so nothing is remembered at all, and nothing is lost by the restart.
        return true;
    }
    // Its own task would wait for itself, which saved what it had a moment ago.
    if (scheduler_task_is_current()) {
        return false;
    }
    for (uint32_t waited = 0;; waited += kRestartSaveStepMs) {
        // Held of an earlier pass says nothing of a change since, so it counts only if it is what is chosen.
        if (s_restart_save.load() == RestartSave::Held && restart_held_matches_now()) {
            return true;
        }
        if (waited >= wait_ms) {
            return false;
        }
        scheduler_wake_to_save();
        vTaskDelay(pdMS_TO_TICKS(kRestartSaveStepMs));
    }
}

}  // namespace garland
