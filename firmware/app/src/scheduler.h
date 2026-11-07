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

#include "effect_memory.h"
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

// False when the owner has not chosen a zone yet, which means times are UTC, and a page may set the browser's.
// True while one saved could not be read yet, though times are UTC meanwhile: it may be the owner's.
bool time_zone_is_set();
void time_zone_get(char* out, size_t out_size);
esp_err_t time_zone_set(const char* zone);

// "2026-10-31 18:05" in local time, or an empty string before the first sync.
void time_format_local(char* out, size_t out_size);

// "18:05:33" in local time. False, with nothing written, before the first sync.
bool time_format_clock(char* out, size_t out_size);

// What a save of the remembered choice writes of the state, with the effect as given: the one the owner
// chose, which the playlist moving on or a preview leaves as it was.
inline Remembered remembered_from(const Snapshot& now, uint16_t effect) {
    return Remembered{effect, now.brightness_percent, now.speed_x4, now.power_chosen, now.playlist};
}

// False if the task could not be started: then nothing is scheduled or remembered.
// choice_epoch: when the owner's remembered choice was made, as main read it, kNoChoiceYet with none
// saved; 0 for one there that could not be read, which then outranks every step until the clock is known.
// saved: what flash holds of it, as put on the state before the buttons started, so that a choice of theirs
// before the task starts is still a change to save, at once if it turns the garland on or off.
// remembered_unread: what was remembered is there and could not be read. Nothing is saved over it, and it is
// read again every few seconds: read, it is put on the state as at a start, unless the owner chose since,
// which is newer, and saves go on from what flash holds either way. Put on, or found to be none, the steps
// after it are done as at a start, which the replay without it could not tell, those done meanwhile again on
// top of it, before anything is saved. Its time is not given a new one meanwhile.
bool start_scheduler(State& state, int64_t choice_epoch, const Remembered& saved, bool remembered_unread);

// The uptime clock in ms that the scheduler reads the moment of a choice by. On the state before the
// buttons start, so that no choice of theirs goes without its moment.
uint32_t choice_clock_ms();

// The remembered effect, levels and power on the state, as at a start. What this firmware no longer
// accepts keeps its default, such as an effect gone from a newer catalog. In one change, and only if the owner
// has chosen nothing since the start: false when they have, and nothing changed.
bool remembered_apply(State& state, const StoredSettings& remembered);

size_t schedule_copy(ScheduleEntry* out);

// When the next action is due, as "Sat 31 Oct, 18:00" in local time, and what all that is due
// then adds up to, for a garland with a playlist to start or with none, as playlist_ready says.
// False with the clock not set, or nothing due within a week.
// catalog is the state's catalog_revision: an effect uploaded since may be one a step names.
bool schedule_next(char* when, size_t when_size, ResolvedAction& action, bool playlist_ready, uint16_t catalog);

// For the Scheduler24 screensaver: the next step that does something within a day, its minute since the epoch
// and what it comes to, worked out once a minute. False with none, or while the clock, the schedule or the
// time zone is not known. catalog is the state's catalog_revision, as for schedule_next.
bool schedule_next_step(bool playlist_ready, uint16_t catalog, int64_t& minute, ResolvedAction& action);
// The step last done since the start that did something: when on the uptime clock in ms, and its name as it
// was then, see saver_action_name. False with none.
bool schedule_last_step(int64_t& at_ms, char* name, size_t name_size);

// The next 24 hours of the schedule, see schedule_timeline. -1 with the clock not set, as nothing can be
// said of when.
// now_epoch, if given, is the time the day was worked out from, read once, for the answer to name.
int schedule_timeline_now(TimelineEvent* out, size_t max_events, bool playlist_ready, int64_t* now_epoch = nullptr);

// Saves first, so a failed save leaves the running list as it was.
esp_err_t schedule_replace(const ScheduleEntry* entries, size_t count);
// Moves with every replacement, so that a page can say which list its change was made to.
// Whether the schedule has been read from NVS: until it has, it is no empty list, and the portal answers
// 503 for it rather than show or save one.
bool schedule_is_read();

// The garland restarts in a moment, as after an update installed: what is chosen now, as an Off of the
// POWER button's countdown during the update, is saved at once, and the scheduler is woken for it.
void scheduler_save_before_restart();
// Waits up to wait_ms for that save, and says whether what is chosen is saved. False when it failed, did
// not come in time, or this is the scheduler's own task, which would wait for itself.
bool scheduler_saved_before_restart(uint32_t wait_ms);
// The second up to which the scheduler has done its steps, a minute's end, or 0 before its first pass: a
// step due before it is done, whatever the state says of it.
int64_t schedule_steps_done_until();
uint32_t schedule_revision();

}  // namespace garland
