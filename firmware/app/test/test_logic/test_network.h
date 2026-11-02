/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Host tests for the access point policy, Wi-Fi credentials and the setup network DNS
 * Contributors: None
 *
 * Included by test_main.cpp.
 */

#pragma once

static void test_access_point_policy() {
    // Never set up: the setup network is the only way in.
    TEST_ASSERT_TRUE(access_point_should_run(false, false, 0, false));
    TEST_ASSERT_TRUE(access_point_should_run(false, false, 100000, true));
    // Just connected through it: it stays long enough to read the new address, then goes.
    TEST_ASSERT_TRUE(access_point_should_run(true, true, 0, true));
    TEST_ASSERT_TRUE(access_point_should_run(true, true, kApLingerAfterConnectS - 1, true));
    TEST_ASSERT_FALSE(access_point_should_run(true, true, kApLingerAfterConnectS, true));
    // Connected without it, as at every boot: connecting does not raise it.
    TEST_ASSERT_FALSE(access_point_should_run(true, true, 0, false));
    // A short drop does not bring it up; a long one does.
    TEST_ASSERT_FALSE(access_point_should_run(true, false, kApGraceAfterDisconnectS - 1, false));
    TEST_ASSERT_TRUE(access_point_should_run(true, false, kApGraceAfterDisconnectS, false));
    // Still connecting through it: the phone doing the setup must not lose the portal.
    TEST_ASSERT_TRUE(access_point_should_run(true, false, 0, true));
}

static void test_wifi_credentials_rules() {
    TEST_ASSERT_TRUE(wifi_credentials_are_valid(1, 0));
    TEST_ASSERT_TRUE(wifi_credentials_are_valid(32, 8));
    TEST_ASSERT_TRUE(wifi_credentials_are_valid(32, 63));
    TEST_ASSERT_FALSE(wifi_credentials_are_valid(0, 8));
    TEST_ASSERT_FALSE(wifi_credentials_are_valid(33, 8));
    TEST_ASSERT_FALSE(wifi_credentials_are_valid(4, 7));
    TEST_ASSERT_FALSE(wifi_credentials_are_valid(4, 64));
}

// A query for "a.co", type A, class IN, with one EDNS additional record that must not be echoed.
static const uint8_t kDnsQuery[] = {0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
                                    0x01, 'a',  0x02, 'c',  'o',  0x00, 0x00, 0x01, 0x00, 0x01,
                                    0x00, 0x00, 0x29, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static constexpr std::array<uint8_t, 4> kPortalAddress = {192, 168, 4, 1};

static void test_dns_answers_with_the_portal_address() {
    uint8_t reply[64];
    const size_t length = build_dns_reply(kDnsQuery, sizeof(kDnsQuery), kPortalAddress, reply, sizeof(reply));
    TEST_ASSERT_EQUAL_size_t(22 + 16, length);
    TEST_ASSERT_EQUAL_UINT8(0x12, reply[0]);
    TEST_ASSERT_EQUAL_UINT8(0x34, reply[1]);
    TEST_ASSERT_BITS_HIGH(0x80, reply[2]);
    TEST_ASSERT_EQUAL_UINT8(1, reply[7]);    // One answer.
    TEST_ASSERT_EQUAL_UINT8(0, reply[11]);   // No additional records.
    TEST_ASSERT_EQUAL_UINT8(0xC0, reply[22]);
    TEST_ASSERT_EQUAL_UINT8(0x0C, reply[23]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kPortalAddress.data(), reply + length - 4, 4);
}

static void test_dns_gives_no_answer_for_other_types() {
    uint8_t query[sizeof(kDnsQuery)];
    std::memcpy(query, kDnsQuery, sizeof(query));
    query[19] = 28;  // AAAA: an empty reply sends the client to IPv4.
    uint8_t reply[64];
    const size_t length = build_dns_reply(query, sizeof(query), kPortalAddress, reply, sizeof(reply));
    TEST_ASSERT_EQUAL_size_t(22, length);
    TEST_ASSERT_EQUAL_UINT8(0, reply[7]);
}

static void test_dns_ignores_bad_packets() {
    uint8_t reply[64];
    TEST_ASSERT_EQUAL_size_t(0, build_dns_reply(kDnsQuery, 5, kPortalAddress, reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_size_t(0, build_dns_reply(kDnsQuery, 17, kPortalAddress, reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_size_t(0, build_dns_reply(kDnsQuery, sizeof(kDnsQuery), kPortalAddress, reply, 20));

    uint8_t query[sizeof(kDnsQuery)];
    std::memcpy(query, kDnsQuery, sizeof(query));
    query[2] = 0x81;  // Already a response.
    TEST_ASSERT_EQUAL_size_t(0, build_dns_reply(query, sizeof(query), kPortalAddress, reply, sizeof(reply)));

    std::memcpy(query, kDnsQuery, sizeof(query));
    query[12] = 0xFF;  // A label longer than the packet.
    TEST_ASSERT_EQUAL_size_t(0, build_dns_reply(query, sizeof(query), kPortalAddress, reply, sizeof(reply)));
}

static void run_network_tests() {
    RUN_TEST(test_access_point_policy);
    RUN_TEST(test_wifi_credentials_rules);
    RUN_TEST(test_dns_answers_with_the_portal_address);
    RUN_TEST(test_dns_gives_no_answer_for_other_types);
    RUN_TEST(test_dns_ignores_bad_packets);
}
