/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Host tests for who may use the portal
 * Contributors: None
 */

#pragma once

#include "tls_logic.h"

using PairId = std::array<uint8_t, kPairRequestBytes>;

static PairId pair_id(uint8_t first) {
    PairId id{};
    id[0] = first;
    return id;
}

static AuthDevice auth_device(uint8_t first_hash_byte, int64_t paired, int64_t expires) {
    AuthDevice device{};
    device.in_use = true;
    device.hash[0] = first_hash_byte;
    device.paired_epoch = paired;
    device.expires_epoch = expires;
    return device;
}

static void test_pairing_needs_the_owners_answer() {
    Pairing pairing;
    TEST_ASSERT_TRUE(pairing.start(pair_id(1), 1234, 1000));
    TEST_ASSERT_EQUAL_UINT16(1234, pairing.code());
    TEST_ASSERT_TRUE(pairing.waiting(1000));
    TEST_ASSERT_TRUE(pairing.poll(pair_id(1), 2000) == PairPoll::Waiting);
    TEST_ASSERT_TRUE(pairing.approve(3000));
    TEST_ASSERT_FALSE(pairing.waiting(3000));
    // Only the id's holder gets it, and only once.
    TEST_ASSERT_TRUE(pairing.poll(pair_id(2), 3000) == PairPoll::Gone);
    TEST_ASSERT_TRUE(pairing.poll(pair_id(1), 3000) == PairPoll::Approved);
    TEST_ASSERT_TRUE(pairing.poll(pair_id(1), 3000) == PairPoll::Gone);
}

static uint32_t ipv4_in_memory(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    const uint8_t bytes[4] = {a, b, c, d};
    uint32_t address = 0;
    std::memcpy(&address, bytes, sizeof(address));
    return address;
}

static void test_only_the_setup_networks_own_clients_count_as_on_it() {
    const uint32_t portal = ipv4_in_memory(192, 168, 4, 1);
    TEST_ASSERT_TRUE(auth_is_setup_client(portal, ipv4_in_memory(192, 168, 4, 2), portal));
    TEST_ASSERT_TRUE(auth_is_setup_client(portal, ipv4_in_memory(192, 168, 4, 200), portal));
    // A home computer that routes to the setup address reaches the same socket.
    TEST_ASSERT_FALSE(auth_is_setup_client(portal, ipv4_in_memory(192, 168, 1, 50), portal));
    TEST_ASSERT_FALSE(auth_is_setup_client(portal, ipv4_in_memory(10, 0, 0, 7), portal));
    // And the home address, whoever asks.
    TEST_ASSERT_FALSE(auth_is_setup_client(ipv4_in_memory(192, 168, 1, 9), ipv4_in_memory(192, 168, 4, 2), portal));
    // Only while the setup network is up, and a home network of the same /24 never passes for it.
    TEST_ASSERT_TRUE(auth_setup_network_applies(true, ipv4_in_memory(192, 168, 1, 9), portal));
    TEST_ASSERT_TRUE(auth_setup_network_applies(true, 0, portal));
    TEST_ASSERT_FALSE(auth_setup_network_applies(false, 0, portal));
    TEST_ASSERT_FALSE(auth_setup_network_applies(true, ipv4_in_memory(192, 168, 4, 1), portal));
    TEST_ASSERT_FALSE(auth_setup_network_applies(true, ipv4_in_memory(192, 168, 4, 30), portal));
}

static void test_a_page_may_send_to_the_setup_network_only_from_its_own_address() {
    TEST_ASSERT_TRUE(auth_origin_is_own("http://192.168.4.1", "192.168.4.1", false));
    TEST_ASSERT_TRUE(auth_origin_is_own("https://garland.example.com:8443", "garland.example.com:8443", true));
    TEST_ASSERT_FALSE(auth_origin_is_own("https://evil.example", "192.168.4.1", true));
    TEST_ASSERT_FALSE(auth_origin_is_own("http://192.168.4.1.evil.example", "192.168.4.1", false));
    // A sandboxed frame or a local file says this.
    TEST_ASSERT_FALSE(auth_origin_is_own("null", "192.168.4.1", false));
    TEST_ASSERT_FALSE(auth_origin_is_own("ftp://192.168.4.1", "192.168.4.1", false));
}

// Firefox and Safari send the HTTPS cookie with a request of a page on plain HTTP under the same name, so
// whoever stands in for the plain page must not pass for the portal on HTTPS, nor the other way round.
static void test_an_origin_counts_only_by_the_scheme_of_the_server_it_came_to() {
    TEST_ASSERT_TRUE(auth_origin_is_own("https://csigarland.local", "csigarland.local", true));
    TEST_ASSERT_FALSE(auth_origin_is_own("http://csigarland.local", "csigarland.local", true));
    TEST_ASSERT_TRUE(auth_origin_is_own("http://csigarland.local", "csigarland.local", false));
    TEST_ASSERT_FALSE(auth_origin_is_own("https://csigarland.local", "csigarland.local", false));
    TEST_ASSERT_FALSE(auth_cookie_origin_ok("http://csigarland.local", "csigarland.local", true));
}

// A paired browser's cookie goes with a sibling origin's request under the same domain too.
static void test_a_cookie_counts_only_from_the_portals_own_origin() {
    TEST_ASSERT_TRUE(auth_cookie_origin_ok("https://garland.example.com", "garland.example.com", true));
    TEST_ASSERT_TRUE(auth_cookie_origin_ok(nullptr, "garland.example.com", true));
    TEST_ASSERT_FALSE(auth_cookie_origin_ok("https://evil.example.com", "garland.example.com", true));
    TEST_ASSERT_FALSE(auth_cookie_origin_ok("http://garland.example.com", "garland.example.com:8443", false));
    TEST_ASSERT_FALSE(auth_cookie_origin_ok("null", "csigarland.local", true));
}

