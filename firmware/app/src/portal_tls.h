/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The portal's root, key and certificate, made on the board and kept in encrypted NVS
 * Contributors: None
 */

#pragma once

#include "garland_state.h"
#include "tls_logic.h"

namespace garland {

// Loads the identity, making whatever is missing, stale or unusable, with a screen saying so
// while it takes. Makes its own task for that, as main's stack is too small. nullptr without one.
const TlsIdentity* portal_tls_identity(const char* hostname, State& state);

// The identity already in use, or nullptr before there is one.
const TlsIdentity* portal_tls_identity_loaded();

// By the real time now: what is in use has to be made anew, which happens at a start.
bool portal_tls_renewal_due(int64_t now_epoch);

// Leaves word for that start of what the real time is and which certificates it rules out.
bool portal_tls_prepare_renewal(int64_t now_epoch);

}  // namespace garland
