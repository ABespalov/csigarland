/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Installs signed firmware updates and keeps a bad one from sticking
 * Contributors: None
 */

#pragma once

#include "esp_http_server.h"
#include "garland_state.h"

namespace garland {

// What an update file held, and so what it replaced.
struct OtaInstalled {
    bool firmware = false;
    bool effects = false;
};

// Installs what the update file in the request's body holds: firmware to the other slot, to
// boot next, and effects to the other pack slot. The portal has no login, so the signature
// is what decides: anyone on the network can send a file, but only one signed with the
// owner's key can run. Returns what went wrong, as text for the owner, or nullptr. What is
// running is untouched either way, and a failed slot is simply overwritten by the next try.
const char* ota_receive(httpd_req_t* req, State& state, OtaInstalled& installed);

// Restarts into the installed update, after the time a reply needs to get out.
void ota_restart_soon();
// A restart taken before anything is done for it, unless an update is being received or a restart is
// under way already: false then, and nothing is taken. One not needed after all is given back with
// ota_release_restart, and ota_restart_soon goes on with one taken.
bool ota_reserve_restart();
void ota_release_restart();

// What tells one image and one start from another, for a client to know that what it sent is what
// runs: a version may repeat. The image's is the ELF hash its descriptor holds, in hex, which a
// client can read from the update file. The boot id is random, and differs at each start.
// Call these from one task at a time.
const char* ota_running_image();
const char* ota_boot_id();

// How much of the update slot's size the running firmware takes.
size_t ota_running_image_bytes();
size_t ota_slot_bytes();

// The running version, and whether it is still on trial.
const char* ota_running_version();
bool ota_on_trial();
// The whole seconds of the trial left, 0 off trial and once its time is out.
uint32_t ota_trial_seconds_left();

// Call once at the end of startup. A new image is on trial: if it reports itself
// unhealthy, or restarts in any way before kOtaTrialSeconds pass, the bootloader goes
// back to the previous one. The enclosure has no USB opening, so an update that cannot
// be replaced by the next update must not survive.
constexpr uint32_t kOtaTrialSeconds = 60;
void ota_start_trial(bool healthy);

}  // namespace garland