static void test_the_name_in_a_host_header_is_read_without_its_port() {
    TEST_ASSERT_EQUAL(11, auth_host_name_length("192.168.4.1"));
    TEST_ASSERT_EQUAL(11, auth_host_name_length("192.168.4.1:80"));
    TEST_ASSERT_EQUAL(13, auth_host_name_length("garland.local:443"));
    TEST_ASSERT_EQUAL(0, auth_host_name_length(""));
    TEST_ASSERT_EQUAL(0, auth_host_name_length("garland.local:"));
    TEST_ASSERT_EQUAL(0, auth_host_name_length("garland.local:80x"));
    TEST_ASSERT_EQUAL(0, auth_host_name_length("[fe80::1]:80"));
    TEST_ASSERT_EQUAL(0, auth_host_name_length("evil.example/path"));
    TEST_ASSERT_EQUAL(0, auth_host_name_length("me@evil.example"));
}

// The setup network's DNS sends every name here, so a site could become the portal to its own page.
static void test_the_setup_network_answers_only_to_the_portals_own_names() {
    const char* const ip = "192.168.4.1";
    TEST_ASSERT_TRUE(auth_host_is_own("192.168.4.1", ip, "garland"));
    TEST_ASSERT_TRUE(auth_host_is_own("192.168.4.1:80", ip, "garland"));
    TEST_ASSERT_TRUE(auth_host_is_own("garland.local", ip, "garland"));
    TEST_ASSERT_TRUE(auth_host_is_own("Garland.LOCAL:443", ip, "garland"));
    TEST_ASSERT_FALSE(auth_host_is_own("evil.example", ip, "garland"));
    TEST_ASSERT_FALSE(auth_host_is_own("192.168.4.1.evil.example", ip, "garland"));
    TEST_ASSERT_FALSE(auth_host_is_own("garland.local.evil.example", ip, "garland"));
    TEST_ASSERT_FALSE(auth_host_is_own("garland", ip, "garland"));
    TEST_ASSERT_FALSE(auth_host_is_own("other.local", ip, "garland"));
    TEST_ASSERT_FALSE(auth_host_is_own("", ip, "garland"));
}

static void test_forgetting_everyone_ends_what_pairing_has_under_way() {
    Pairing pairing;
    TEST_ASSERT_TRUE(pairing.start(pair_id(1), 1, 1000));
    pairing.revoke();
    TEST_ASSERT_FALSE(pairing.waiting(1000));
    TEST_ASSERT_FALSE(pairing.approve(1000));

    // An answer nobody has collected yet goes as well.
    TEST_ASSERT_TRUE(pairing.start(pair_id(2), 2, 1000));
    TEST_ASSERT_TRUE(pairing.approve(1000));
    pairing.revoke();
    TEST_ASSERT_TRUE(pairing.poll(pair_id(2), 1000) == PairPoll::Gone);

    // One just collected, whose token is not made yet, can tell it no longer counts.
    TEST_ASSERT_TRUE(pairing.start(pair_id(3), 3, 1000));
    TEST_ASSERT_TRUE(pairing.approve(1000));
    TEST_ASSERT_TRUE(pairing.poll(pair_id(3), 1000) == PairPoll::Approved);
    const uint32_t approved_in = pairing.generation();
    pairing.revoke();
    TEST_ASSERT_NOT_EQUAL(approved_in, pairing.generation());

    // A new request needs a new answer, and is free to ask.
    TEST_ASSERT_TRUE(pairing.start(pair_id(4), 4, 1000));
    TEST_ASSERT_TRUE(pairing.poll(pair_id(4), 1000) == PairPoll::Waiting);
}

static void test_pairing_takes_one_request_and_expires() {
    Pairing pairing;
    TEST_ASSERT_TRUE(pairing.start(pair_id(1), 1, 1000));
    TEST_ASSERT_FALSE(pairing.start(pair_id(2), 2, 2000));
    TEST_ASSERT_EQUAL_UINT16(1, pairing.code());
    // Too late to approve, and room for the next.
    TEST_ASSERT_FALSE(pairing.approve(1000 + kPairWaitMs));
    TEST_ASSERT_TRUE(pairing.poll(pair_id(1), 1000 + kPairWaitMs) == PairPoll::Gone);
    TEST_ASSERT_TRUE(pairing.start(pair_id(2), 2, 1000 + kPairWaitMs));

    // An approval nobody claims runs out as well.
    TEST_ASSERT_TRUE(pairing.approve(1000 + kPairWaitMs));
    TEST_ASSERT_TRUE(pairing.poll(pair_id(2), 1000 + 2 * kPairWaitMs) == PairPoll::Gone);
}

static void test_pairing_survives_the_clock_wrapping() {
    Pairing pairing;
    constexpr uint32_t kNearWrap = 0xFFFFFFFFu - 1000;
    TEST_ASSERT_TRUE(pairing.start(pair_id(1), 1, kNearWrap));
    TEST_ASSERT_TRUE(pairing.waiting(kNearWrap + 2000));
    TEST_ASSERT_FALSE(pairing.waiting(kNearWrap + kPairWaitMs));
}

