/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: What the scheduler's task and the clock's code ask of each other, apart from scheduler.h
 * Contributors: None
 */

#pragma once

namespace garland {

// For the scheduler's task: whether an NTP answer came since it last asked, which it then takes on, as
// the network's task that hears it must not wait on the clock module's bus.
bool time_take_ntp_arrival();

// The clock module follows NTP when it has drifted or lost time. From the scheduler's task.
void time_pass_ntp_to_rtc();

// The next action found last no longer holds, as after the clock or the zone changed.
void schedule_forget_next_action();

}  // namespace garland
