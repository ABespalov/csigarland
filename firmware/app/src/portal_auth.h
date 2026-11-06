/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Who may use the portal: browsers paired with the panel's buttons
 * Contributors: None
 */

#pragma once

#include "auth_logic.h"
#include "esp_http_server.h"
#include "garland_state.h"

namespace garland {

// Before the buttons start, as they ask about requests from their first press. Needs nothing
// else to be working: with no portal no request ever waits, and the buttons work as ever.
void auth_init(State& state);

// Whether cookies say Secure: not while HTTPS is failing, or no browser would send them back.
void auth_set_secure_cookies(bool secure);

// Once settings are up: reads the paired browsers.
void auth_load_devices();

// The request came to the setup network's address. Joining it took the password on the screen,
// and the phone windows that open it neither accept our certificate nor keep a cookie.
bool auth_on_setup_network(httpd_req_t* req);

// The request on the setup network names another site, which that network's DNS sent here.
bool auth_setup_host_is_foreign(httpd_req_t* req);

// For what asks for no pairing, so a page must not reach it on the setup network from another name,
// which that network's DNS sends here: the token a pairing makes would go to that page.
bool auth_open_route_allows(httpd_req_t* req);

// Allowed with a paired browser's cookie or program's token, or on the setup network. Unreadable when the
// token was looked for in a list of paired browsers that is there and could not be read yet, so the client
// may be paired all the same: told 503, not 401, which a program takes to mean its token is gone and lets
// it go. Of this request's own look, as another request may read the list in a moment after it.
enum class AuthAnswer : uint8_t { Allowed, NotPaired, Unreadable };
AuthAnswer auth_check(httpd_req_t* req);

// What a request was let in under, for a long one to be held to later: see auth_grant_holds.
AuthGrant auth_grant_of(httpd_req_t* req);
// Whether that still holds: the client still paired with that token and in its days, or the setup
// network still up. Forgetting a client, forgetting all, or its days running out end it.
bool auth_grant_holds(const AuthGrant& grant);

// As auth_handle_wait, and answers a program with the token itself.
esp_err_t auth_handle_wait_for_token(httpd_req_t* req);

// Handlers, called by the portal. The first two are open to anyone.
esp_err_t auth_handle_request(httpd_req_t* req);
esp_err_t auth_handle_wait(httpd_req_t* req);
esp_err_t auth_handle_devices(httpd_req_t* req);
esp_err_t auth_handle_forget(httpd_req_t* req);

// From the button callbacks. These only touch memory, so they return at once.
bool auth_request_is_waiting();
bool auth_buttons_approve();
void auth_button_refuse();
void auth_forget_all_soon();

}  // namespace garland