static void test_refusing_holds_off_new_requests() {
    Pairing pairing;
    TEST_ASSERT_TRUE(pairing.start(pair_id(1), 1, 1000));
    pairing.refuse(2000);
    TEST_ASSERT_FALSE(pairing.waiting(2000));
    TEST_ASSERT_TRUE(pairing.poll(pair_id(1), 2000) == PairPoll::Gone);
    TEST_ASSERT_EQUAL_UINT32(kPairRefusedMs, pairing.retry_after_ms(2000));
    TEST_ASSERT_EQUAL_UINT32(1, pairing.retry_after_ms(2000 + kPairRefusedMs - 1));
    TEST_ASSERT_FALSE(pairing.start(pair_id(2), 2, 2000 + kPairRefusedMs - 1));
    TEST_ASSERT_EQUAL_UINT32(0, pairing.retry_after_ms(2000 + kPairRefusedMs));
    TEST_ASSERT_TRUE(pairing.start(pair_id(2), 2, 2000 + kPairRefusedMs));
}

static void test_a_token_counts_while_it_is_current() {
    AuthDevices devices{};
    devices[3] = auth_device(0xAB, 1000, 5000);
    std::array<uint8_t, kAuthHashBytes> hash{};
    hash[0] = 0xAB;
    TEST_ASSERT_EQUAL_INT(3, auth_find(devices, hash, 4999));
    TEST_ASSERT_EQUAL_INT(-1, auth_find(devices, hash, 5000));
    // With no clock the board cannot say it has run out.
    TEST_ASSERT_EQUAL_INT(3, auth_find(devices, hash, 0));
    hash[0] = 0xAC;
    TEST_ASSERT_EQUAL_INT(-1, auth_find(devices, hash, 4999));
    // The all-zero hash of an empty entry opens nothing.
    hash[0] = 0;
    TEST_ASSERT_EQUAL_INT(-1, auth_find(devices, hash, 4999));
}

static void test_pairing_before_the_clock_starts_the_days_when_it_comes() {
    AuthDevices devices{};
    devices[0] = auth_device(1, 0, 0);
    TEST_ASSERT_FALSE(auth_settle_clock(devices, 0));
    TEST_ASSERT_TRUE(auth_settle_clock(devices, 1000));
    TEST_ASSERT_EQUAL_INT64(1000, devices[0].paired_epoch);
    TEST_ASSERT_EQUAL_INT64(1000 + 100LL * 86400, devices[0].expires_epoch);
    TEST_ASSERT_FALSE(auth_settle_clock(devices, 2000));
}

static void test_a_new_browser_takes_free_then_expired_then_oldest() {
    AuthDevices devices{};
    TEST_ASSERT_EQUAL_size_t(0, auth_slot_for_new(devices, 1000));
    for (size_t i = 0; i < devices.size(); ++i) {
        devices[i] = auth_device(static_cast<uint8_t>(i + 1), 100 + static_cast<int64_t>(i), 9000);
    }
    devices[5].paired_epoch = 1;
    TEST_ASSERT_EQUAL_size_t(5, auth_slot_for_new(devices, 1000));
    devices[6].expires_epoch = 500;
    TEST_ASSERT_EQUAL_size_t(6, auth_slot_for_new(devices, 1000));
}

static void test_tokens_travel_as_exact_hex() {
    const std::array<uint8_t, 2> bytes = {0xAB, 0x01};
    char text[5];
    auth_to_hex(bytes.data(), bytes.size(), text);
    TEST_ASSERT_EQUAL_STRING("ab01", text);
    std::array<uint8_t, 2> back{};
    TEST_ASSERT_TRUE(auth_from_hex("ab01", back.data(), back.size()));
    TEST_ASSERT_TRUE(back == bytes);
    TEST_ASSERT_FALSE(auth_from_hex("ab0", back.data(), back.size()));
    TEST_ASSERT_FALSE(auth_from_hex("ab011", back.data(), back.size()));
    TEST_ASSERT_FALSE(auth_from_hex("AB01", back.data(), back.size()));
    TEST_ASSERT_FALSE(auth_from_hex("zz01", back.data(), back.size()));
}

static void test_a_label_is_cleaned_and_cut() {
    char label[kAuthLabelBytes];
    auth_clean_label("Windows Chrome", label);
    TEST_ASSERT_EQUAL_STRING("Windows Chrome", label);
    auth_clean_label("a\nb\x7F", label);
    TEST_ASSERT_EQUAL_STRING("a?b?", label);
    auth_clean_label("0123456789012345678901234567890123456789", label);
    TEST_ASSERT_EQUAL_size_t(kAuthLabelBytes - 1, std::strlen(label));
}

static void test_the_access_screen_comes_after_an_update_only() {
    Snapshot s{};
    s.led_setting = 32;
    s.update_percent = kNoUpdate;
    s.pair_display = pair_display_for_code(0);
    s.off_seconds = 3;
    s.setup_network = true;
    TEST_ASSERT_TRUE(choose_screen(s) == ScreenId::Access);
    const ScreenInputs inputs{"Effect", "Ssid", "Pass", "1.2.3.4"};
    TEST_ASSERT_NOT_NULL(std::strstr(build_screen_model(s, inputs).text, "Code 0000"));
    s.pair_display = pair_display_for_code(9999);
    TEST_ASSERT_NOT_NULL(std::strstr(build_screen_model(s, inputs).text, "Code 9999"));
    s.pair_display = kPairDisplayForgotten;
    TEST_ASSERT_NOT_NULL(std::strstr(build_screen_model(s, inputs).text, "forgotten"));
    s.update_percent = 40;
    TEST_ASSERT_TRUE(choose_screen(s) == ScreenId::UpdateReceiving);
}

