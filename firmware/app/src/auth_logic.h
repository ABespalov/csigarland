/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Hardware-free part of portal access: paired browsers and the pairing request
 * Contributors: None
 *
 * A browser asks for access, the garland and the browser show the same code, and the owner
 * presses POWER and SPEED together on seeing it match. The code is what stops a stranger who
 * asks at the moment the owner is pressing for their own browser. The browser then holds a
 * random token for 100 days, and the board keeps only its hash.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "tls_logic.h"

namespace garland {

constexpr int64_t kAuthValiditySeconds = 100LL * 24 * 60 * 60;
constexpr size_t kMaxAuthDevices = 8;
constexpr size_t kAuthTokenBytes = 32;
constexpr size_t kAuthHashBytes = 32;
constexpr size_t kAuthLabelBytes = 24;
constexpr size_t kPairRequestBytes = 16;
constexpr uint32_t kPairWaitMs = 60000;
constexpr uint32_t kPairRefusedMs = 60000;

// Stored as a blob in NVS, so changing the layout needs a new key. An epoch of 0 means the
// clock was not set when it was written.
struct AuthDevice {
    bool in_use;
    std::array<uint8_t, kAuthHashBytes> hash;
    int64_t paired_epoch;
    int64_t expires_epoch;
    char label[kAuthLabelBytes];
};

using AuthDevices = std::array<AuthDevice, kMaxAuthDevices>;

// With no clock nothing can be called expired; the browser's own cookie still runs out.
constexpr bool auth_device_is_current(const AuthDevice& device, int64_t now_epoch) {
    return device.in_use && (now_epoch == 0 || device.expires_epoch == 0 || now_epoch < device.expires_epoch);
}

// What a long request was let in under: a paired client's place and the hash of its token. It holds for
// as long as that client is still paired with that token and its days have not run out.
struct AuthGrant {
    int slot = -1;  // kAuthGrantSetupNetwork for the setup network, which needs no pairing.
    std::array<uint8_t, kAuthHashBytes> hash{};
};
constexpr int kAuthGrantSetupNetwork = -2;

constexpr bool auth_grant_holds(const AuthDevices& devices, const AuthGrant& grant, int64_t now_epoch) {
    return grant.slot >= 0 && static_cast<size_t>(grant.slot) < devices.size() &&
           auth_device_is_current(devices[static_cast<size_t>(grant.slot)], now_epoch) &&
           devices[static_cast<size_t>(grant.slot)].hash == grant.hash;
}

// Starts the 100 days of those paired before the clock was known. True when any changed.
inline bool auth_settle_clock(AuthDevices& devices, int64_t now_epoch) {
    if (now_epoch == 0) {
        return false;
    }
    bool changed = false;
    for (AuthDevice& device : devices) {
        if (device.in_use && device.expires_epoch == 0) {
            device.paired_epoch = now_epoch;
            device.expires_epoch = now_epoch + kAuthValiditySeconds;
            changed = true;
        }
    }
    return changed;
}

// Looks at every byte of every entry, so the time taken says nothing about a guess.
inline int auth_find(const AuthDevices& devices, const std::array<uint8_t, kAuthHashBytes>& hash, int64_t now_epoch) {
    int found = -1;
    for (size_t i = 0; i < devices.size(); ++i) {
        uint8_t difference = 0;
        for (size_t b = 0; b < hash.size(); ++b) {
            difference = static_cast<uint8_t>(difference | (devices[i].hash[b] ^ hash[b]));
        }
        if (difference == 0 && auth_device_is_current(devices[i], now_epoch)) {
            found = static_cast<int>(i);
        }
    }
    return found;
}

// A free entry, then an expired one, then the one paired longest ago.
inline size_t auth_slot_for_new(const AuthDevices& devices, int64_t now_epoch) {
    size_t oldest = 0;
    for (size_t i = 0; i < devices.size(); ++i) {
        if (!auth_device_is_current(devices[i], now_epoch)) {
            return i;
        }
        if (devices[i].paired_epoch < devices[oldest].paired_epoch) {
            oldest = i;
        }
    }
    return oldest;
}

// What a reading of the paired list from flash said, each of its two steps: the namespace opened, and
// the list got from it.
using AuthListAnswer = NvsAnswer;

// What the list came to, as stored_read says, and a list of another size, which is another layout's and the
// browsers pair again from, is Missing too. A list saved over one Unreadable would forget every browser paired.
constexpr StoredRead auth_list_read(AuthListAnswer opened, AuthListAnswer got, size_t bytes, size_t expected) {
    const StoredRead read = stored_read(opened, got);
    return read == StoredRead::Loaded && bytes != expected ? StoredRead::Missing : read;
}

constexpr int auth_hex_digit(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

// text must be exactly twice out_size lowercase hex digits.
inline bool auth_from_hex(const char* text, uint8_t* out, size_t out_size) {
    if (std::strlen(text) != out_size * 2) {
        return false;
    }
    for (size_t i = 0; i < out_size; ++i) {
        const int high = auth_hex_digit(text[i * 2]);
        const int low = auth_hex_digit(text[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        out[i] = static_cast<uint8_t>(high << 4 | low);
    }
    return true;
}

// out holds size * 2 + 1.
inline void auth_to_hex(const uint8_t* bytes, size_t size, char* out) {
    constexpr char kDigits[] = "0123456789abcdef";
    for (size_t i = 0; i < size; ++i) {
        out[i * 2] = kDigits[bytes[i] >> 4];
        out[i * 2 + 1] = kDigits[bytes[i] & 0x0F];
    }
    out[size * 2] = '\0';
}

// A name for the list, whatever a stranger sends: printable ASCII, cut to fit.
inline void auth_clean_label(const char* text, char (&out)[kAuthLabelBytes]) {
    size_t used = 0;
    for (; text[used] != '\0' && used + 1 < kAuthLabelBytes; ++used) {
        const char c = text[used];
        out[used] = c >= 0x20 && c <= 0x7E ? c : '?';
    }
    out[used] = '\0';
}

// How long the name in a Host header is, before any port. 0 if it is not a name or an IPv4 address
// with at most a port, which is all this board can be reached at, as it has no IPv6.
inline size_t auth_host_name_length(const char* host) {
    size_t length = 0;
    for (; host[length] != '\0' && host[length] != ':'; ++length) {
        const char c = host[length];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) {
            return 0;
        }
    }
    if (host[length] == ':') {
        const char* port = host + length + 1;
        if (*port == '\0') {
            return 0;
        }
        for (; *port != '\0'; ++port) {
            if (*port < '0' || *port > '9') {
                return 0;
            }
        }
    }
    return length;
}

// The setup network asks for no cookie, and its DNS answers every name with its own address, so
// any site could turn out to be the portal to a page already open. So the name asked for has to be
// the portal's: its address, or <name>.local in any case, as a browser may keep what was typed.
inline bool auth_host_is_own(const char* host, const char* address, const char* name) {
    constexpr char kSuffix[] = ".local";
    constexpr size_t kSuffixBytes = sizeof(kSuffix) - 1;
    const size_t length = auth_host_name_length(host);
    if (length == 0) {
        return false;
    }
    if (length == std::strlen(address) && std::memcmp(host, address, length) == 0) {
        return true;
    }
    const size_t name_bytes = std::strlen(name);
    if (length != name_bytes + kSuffixBytes) {
        return false;
    }
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    for (size_t i = 0; i < length; ++i) {
        const char wanted = i < name_bytes ? name[i] : kSuffix[i - name_bytes];
        if (lower(host[i]) != lower(wanted)) {
            return false;
        }
    }
    return true;
}

// Whatever a page sends that changes something, its browser names where the page came from, which
// then has to be the name asked for, by the scheme of the server it came to: secure for HTTPS. Firefox
// and Safari count http:// and https:// of one name as one site, and send a cookie of the one with a
// request of the other, so a page someone put in place of the plain HTTP one must not pass for the
// portal's own. Tools send no origin, and pass: they had the password too.
inline bool auth_origin_is_own(const char* origin, const char* host, bool secure) {
    const char* const scheme = secure ? "https://" : "http://";
    const size_t scheme_bytes = std::strlen(scheme);
    return std::strncmp(origin, scheme, scheme_bytes) == 0 && std::strcmp(origin + scheme_bytes, host) == 0;
}

// A browser sends its cookie with any request to this site, also one a page of another origin under
// the same domain makes, as SameSite tells sites apart and not origins. So a cookie counts only from
// this portal's own origin, or with none, which a browser leaves out of no request that changes
// anything. origin is nullptr when there is none.
inline bool auth_cookie_origin_ok(const char* origin, const char* host, bool secure) {
    return origin == nullptr || auth_origin_is_own(origin, host, secure);
}

// The token in an Authorization header, or nullptr. As the public API's clients send it.
inline const char* auth_bearer_token(const char* header) {
    constexpr char kScheme[] = "bearer ";
    for (size_t i = 0; kScheme[i] != '\0'; ++i) {
        const char c = header[i];
        if ((c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c) != kScheme[i]) {
            return nullptr;
        }
    }
    return header + sizeof(kScheme) - 1;
}

// Addresses as they are in memory, in network order. The address asked for is not enough: lwIP
// takes a packet for any of its addresses on any interface, so while the setup network is up a
// computer on the home network could route to its address and pass for one of its clients. The
// sender has to be on that network too, and the answer to one faking that would go there.
inline bool auth_is_setup_client(uint32_t local, uint32_t peer, uint32_t portal) {
    uint8_t peer_bytes[4];
    uint8_t portal_bytes[4];
    std::memcpy(peer_bytes, &peer, sizeof(peer_bytes));
    std::memcpy(portal_bytes, &portal, sizeof(portal_bytes));
    return local == portal && std::memcmp(peer_bytes, portal_bytes, 3) == 0;
}

// Whether the setup network's exception can hold at all: only while the setup network is up, and only
// while the home network's own address is outside its /24. A home network of 192.168.4.0/24 that gave the
// garland 192.168.4.1 would else let any computer on it pass for a client of the setup network, unpaired.
inline bool auth_setup_network_applies(bool access_point_on, uint32_t home_ip, uint32_t portal) {
    uint8_t home_bytes[4];
    uint8_t portal_bytes[4];
    std::memcpy(home_bytes, &home_ip, sizeof(home_bytes));
    std::memcpy(portal_bytes, &portal, sizeof(portal_bytes));
    return access_point_on && (home_ip == 0 || std::memcmp(home_bytes, portal_bytes, 3) != 0);
}

enum class PairPoll : uint8_t { Waiting, Approved, Gone };

// One request at a time: a request id only its browser knows, its code, and the owner's answer.
// Comparing times by difference keeps them right across the millisecond counter's wrap.
class Pairing {
public:
    // False while another is under way, or for a while after the owner refused one, so that a
    // stranger's requests cannot keep the screen from them.
    bool start(const std::array<uint8_t, kPairRequestBytes>& id, uint16_t code, uint32_t now_ms) {
        expire(now_ms);
        if (state_ != Stage::Idle || (refused_ && now_ms - refused_at_ms_ < kPairRefusedMs)) {
            return false;
        }
        refused_ = false;
        id_ = id;
        code_ = code;
        started_ms_ = now_ms;
        state_ = Stage::Waiting;
        return true;
    }

    bool waiting(uint32_t now_ms) {
        expire(now_ms);
        return state_ == Stage::Waiting;
    }

    // How long until start can succeed, for a browser to say so.
    uint32_t retry_after_ms(uint32_t now_ms) {
        expire(now_ms);
        if (state_ != Stage::Idle) {
            return kPairWaitMs - (now_ms - started_ms_);
        }
        return refused_ && now_ms - refused_at_ms_ < kPairRefusedMs ? kPairRefusedMs - (now_ms - refused_at_ms_) : 0;
    }

    uint16_t code() const { return code_; }

    bool approve(uint32_t now_ms) {
        if (!waiting(now_ms)) {
            return false;
        }
        state_ = Stage::Approved;
        return true;
    }

    void refuse(uint32_t now_ms) {
        state_ = Stage::Idle;
        refused_ = true;
        refused_at_ms_ = now_ms;
    }

    // Approved is answered once, to the browser holding the id.
    PairPoll poll(const std::array<uint8_t, kPairRequestBytes>& id, uint32_t now_ms) {
        expire(now_ms);
        uint8_t difference = 0;
        for (size_t i = 0; i < id.size(); ++i) {
            difference = static_cast<uint8_t>(difference | (id[i] ^ id_[i]));
        }
        if (state_ == Stage::Idle || difference != 0) {
            return PairPoll::Gone;
        }
        if (state_ == Stage::Approved) {
            state_ = Stage::Idle;
            return PairPoll::Approved;
        }
        return PairPoll::Waiting;
    }

    void expire(uint32_t now_ms) {
        if (state_ != Stage::Idle && now_ms - started_ms_ >= kPairWaitMs) {
            state_ = Stage::Idle;
        }
    }

    // Every browser is being forgotten: an answer not yet collected ends with them, and so does
    // one collected whose token is not yet made, which is what the generation is there to tell.
    void revoke() {
        state_ = Stage::Idle;
        ++generation_;
    }

    uint32_t generation() const { return generation_; }

private:
    enum class Stage : uint8_t { Idle, Waiting, Approved };

    Stage state_ = Stage::Idle;
    uint32_t generation_ = 0;
    std::array<uint8_t, kPairRequestBytes> id_{};
    uint16_t code_ = 0;
    uint32_t started_ms_ = 0;
    bool refused_ = false;
    uint32_t refused_at_ms_ = 0;
};

}  // namespace garland
