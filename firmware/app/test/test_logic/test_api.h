/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Host tests for the hardware-free part of the public API
 * Contributors: None
 *
 * Included by test_main.cpp.
 */

#pragma once

#include "api_logic.h"
#include "network_logic.h"
#include "tls_logic.h"

static void test_the_public_api_is_told_from_the_portals() {
    TEST_ASSERT_TRUE(api_is_public("/api/v1/state"));
    TEST_ASSERT_TRUE(api_is_public("/api/v1/log?after=5"));
    TEST_ASSERT_FALSE(api_is_public("/api/state"));
    TEST_ASSERT_FALSE(api_is_public("/api/v1"));
    TEST_ASSERT_FALSE(api_is_public("/"));
    TEST_ASSERT_FALSE(api_is_public(""));
}

// A client depends on these words, so they are held here.
static void test_the_public_error_codes_stay() {
    TEST_ASSERT_EQUAL_STRING("bad_request", api_error_code(400));
    TEST_ASSERT_EQUAL_STRING("not_paired", api_error_code(401));
    TEST_ASSERT_EQUAL_STRING("not_found", api_error_code(404));
    TEST_ASSERT_EQUAL_STRING("method_not_allowed", api_error_code(405));
    TEST_ASSERT_EQUAL_STRING("timeout", api_error_code(408));
    TEST_ASSERT_EQUAL_STRING("conflict", api_error_code(409));
    TEST_ASSERT_EQUAL_STRING("too_large", api_error_code(413));
    TEST_ASSERT_EQUAL_STRING("try_later", api_error_code(429));
    TEST_ASSERT_EQUAL_STRING("internal", api_error_code(500));
    TEST_ASSERT_EQUAL_STRING("failed", api_error_code(503));
}

static void test_a_route_is_found_with_no_query() {
    TEST_ASSERT_EQUAL_size_t(11, api_path_length("/api/v1/log"));
    TEST_ASSERT_EQUAL_size_t(11, api_path_length("/api/v1/log?after=12"));
    TEST_ASSERT_EQUAL_size_t(0, api_path_length(""));
}

static void test_a_bearer_token_is_read_from_its_header() {
    TEST_ASSERT_EQUAL_STRING("abc123", auth_bearer_token("Bearer abc123"));
    TEST_ASSERT_EQUAL_STRING("abc123", auth_bearer_token("bearer abc123"));
    TEST_ASSERT_EQUAL_STRING("", auth_bearer_token("Bearer "));
    TEST_ASSERT_NULL(auth_bearer_token("Basic abc123"));
    TEST_ASSERT_NULL(auth_bearer_token("Bearer"));
    TEST_ASSERT_NULL(auth_bearer_token(""));
}

static void test_the_log_is_read_from_where_a_reader_got_to() {
    // All of it is there.
    LogWindow window = log_window(0, 100, 100);
    TEST_ASSERT_EQUAL_size_t(0, window.skip);
    TEST_ASSERT_EQUAL_UINT32(0, window.lost);
    // Only what is new.
    window = log_window(60, 100, 100);
    TEST_ASSERT_EQUAL_size_t(60, window.skip);
    TEST_ASSERT_EQUAL_UINT32(0, window.lost);
    // Nothing new.
    window = log_window(100, 100, 100);
    TEST_ASSERT_EQUAL_size_t(100, window.skip);
    // Came back too late: all that is kept, and how much went.
    window = log_window(100, 9000, 3000);
    TEST_ASSERT_EQUAL_size_t(0, window.skip);
    TEST_ASSERT_EQUAL_UINT32(5900, window.lost);
    // Its old buffer halved under it, but what it has not had is still there.
    window = log_window(8000, 9000, 3000);
    TEST_ASSERT_EQUAL_size_t(2000, window.skip);
    // The count wrapping changes nothing.
    window = log_window(UINT32_MAX - 9, 10, 100);
    TEST_ASSERT_EQUAL_size_t(80, window.skip);
    TEST_ASSERT_EQUAL_UINT32(0, window.lost);
    // Ahead of the log, as a position from before a restart: a new log, so all of it.
    window = log_window(9000, 100, 100);
    TEST_ASSERT_EQUAL_size_t(0, window.skip);
    TEST_ASSERT_EQUAL_UINT32(0, window.lost);
}

