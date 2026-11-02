/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: NTP time, the time zone, and the task that runs scheduled actions
 * Contributors: None
 */

#pragma once

#include <cstddef>

#include "esp_err.h"
#include "garland_state.h"
#include "rtc_logic.h"
#include "schedule.h"

namespace garland {

// Loads the time zone. Needs NVS, and comes before anything formats a local time.
void time_load_zone();

// Needs esp_netif, so the network starts it. Then call on each home network connection.
esp_err_t time_sync_init();
void time_sync_on_connected();

// Looks for the optional clock module, and takes the time from it if it has kept it. Comes
// after the time zone and before the scheduler.
void time_load_from_rtc();

// Where the clock module's time last came from, since startup.
enum class ClockSource : uint8_t { None, Module, Ntp, Browser, Manual };

struct RtcStatus {
    bool present = false;
    bool trusted = false;
    char now[24] = "";  // Local time; empty when the module did not answer.
    ClockSource source = ClockSource::None;
};

RtcStatus time_rtc_status();

// Set the module and the garland's own clock. False when the time is out of range or the
// module did not take it. The next NTP answer overrules a time set here.
bool time_set_rtc(int64_t epoch, ClockSource source);
bool time_set_rtc_local(const LocalDateTime& local, ClockSource source);

// Asks NTP again now. The answer arrives in a few seconds, if the network has one.
void time_sync_now();

bool time_is_synced();

// False when the owner has not chosen a zone yet, which means times are UTC.
bool time_zone_is_set();
void time_zone_get(char* out, size_t out_size);
esp_err_t time_zone_set(const char* zone);

// "2026-10-31 18:05" in local time, or an empty string before the first sync.
void time_format_local(char* out, size_t out_size);

// "18:05:33" in local time. False, with nothing written, before the first sync.
bool time_format_clock(char* out, size_t out_size);

// False if the task could not be started: then nothing is scheduled or remembered.
bool start_scheduler(State& state);

size_t schedule_copy(ScheduleEntry* out);

// When the next action is due, as "Sat 31 Oct, 18:00" in local time, and what all that is due
// then adds up to. False with the clock not set, or nothing due within a week.
bool schedule_next(char* when, size_t when_size, ResolvedAction& action);

// Saves first, so a failed save leaves the running list as it was.
esp_err_t schedule_replace(const ScheduleEntry* entries, size_t count);
// Moves with every replacement, so that a page can say which list its change was made to.
uint32_t schedule_revision();

}  // namespace garland
