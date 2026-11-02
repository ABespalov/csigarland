/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Hardware-free network decisions: when the access point runs, DNS replies
 * Contributors: None
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace garland {

// The name before .local, a single DNS label kept to what any resolver takes.
constexpr size_t kMaxHostnameLength = 32;

constexpr bool hostname_is_valid(const char* name) {
    size_t length = 0;
    for (; name[length] != '\0'; ++length) {
        const char c = name[length];
        const bool letter_or_digit = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (length >= kMaxHostnameLength || !(letter_or_digit || c == '-')) {
            return false;
        }
    }
    return length > 0 && name[0] != '-' && name[length - 1] != '-';
}

// Keeps the portal reachable while the owner reads the new address.
constexpr uint32_t kApLingerAfterConnectS = 60;
// Long enough for an ordinary reconnect not to bring the setup network up.
constexpr uint32_t kApGraceAfterDisconnectS = 20;

// The setup network runs whenever the home network is not there to reach the portal on.
// Connecting only delays stopping one that is up: raising it for that minute would share
// the radio for nothing, and it slows the portal on the home network as well.
constexpr bool access_point_should_run(bool has_credentials, bool connected, uint32_t seconds_in_this_state,
                                       bool running) {
    if (!has_credentials) {
        return true;
    }
    return connected ? running && seconds_in_this_state < kApLingerAfterConnectS
                     : running || seconds_in_this_state >= kApGraceAfterDisconnectS;
}

constexpr bool wifi_credentials_are_valid(size_t ssid_length, size_t password_length) {
    return ssid_length >= 1 && ssid_length <= 32 &&
           (password_length == 0 || (password_length >= 8 && password_length <= 63));
}

constexpr size_t kMaxScanEntries = 16;

struct ScanEntry {
    char ssid[33];
    int8_t rssi;
    bool open;
};

enum class ScanStep : uint8_t { Accept, Retry, Fail };

// What to do when the driver reports a scan finished. A driver that cuts a scan short
// (status false) or finds nothing may just have been busy, so an empty answer gets
// another try before the owner is told there are no networks.
constexpr ScanStep scan_step_after_done(bool status_ok, uint16_t found, int attempts_made) {
    constexpr int kAttemptsAllowed = 3;
    if (status_ok && found > 0) {
        return ScanStep::Accept;
    }
    if (attempts_made < kAttemptsAllowed) {
        return ScanStep::Retry;
    }
    return status_ok ? ScanStep::Accept : ScanStep::Fail;
}

// One entry per name, the strongest first, without hidden networks, which have no name
// to choose. An access point with many radios would otherwise fill the short list.
// Returns the new count.
inline size_t tidy_scan_entries(ScanEntry* entries, size_t count) {
    size_t kept = 0;
    for (size_t i = 0; i < count; ++i) {
        if (entries[i].ssid[0] == '\0') {
            continue;
        }
        size_t same = 0;
        while (same < kept && std::strcmp(entries[same].ssid, entries[i].ssid) != 0) {
            ++same;
        }
        if (same == kept) {
            entries[kept++] = entries[i];
        } else if (entries[i].rssi > entries[same].rssi) {
            entries[same] = entries[i];
        }
    }
    std::sort(entries, entries + kept, [](const ScanEntry& a, const ScanEntry& b) { return a.rssi > b.rssi; });
    return kept;
}

constexpr char kApSsid[] = "csiGarland-Setup";

// The setup network's password is the board's own, read off its screen and typed on a
// phone, so the alphabet leaves out what looks alike: 0/o, 1/l/i. About 59 bits.
constexpr char kApPasswordAlphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";
constexpr size_t kApPasswordAlphabetSize = sizeof(kApPasswordAlphabet) - 1;
constexpr size_t kApPasswordLength = 12;

inline bool ap_password_is_valid(const char* text) {
    return std::strlen(text) == kApPasswordLength && std::strspn(text, kApPasswordAlphabet) == kApPasswordLength;
}

// Fills out, of kApPasswordLength + 1 bytes, from random_byte(). Bytes past the last
// whole multiple of the alphabet are skipped, or the first letters would come up more
// often than the rest.
template <typename RandomByte>
void make_ap_password(char* out, RandomByte random_byte) {
    const size_t usable = 256 - 256 % kApPasswordAlphabetSize;
    size_t written = 0;
    while (written < kApPasswordLength) {
        const uint8_t value = random_byte();
        if (value < usable) {
            out[written++] = kApPasswordAlphabet[value % kApPasswordAlphabetSize];
        }
    }
    out[written] = '\0';
}

// Builds the captive portal's DNS answer: every A question gets the portal's address.
// Returns the reply length, or 0 for anything that should go unanswered.
inline size_t build_dns_reply(const uint8_t* query, size_t query_length, const std::array<uint8_t, 4>& address,
                              uint8_t* reply, size_t reply_capacity) {
    constexpr size_t kHeaderBytes = 12;
    constexpr size_t kAnswerBytes = 16;
    if (query_length < kHeaderBytes + 5) {
        return 0;
    }
    const bool is_response = (query[2] & 0x80) != 0;
    const bool is_standard_query = (query[2] & 0x78) == 0;
    const uint16_t questions = static_cast<uint16_t>((query[4] << 8) | query[5]);
    if (is_response || !is_standard_query || questions != 1) {
        return 0;
    }

    size_t position = kHeaderBytes;
    while (position < query_length && query[position] != 0) {
        const uint8_t label = query[position];
        if ((label & 0xC0) != 0) {
            return 0;  // A compressed name has no place in a question.
        }
        position += size_t{label} + 1;
    }
    if (position + 5 > query_length) {
        return 0;
    }
    const size_t question_end = position + 5;
    const bool asks_for_a = query[position + 1] == 0 && query[position + 2] == 1;
    const size_t reply_length = question_end + (asks_for_a ? kAnswerBytes : 0);
    if (reply_length > reply_capacity) {
        return 0;
    }

    std::memcpy(reply, query, question_end);
    reply[2] = static_cast<uint8_t>(0x84 | (query[2] & 0x01));  // Response, authoritative.
    reply[3] = 0x00;                                            // No error.
    reply[6] = 0;
    reply[7] = asks_for_a ? 1 : 0;
    std::memset(reply + 8, 0, 4);  // Additional records of the query are not echoed.
    if (asks_for_a) {
        const uint8_t answer[kAnswerBytes] = {0xC0, 0x0C,  // Name: the question's.
                                              0x00, 0x01, 0x00, 0x01,  // Type A, class IN.
                                              0x00, 0x00, 0x00, 0x3C,  // TTL 60 s.
                                              0x00, 0x04, address[0], address[1], address[2], address[3]};
        std::memcpy(reply + question_end, answer, kAnswerBytes);
    }
    return reply_length;
}

// A client cannot read the saved password back, so an empty one for the network already saved means
// keep it. A network that has become open is forgotten first.
inline bool wifi_keeps_saved_password(const char* ssid, const char* password, const char* saved_ssid) {
    return password[0] == '\0' && saved_ssid[0] != '\0' && std::strcmp(ssid, saved_ssid) == 0;
}

}  // namespace garland