static void test_an_empty_password_keeps_the_saved_one() {
    TEST_ASSERT_TRUE(wifi_keeps_saved_password("Home", "", "Home"));
    TEST_ASSERT_FALSE(wifi_keeps_saved_password("Home", "newpassword", "Home"));
    TEST_ASSERT_FALSE(wifi_keeps_saved_password("Cafe", "", "Home"));
    TEST_ASSERT_FALSE(wifi_keeps_saved_password("Home", "", ""));
}

// internal/api/chain_test.go has the same bytes, and shows that Go takes a chain under them.
static void test_the_name_constraints_are_those_the_client_was_tried_with() {
    const uint8_t expected[] = {0x30, 0x17, 0xA0, 0x15, 0x30, 0x07, 0x82, 0x05, 'l',  'o',  'c',  'a',  'l',
                                0x30, 0x0A, 0x87, 0x08, 0xC0, 0xA8, 0x04, 0x01, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t der[kTlsConstraintsBytes];
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), tls_name_constraints_der("", "192.168.4.1", der, sizeof(der)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, der, sizeof(expected));
}

static void test_a_trial_ends_only_on_what_the_flash_says() {
    TEST_ASSERT_TRUE(ota_record_means_trial(OtaRecord::Pending));
    TEST_ASSERT_TRUE(ota_record_means_trial(OtaRecord::Unreadable));
    TEST_ASSERT_FALSE(ota_record_means_trial(OtaRecord::Valid));
    TEST_ASSERT_FALSE(ota_record_means_trial(OtaRecord::Absent));
    TEST_ASSERT_TRUE(ota_confirm_held(OtaRecord::Valid, true));
    TEST_ASSERT_FALSE(ota_confirm_held(OtaRecord::Valid, false));
    // A failed write lost this image's record, and confirming then answered OK for the other's.
    TEST_ASSERT_FALSE(ota_confirm_held(OtaRecord::Absent, false));
    TEST_ASSERT_FALSE(ota_confirm_held(OtaRecord::Absent, true));
    TEST_ASSERT_FALSE(ota_confirm_held(OtaRecord::Unreadable, true));
    TEST_ASSERT_FALSE(ota_confirm_held(OtaRecord::Pending, true));
}

// Only flash that says there is no identity lets a start make one: a read that failed keeps the root.
static void test_an_unreadable_identity_is_not_made_again() {
    TEST_ASSERT_TRUE(tls_may_make_identity(StoredRead::Missing));
    TEST_ASSERT_TRUE(tls_may_make_identity(StoredRead::Loaded));
    TEST_ASSERT_FALSE(tls_may_make_identity(StoredRead::Unreadable));
    // A domain that could not be read keeps the root as well, though the identity was read.
    TEST_ASSERT_TRUE(tls_may_make_identity(StoredRead::Loaded, StoredRead::Missing));
    TEST_ASSERT_TRUE(tls_may_make_identity(StoredRead::Loaded, StoredRead::Loaded));
    TEST_ASSERT_FALSE(tls_may_make_identity(StoredRead::Loaded, StoredRead::Unreadable));
    TEST_ASSERT_FALSE(tls_may_make_identity(StoredRead::Unreadable, StoredRead::Loaded));
}

// A stream holds while its client stays paired with that token, in its days: forgetting it, pairing
// another in its place, or its days running out each end it.
static void test_a_grant_holds_only_while_its_client_does() {
    AuthDevices devices{};
    devices[2].in_use = true;
    devices[2].hash.fill(7);
    devices[2].expires_epoch = 2000;
    AuthGrant grant;
    grant.slot = 2;
    grant.hash.fill(7);
    TEST_ASSERT_TRUE(auth_grant_holds(devices, grant, 1000));
    TEST_ASSERT_FALSE(auth_grant_holds(devices, grant, 2000));
    devices[2].hash.fill(8);
    TEST_ASSERT_FALSE(auth_grant_holds(devices, grant, 1000));
    devices[2] = AuthDevice{};
    TEST_ASSERT_FALSE(auth_grant_holds(devices, grant, 1000));
    grant.slot = -1;
    TEST_ASSERT_FALSE(auth_grant_holds(devices, grant, 1000));
}

