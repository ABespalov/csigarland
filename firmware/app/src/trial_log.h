/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The log of a new firmware on trial, readable over the network
 * Contributors: None
 *
 * A closed unit has no USB to read a log from, and a firmware that fails its trial is gone
 * again within seconds. So while on trial it keeps its log in memory and hands it out, and
 * deploy/ota-update.ps1 prints it as it comes. What gets out before a crash says how far it got.
 */

#pragma once

#include <cstddef>

#include "esp_http_server.h"

namespace garland {

// First thing at startup. Does nothing unless the running firmware is on trial.
void trial_log_start();

// Answers with the first line "version=... on_trial=0|1 https=0|1 boot=... image=... pack=...", and,
// while on trial, the log after it. Open to anyone and over plain HTTP, as the portal may not be up,
// so only while on trial, or while HTTPS has failed and this is how the owner finds out why. With
// HTTPS down and the trial over, that line is the whole answer. 404 otherwise.
esp_err_t trial_log_handle(httpd_req_t* req, bool https_running);

// GET /api/v1/log?after=: the log since a position, for a paired client. Starts keeping it.
esp_err_t public_log_handle(httpd_req_t* req);

}  // namespace garland