static void test_what_a_browser_sends_is_held_to_the_rules() {
    TEST_ASSERT_TRUE(tls_domain_is_valid(""));
    TEST_ASSERT_TRUE(tls_domain_is_valid("garland.example.com"));
    TEST_ASSERT_TRUE(tls_domain_is_valid("my-garland.example.am"));
    TEST_ASSERT_FALSE(tls_domain_is_valid("garland"));
    TEST_ASSERT_FALSE(tls_domain_is_valid("Garland.example.com"));
    TEST_ASSERT_FALSE(tls_domain_is_valid(".example.com"));
    TEST_ASSERT_FALSE(tls_domain_is_valid("garland..example.com"));
    TEST_ASSERT_FALSE(tls_domain_is_valid("garland.example.com."));
    TEST_ASSERT_FALSE(tls_domain_is_valid("-garland.example.com"));
    TEST_ASSERT_FALSE(tls_domain_is_valid("garland-.example.com"));
    TEST_ASSERT_FALSE(tls_domain_is_valid("garland.example.com,other.example.com"));
    TEST_ASSERT_FALSE(tls_domain_is_valid("garland.example.com/x"));
    // A fixed address will do as well, if it is one.
    TEST_ASSERT_TRUE(tls_domain_is_valid("203.0.113.10"));
    TEST_ASSERT_TRUE(tls_domain_is_valid("192.168.1.50"));
    TEST_ASSERT_FALSE(tls_domain_is_valid("203.0.113.256"));
    TEST_ASSERT_FALSE(tls_domain_is_valid("203.0.113"));
    TEST_ASSERT_FALSE(tls_domain_is_valid("203.0.113.10.1"));
    TEST_ASSERT_FALSE(tls_domain_is_valid("0.0.0.0"));
    TEST_ASSERT_FALSE(tls_domain_is_valid("127.0.0.1"));
    TEST_ASSERT_FALSE(tls_domain_is_valid("239.1.1.1"));
    TEST_ASSERT_FALSE(tls_domain_is_valid(" 203.0.113.10"));
    TEST_ASSERT_TRUE(tls_domain_is_valid("203.0.113.example"));
    TEST_ASSERT_TRUE(tls_domain_is_valid("garland.x1"));

    int64_t epoch = 0;
    TEST_ASSERT_TRUE(rtc_epoch_from_number(1800000000.0, epoch));
    TEST_ASSERT_EQUAL_INT64(1800000000, epoch);
    TEST_ASSERT_FALSE(rtc_epoch_from_number(1800000000.5, epoch));
    TEST_ASSERT_FALSE(rtc_epoch_from_number(1.0, epoch));
    TEST_ASSERT_FALSE(rtc_epoch_from_number(1e300, epoch));
    TEST_ASSERT_FALSE(rtc_epoch_from_number(-1e300, epoch));
    TEST_ASSERT_FALSE(rtc_epoch_from_number(__builtin_nan(""), epoch));
    TEST_ASSERT_FALSE(rtc_epoch_from_number(__builtin_inf(), epoch));
}

static void test_certificate_dates_and_names() {
    char text[kTlsTimeBytes];
    tls_format_time(1793491200, text);  // 2026-11-01 00:00:00 UTC.
    TEST_ASSERT_EQUAL_STRING("20261101000000", text);
    TEST_ASSERT_EQUAL_INT64(1793491200, tls_epoch_from_build_date("Nov  1 2026"));
    TEST_ASSERT_EQUAL_INT64(kRtcEarliestEpoch, tls_epoch_from_build_date("nonsense"));

    char names[kTlsNamesBytes];
    tls_wanted_names("csigarland", "", "192.168.4.1", names);
    TEST_ASSERT_EQUAL_STRING("csigarland.local,192.168.4.1", names);
    tls_wanted_names("csigarland", "garland.example.com", "192.168.4.1", names);
    TEST_ASSERT_EQUAL_STRING("csigarland.local,garland.example.com,192.168.4.1", names);

    static TlsIdentity identity{};
    TEST_ASSERT_TRUE(tls_root_is_stale(identity, "", 1000));
    TEST_ASSERT_TRUE(tls_certificate_is_stale(identity, names, 1000));
    std::strcpy(identity.root_certificate, "x");
    std::strcpy(identity.certificate, "x");
    std::strcpy(identity.names, names);
    std::strcpy(identity.root_domain, "garland.example.com");
    identity.root_not_after = 1000 + kTlsRenewBeforeSeconds + 1;
    identity.not_after = 1000 + kTlsRenewBeforeSeconds + 1;
    TEST_ASSERT_FALSE(tls_root_is_stale(identity, "garland.example.com", 1000));
    TEST_ASSERT_FALSE(tls_certificate_is_stale(identity, names, 1000));
    // A new domain means a new root, a new name only a new certificate.
    TEST_ASSERT_TRUE(tls_root_is_stale(identity, "other.example.com", 1000));
    TEST_ASSERT_TRUE(tls_certificate_is_stale(identity, "new.local,192.168.4.1", 1000));
    // Ahead of the dates running out.
    TEST_ASSERT_TRUE(tls_root_is_stale(identity, "garland.example.com", 2000));
    TEST_ASSERT_TRUE(tls_certificate_is_stale(identity, names, 2000));
}