// A key is given up only when reading it shows it can never serve: running out of memory reading a
// sound key keeps every key as saved.
static void test_only_a_broken_key_is_replaced() {
    TEST_ASSERT_TRUE(tls_recovery(KeyCheck::Sound, KeyCheck::Sound) == TlsRecovery::Retry);
    TEST_ASSERT_TRUE(tls_recovery(KeyCheck::Broken, KeyCheck::Sound) == TlsRecovery::NewKeys);
    TEST_ASSERT_TRUE(tls_recovery(KeyCheck::Sound, KeyCheck::Broken) == TlsRecovery::NewPortalKey);
    TEST_ASSERT_TRUE(tls_recovery(KeyCheck::Broken, KeyCheck::Broken) == TlsRecovery::NewKeys);
    TEST_ASSERT_TRUE(tls_recovery(KeyCheck::Unknown, KeyCheck::Sound) == TlsRecovery::Keep);
    TEST_ASSERT_TRUE(tls_recovery(KeyCheck::Sound, KeyCheck::Unknown) == TlsRecovery::Keep);
    TEST_ASSERT_TRUE(tls_recovery(KeyCheck::Broken, KeyCheck::Unknown) == TlsRecovery::Keep);
}
// A client counts down from what the garland says, rounded up, and through a wrap of its clock.
static void test_a_trial_says_how_long_it_has_left() {
    TEST_ASSERT_EQUAL_UINT32(60, ota_trial_seconds_left(1000, 1000, 60));
    TEST_ASSERT_EQUAL_UINT32(60, ota_trial_seconds_left(1000, 1001, 60));
    TEST_ASSERT_EQUAL_UINT32(42, ota_trial_seconds_left(1000, 19000, 60));
    TEST_ASSERT_EQUAL_UINT32(1, ota_trial_seconds_left(1000, 60999, 60));
    TEST_ASSERT_EQUAL_UINT32(0, ota_trial_seconds_left(1000, 61000, 60));
    TEST_ASSERT_EQUAL_UINT32(0, ota_trial_seconds_left(1000, 500000, 60));
    TEST_ASSERT_EQUAL_UINT32(50, ota_trial_seconds_left(0xFFFFF000u, 0x00001800u, 60));
}

static void test_a_refused_update_says_whose_problem_it_is() {
    for (const char* const file : {"not a csiGarland update file", "update file format not supported", "update file has no usable certificate",
                                   "update file is incomplete", "update does not fit the update slot", "the effects pack does not fit",
                                   "this file was changed after signing", "the image is damaged or not for this board",
                                   "this file was not signed by a certificate this garland trusts",
                                   "the signature is not valid: this file was changed after signing"}) {
        TEST_ASSERT_EQUAL_STRING_MESSAGE("400 Bad Request", ota_problem_status(file), file);
    }
    for (const char* const garland : {"could not write the update", "could not start the update", "could not check the signature",
                                      "could not switch to the update", "could not write the effects pack", "could not read the certificate",
                                      "no update slot: this board needs one flash over USB first",
                                      "the effects pack did not reach the flash intact"}) {
        TEST_ASSERT_EQUAL_STRING_MESSAGE("500 Internal Server Error", ota_problem_status(garland), garland);
    }
    TEST_ASSERT_EQUAL_STRING("408 Request Timeout", ota_problem_status("the connection was lost"));
    TEST_ASSERT_EQUAL_STRING("409 Conflict", ota_problem_status("the running firmware is still on trial: try again in a minute"));
    TEST_ASSERT_EQUAL_STRING("409 Conflict", ota_problem_status("the garland is restarting: send this again after that"));
}

static void run_api_tests() {
    RUN_TEST(test_the_public_api_is_told_from_the_portals);
    RUN_TEST(test_the_public_error_codes_stay);
    RUN_TEST(test_a_route_is_found_with_no_query);
    RUN_TEST(test_a_bearer_token_is_read_from_its_header);
    RUN_TEST(test_the_log_is_read_from_where_a_reader_got_to);
    RUN_TEST(test_an_empty_password_keeps_the_saved_one);
    RUN_TEST(test_a_refused_update_says_whose_problem_it_is);
    RUN_TEST(test_a_trial_ends_only_on_what_the_flash_says);
    RUN_TEST(test_a_trial_says_how_long_it_has_left);
    RUN_TEST(test_an_unreadable_identity_is_not_made_again);
    RUN_TEST(test_only_a_broken_key_is_replaced);
    RUN_TEST(test_a_grant_holds_only_while_its_client_does);
    RUN_TEST(test_the_name_constraints_are_those_the_client_was_tried_with);
}