static void test_name_constraints_are_exact_der() {
    std::array<uint8_t, kTlsConstraintsBytes> der{};
    // Checked against the cryptography package's NameConstraints.
    const uint8_t with_domain[] = {0x30, 0x24, 0xA0, 0x22, 0x30, 0x07, 0x82, 0x05, 'l', 'o', 'c', 'a', 'l', 0x30, 0x0B, 0x82, 0x09,
                                   'a', '.', 'e', 'x', 'a', 'm', 'p', 'l', 'e', 0x30, 0x0A, 0x87, 0x08, 0xC0, 0xA8, 0x04, 0x01, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_size_t(sizeof(with_domain), tls_name_constraints_der("a.example", "192.168.4.1", der.data(), der.size()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(with_domain, der.data(), sizeof(with_domain));
    const uint8_t without[] = {0x30, 0x17, 0xA0, 0x15, 0x30, 0x07, 0x82, 0x05, 'l', 'o', 'c', 'a', 'l',
                               0x30, 0x0A, 0x87, 0x08, 0xC0, 0xA8, 0x04, 0x01, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_size_t(sizeof(without), tls_name_constraints_der("", "192.168.4.1", der.data(), der.size()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(without, der.data(), sizeof(without));
    // The longest domain stays in DER's short form.
    const std::string longest(kTlsDomainBytes - 1, 'a');
    const size_t bytes = tls_name_constraints_der(longest.c_str(), "192.168.4.1", der.data(), der.size());
    TEST_ASSERT_GREATER_THAN_size_t(0, bytes);
    TEST_ASSERT_LESS_THAN_UINT8(128, der[1]);
    TEST_ASSERT_EQUAL_size_t(0, tls_name_constraints_der("a.example", "not an address", der.data(), der.size()));

    // A fixed address goes in as an address with its full mask, not as a name.
    const uint8_t with_address[] = {0x30, 0x23, 0xA0, 0x21, 0x30, 0x07, 0x82, 0x05, 'l', 'o', 'c', 'a', 'l',
                                    0x30, 0x0A, 0x87, 0x08, 0xCB, 0x00, 0x71, 0x0A, 0xFF, 0xFF, 0xFF, 0xFF,
                                    0x30, 0x0A, 0x87, 0x08, 0xC0, 0xA8, 0x04, 0x01, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_size_t(sizeof(with_address), tls_name_constraints_der("203.0.113.10", "192.168.4.1", der.data(), der.size()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(with_address, der.data(), sizeof(with_address));
}

static void test_an_action_arrives_whole() {
    State state;
    state.set_effect_count(4);
    state.apply_action(40, 8, 2, 1);
    Snapshot s = state.snapshot();
    TEST_ASSERT_EQUAL_UINT8(40, s.brightness_percent);
    TEST_ASSERT_EQUAL_UINT8(8, s.speed_x4);
    TEST_ASSERT_EQUAL_UINT8(2, s.effect);
    TEST_ASSERT_TRUE(s.power_requested);
    TEST_ASSERT_TRUE(s.power_chosen);
    // Only what it names.
    state.apply_action(0, 0, -1, 0);
    s = state.snapshot();
    TEST_ASSERT_EQUAL_UINT8(40, s.brightness_percent);
    TEST_ASSERT_EQUAL_UINT8(2, s.effect);
    TEST_ASSERT_FALSE(s.power_requested);
    TEST_ASSERT_FALSE(s.power_chosen);
    // A garland that refuses is no change of the owner's mind.
    state.apply_action(0, 0, -1, 1);
    state.refuse_power();
    s = state.snapshot();
    TEST_ASSERT_FALSE(s.power_requested);
    TEST_ASSERT_TRUE(s.power_chosen);
}

static int s_power_notes = 0;

static void test_a_power_order_is_told_at_once() {
    State state;
    s_power_notes = 0;
    state.set_power_listener([] { ++s_power_notes; });
    state.set_brightness(30);
    TEST_ASSERT_EQUAL_INT(0, s_power_notes);
    state.set_power(true);
    TEST_ASSERT_EQUAL_INT(1, s_power_notes);
    state.activate_effect(0);
    TEST_ASSERT_EQUAL_INT(2, s_power_notes);
    state.apply_action(50, 0, -1, -1);
    TEST_ASSERT_EQUAL_INT(2, s_power_notes);
    state.apply_action(0, 0, -1, 0);
    TEST_ASSERT_EQUAL_INT(3, s_power_notes);
}

static void test_a_replaced_effect_shows_in_the_snapshot() {
    State state;
    const Snapshot before = state.snapshot();
    state.note_catalog_changed();
    TEST_ASSERT_FALSE(state.snapshot() == before);
}

static std::array<Rgb, 8> new_effect_frame(const char* fx, uint32_t time_ms, int ix = 64) {
    char json[200];
    std::snprintf(json, sizeof(json), R"({"format":"csigarland.effect/1","name":"T","layers":[{"fx":"%s","ix":%d,"col":["FFFFFF"]}]})", fx, ix);
    EffectSpec spec;
    const char* error = nullptr;
    TEST_ASSERT_TRUE_MESSAGE(parse_effect(json, "t", spec, error), error);
    static EffectRenderer renderer(1);
    renderer.reset();
    std::array<Rgb, 8> frame{};
    renderer.render(spec, time_ms, frame);
    return frame;
}

static int lit_count(const std::array<Rgb, 8>& frame) {
    int lit = 0;
    for (const Rgb& led : frame) {
        lit += led.r != 0 ? 1 : 0;
    }
    return lit;
}

static void test_build_up_stays_on_then_clears() {
    // ix 64 means 3 groups: nothing, then a third, two thirds, all, and round again.
    TEST_ASSERT_EQUAL_INT(0, lit_count(new_effect_frame("build-up", 100)));
    TEST_ASSERT_EQUAL_INT(3, lit_count(new_effect_frame("build-up", 500)));
    TEST_ASSERT_EQUAL_INT(6, lit_count(new_effect_frame("build-up", 900)));
    TEST_ASSERT_EQUAL_INT(8, lit_count(new_effect_frame("build-up", 1300)));
    TEST_ASSERT_EQUAL_INT(0, lit_count(new_effect_frame("build-up", 1700)));
}

static void test_cross_fade_is_never_dark_and_never_all_on() {
    for (uint32_t t = 0; t < 6000; t += 73) {
        const int lit = lit_count(new_effect_frame("cross-fade", t));
        TEST_ASSERT_GREATER_THAN_INT(0, lit);
        TEST_ASSERT_LESS_THAN_INT(8, lit);
    }
}

static void test_all_glow_rests_in_the_dark() {
    TEST_ASSERT_EQUAL_INT(8, lit_count(new_effect_frame("all-glow", 2500)));
    TEST_ASSERT_EQUAL_INT(0, lit_count(new_effect_frame("all-glow", 5300)));
}

static void test_random_group_lights_one_and_moves_on() {
    // ix 0 to 192 makes 2 to 5 groups, and no group may follow itself at any of them.
    for (int ix = 0; ix <= 192; ix += 64) {
        int last_first = -1;
        for (uint32_t t = 0; t < 350000; t += 350) {
            const std::array<Rgb, 8> frame = new_effect_frame("random-group", t, ix);
            TEST_ASSERT_GREATER_THAN_INT(0, lit_count(frame));
            int first = 0;
            while (frame[static_cast<size_t>(first)].r == 0) {
                ++first;
            }
            TEST_ASSERT_NOT_EQUAL_INT(last_first, first);
            last_first = first;
        }
    }
}

static void test_bubbles_never_go_dark() {
    for (uint32_t t = 0; t < 20000; t += 331) {
        TEST_ASSERT_EQUAL_INT(8, lit_count(new_effect_frame("bubble", t)));
    }
}

static void test_icicle_drop_runs_down_its_row() {
    // Some time in any few seconds every row has its drop on it, and the drop is short.
    bool seen = false;
    for (uint32_t t = 0; t < 8000; t += 37) {
        const std::array<Rgb, 8> frame = new_effect_frame("icicle", t, 96);
        const int lit = lit_count(frame);
        seen = seen || lit > 0;
        TEST_ASSERT_LESS_OR_EQUAL_INT(4, lit);
    }
    TEST_ASSERT_TRUE(seen);
}

static void test_hourglass_runs_out_and_turns_over() {
    const EffectSpec spec = parse_ok(R"({"fx":"hourglass","ix":128,"col":["FFFFFF"]})");  // 16 grains of 32 LEDs.
    Frame frame{};
    s_renderer.render(spec, 0, frame);
    TEST_ASSERT_EQUAL_UINT8(255, frame[31].r);
    TEST_ASSERT_EQUAL_UINT8(255, frame[16].r);
    TEST_ASSERT_EQUAL_UINT8(0, frame[15].r);
    s_renderer.render(spec, 16 * 260 + 700 + 100, frame);  // All landed.
    TEST_ASSERT_EQUAL_UINT8(255, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(255, frame[15].r);
    TEST_ASSERT_EQUAL_UINT8(0, frame[31].r);
    s_renderer.render(spec, 16 * 260 + 700 + 900, frame);  // Turned over: the same place, full.
    TEST_ASSERT_EQUAL_UINT8(255, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(255, frame[15].r);
    TEST_ASSERT_EQUAL_UINT8(0, frame[16].r);
}

static void test_sparkler_keeps_its_head_and_throws_sparks() {
    const EffectSpec spec = parse_ok(R"({"fx":"sparkler","ix":128,"col":["FF8000"]})");
    Frame frame{};
    int most_lit = 0;
    for (uint32_t ms = 0; ms < 8000; ms += 16) {
        s_renderer.render(spec, ms, frame);
        TEST_ASSERT_GREATER_THAN_INT(0, lit_count(frame));
        most_lit = std::max(most_lit, lit_count(frame));
    }
    TEST_ASSERT_GREATER_THAN_INT(3, most_lit);
}

static void test_water_never_goes_dark_and_moves() {
    const EffectSpec spec = parse_ok(R"({"fx":"water-wave","ix":64,"pal":[0,"001030",255,"C0F0FF"]})");
    Frame frame{};
    Frame first{};
    s_renderer.render(spec, 0, first);
    bool moved = false;
    for (uint32_t ms = 0; ms < 6000; ms += 50) {
        s_renderer.render(spec, ms, frame);
        TEST_ASSERT_EQUAL_INT(32, lit_count(frame));
        moved = moved || !(frame == first);
    }
    TEST_ASSERT_TRUE(moved);
}

static void test_comets_meet_burst_and_rest() {
    const EffectSpec spec = parse_ok(R"({"fx":"comet-collision","ix":0,"col":["FF0000","0000FF"]})");
    Frame frame{};
    s_renderer.render(spec, 0, frame);  // One at each end, in its own colour.
    TEST_ASSERT_EQUAL_UINT8(255, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(255, frame[31].b);
    const uint32_t approach_ms = 32 * 70 / 2 + 70;
    s_renderer.render(spec, approach_ms, frame);  // White flash where they met.
    const int lit_at_burst = lit_count(frame);
    TEST_ASSERT_GREATER_THAN_INT(0, lit_at_burst);
    TEST_ASSERT_LESS_THAN_INT(16, lit_at_burst);
    s_renderer.render(spec, approach_ms + 1100 + 100, frame);
    TEST_ASSERT_EQUAL_INT(0, lit_count(frame));
}

static void test_parade_shows_one_type_of_figurine() {
    const EffectSpec spec = parse_ok(R"({"fx":"figure-parade","ix":0,"col":["FFFFFF"]})");
    Frame frame{};
    s_renderer.render(spec, 900, frame);  // The first type at its peak, 4 copies in 32.
    TEST_ASSERT_EQUAL_INT(4, lit_count(frame));
    TEST_ASSERT_EQUAL_UINT8(255, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(255, frame[8].r);
    s_renderer.render(spec, 900 * 4, frame);  // The fourth.
    TEST_ASSERT_EQUAL_UINT8(255, frame[3].r);
    TEST_ASSERT_EQUAL_UINT8(0, frame[0].r);
    s_renderer.render(parse_ok(R"({"fx":"figure-parade","ix":64,"col":["FFFFFF"]})"), 900, frame);  // The rest glow.
    TEST_ASSERT_EQUAL_INT(32, lit_count(frame));
}

static void test_a_mirrored_turned_wipe_opens_from_the_middle() {
    const EffectSpec spec = parse_ok(R"({"fx":"wipe","ix":0,"mi":true,"rev":true,"col":["FF0000","0000FF"]})");
    Frame frame{};
    s_renderer.render(spec, 80 * 8, frame);  // Halfway: 8 of the 16 LEDs each way.
    TEST_ASSERT_EQUAL_UINT8(255, frame[15].r);
    TEST_ASSERT_EQUAL_UINT8(255, frame[16].r);
    TEST_ASSERT_EQUAL_UINT8(0, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(0, frame[31].r);
}

static void test_a_paired_list_not_read_is_told_from_one_not_there() {
    using A = AuthListAnswer;
    constexpr size_t kList = sizeof(AuthDevice) * kMaxAuthDevices;
    TEST_ASSERT_TRUE(auth_list_read(A::Ok, A::Ok, kList, kList) == StoredRead::Loaded);
    // None there, on a new board, or another layout's, which the browsers pair again from.
    TEST_ASSERT_TRUE(auth_list_read(A::NotFound, A::Failed, kList, kList) == StoredRead::Missing);
    TEST_ASSERT_TRUE(auth_list_read(A::Ok, A::NotFound, kList, kList) == StoredRead::Missing);
    TEST_ASSERT_TRUE(auth_list_read(A::Ok, A::TooLong, kList, kList) == StoredRead::Missing);
    TEST_ASSERT_TRUE(auth_list_read(A::Ok, A::Ok, kList - 1, kList) == StoredRead::Missing);
    // A failure says nothing of what is there, and nothing may be saved over it.
    TEST_ASSERT_TRUE(auth_list_read(A::Failed, A::Ok, kList, kList) == StoredRead::Unreadable);
    TEST_ASSERT_TRUE(auth_list_read(A::Ok, A::Failed, kList, kList) == StoredRead::Unreadable);
}

static void test_a_setting_not_read_is_told_from_none_saved() {
    using A = NvsAnswer;
    TEST_ASSERT_TRUE(stored_read(A::Ok, A::Ok) == StoredRead::Loaded);
    TEST_ASSERT_TRUE(stored_read(A::NotFound, A::Failed) == StoredRead::Missing);
    TEST_ASSERT_TRUE(stored_read(A::Ok, A::NotFound) == StoredRead::Missing);
    TEST_ASSERT_TRUE(stored_read(A::Ok, A::TooLong) == StoredRead::Missing);
    TEST_ASSERT_TRUE(stored_read(A::Failed, A::Ok) == StoredRead::Unreadable);
    TEST_ASSERT_TRUE(stored_read(A::Ok, A::Failed) == StoredRead::Unreadable);
    // The screen's settings, key by key: one key not read makes all of them so, as all are saved at once.
    TEST_ASSERT_TRUE(stored_read_with(StoredRead::Loaded, StoredRead::Missing) == StoredRead::Loaded);
    TEST_ASSERT_TRUE(stored_read_with(StoredRead::Missing, StoredRead::Missing) == StoredRead::Missing);
    TEST_ASSERT_TRUE(stored_read_with(StoredRead::Loaded, StoredRead::Unreadable) == StoredRead::Unreadable);
    TEST_ASSERT_TRUE(stored_read_with(StoredRead::Unreadable, StoredRead::Missing) == StoredRead::Unreadable);
}

static void test_a_plain_cookie_counts_only_until_https_runs() {
    TEST_ASSERT_TRUE(auth_plain_cookie_counts(false, false));
    TEST_ASSERT_TRUE(auth_plain_cookie_counts(false, true));
    TEST_ASSERT_FALSE(auth_plain_cookie_counts(true, false));
    TEST_ASSERT_TRUE(auth_plain_cookie_counts(true, true));
}

static void test_a_foreign_host_is_not_the_garland() {
    TEST_ASSERT_TRUE(auth_host_is_garland("csigarland.local", "192.168.4.1", "csigarland", "", ""));
    TEST_ASSERT_TRUE(auth_host_is_garland("192.168.4.1:80", "192.168.4.1", "csigarland", "", ""));
    TEST_ASSERT_TRUE(auth_host_is_garland("10.0.0.8", "192.168.4.1", "csigarland", "", "10.0.0.8"));
    TEST_ASSERT_TRUE(auth_host_is_garland("garland.example.com", "192.168.4.1", "csigarland", "garland.example.com", "10.0.0.8"));
    TEST_ASSERT_FALSE(auth_host_is_garland("evil.example.com", "192.168.4.1", "csigarland", "garland.example.com", "10.0.0.8"));
    TEST_ASSERT_FALSE(auth_host_is_garland("evil.example.com", "192.168.4.1", "csigarland", "", ""));
}

static void test_a_domain_recovered_by_tls_also_counts_for_http() {
    AuthDomain domain;
    const auto allowed = [&domain](const char* host) {
        return auth_host_is_garland(host, "192.168.4.1", "csigarland", domain.name, "10.0.0.8");
    };
    domain.take(StoredRead::Unreadable, "");  // The early auth read failed.
    TEST_ASSERT_FALSE(allowed("garland.example.com"));
    TEST_ASSERT_TRUE(allowed("csigarland.local"));
    TEST_ASSERT_TRUE(allowed("10.0.0.8"));

    // The TLS domain retry recovered, whether or not its separate identity read did.
    domain.take(StoredRead::Loaded, "garland.example.com");
    TEST_ASSERT_TRUE(allowed("garland.example.com:80"));
    TEST_ASSERT_TRUE(allowed("GARLAND.EXAMPLE.COM:443"));
    TEST_ASSERT_FALSE(allowed("evil.example.com"));
    domain.take(StoredRead::Unreadable, "");
    TEST_ASSERT_TRUE(allowed("garland.example.com"));

    domain.take(StoredRead::Missing, "");
    TEST_ASSERT_FALSE(allowed("garland.example.com"));
    TEST_ASSERT_TRUE(allowed("csigarland.local:80"));
    TEST_ASSERT_TRUE(allowed("10.0.0.8"));
    domain.take(StoredRead::Loaded, "garland.example.com");
    domain.take(StoredRead::Loaded, "");
    TEST_ASSERT_FALSE(allowed("garland.example.com"));
    TEST_ASSERT_TRUE(allowed("csigarland.local"));
    TEST_ASSERT_FALSE(allowed("evil.example.com"));
}

static void run_auth_tests() {
    RUN_TEST(test_a_domain_recovered_by_tls_also_counts_for_http);
    RUN_TEST(test_what_a_browser_sends_is_held_to_the_rules);
    RUN_TEST(test_certificate_dates_and_names);
    RUN_TEST(test_name_constraints_are_exact_der);
    RUN_TEST(test_an_action_arrives_whole);
    RUN_TEST(test_a_power_order_is_told_at_once);
    RUN_TEST(test_a_replaced_effect_shows_in_the_snapshot);
    RUN_TEST(test_build_up_stays_on_then_clears);
    RUN_TEST(test_cross_fade_is_never_dark_and_never_all_on);
    RUN_TEST(test_all_glow_rests_in_the_dark);
    RUN_TEST(test_random_group_lights_one_and_moves_on);
    RUN_TEST(test_bubbles_never_go_dark);
    RUN_TEST(test_icicle_drop_runs_down_its_row);
    RUN_TEST(test_hourglass_runs_out_and_turns_over);
    RUN_TEST(test_sparkler_keeps_its_head_and_throws_sparks);
    RUN_TEST(test_water_never_goes_dark_and_moves);
    RUN_TEST(test_comets_meet_burst_and_rest);
    RUN_TEST(test_parade_shows_one_type_of_figurine);
    RUN_TEST(test_a_mirrored_turned_wipe_opens_from_the_middle);
    RUN_TEST(test_pairing_needs_the_owners_answer);
    RUN_TEST(test_only_the_setup_networks_own_clients_count_as_on_it);
    RUN_TEST(test_a_page_may_send_to_the_setup_network_only_from_its_own_address);
    RUN_TEST(test_a_cookie_counts_only_from_the_portals_own_origin);
    RUN_TEST(test_an_origin_counts_only_by_the_scheme_of_the_server_it_came_to);
    RUN_TEST(test_the_name_in_a_host_header_is_read_without_its_port);
    RUN_TEST(test_the_setup_network_answers_only_to_the_portals_own_names);
    RUN_TEST(test_forgetting_everyone_ends_what_pairing_has_under_way);
    RUN_TEST(test_pairing_takes_one_request_and_expires);
    RUN_TEST(test_pairing_survives_the_clock_wrapping);
    RUN_TEST(test_refusing_holds_off_new_requests);
    RUN_TEST(test_a_token_counts_while_it_is_current);
    RUN_TEST(test_pairing_before_the_clock_starts_the_days_when_it_comes);
    RUN_TEST(test_a_new_browser_takes_free_then_expired_then_oldest);
    RUN_TEST(test_tokens_travel_as_exact_hex);
    RUN_TEST(test_a_label_is_cleaned_and_cut);
    RUN_TEST(test_the_access_screen_comes_after_an_update_only);
    RUN_TEST(test_a_paired_list_not_read_is_told_from_one_not_there);
    RUN_TEST(test_a_setting_not_read_is_told_from_none_saved);
    RUN_TEST(test_a_plain_cookie_counts_only_until_https_runs);
    RUN_TEST(test_a_foreign_host_is_not_the_garland);
}
