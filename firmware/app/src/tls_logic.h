/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Hardware-free part of the portal's certificates: names, dates and name constraints
 * Contributors: None
 *
 * The board is its own small authority: a root the owner can install once, and the portal's
 * certificate under it, which can change without troubling anyone. The root may vouch only for
 * .local names, the setup network's address and the owner's one domain, so trusting it can never
 * mean trusting it for anything else. Certificates come and go; the private keys stay for as
 * long as they are good.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "rtc_logic.h"

namespace garland {

constexpr size_t kTlsKeyBytes = 384;
constexpr size_t kTlsCertificateBytes = 1024;
constexpr size_t kTlsDomainBytes = 64;
constexpr size_t kTlsNamesBytes = 128;
constexpr size_t kTlsFingerprintBytes = 65;
constexpr size_t kTlsTimeBytes = 15;
constexpr size_t kTlsConstraintsBytes = 128;
constexpr int64_t kTlsValiditySeconds = 825LL * 86400;  // Apple's devices refuse a longer one.
constexpr int64_t kTlsRenewBeforeSeconds = 60LL * 86400;
constexpr int64_t kTlsRootValiditySeconds = 20LL * 365 * 86400;
constexpr int64_t kTlsClientClockGraceSeconds = 86400;

// Stored as a blob in encrypted NVS, so changing the layout needs a new key.
struct TlsIdentity {
    char root_key[kTlsKeyBytes];
    char root_certificate[kTlsCertificateBytes];
    char root_domain[kTlsDomainBytes];  // The one domain the root may vouch for, or empty.
    char root_fingerprint[kTlsFingerprintBytes];
    int64_t root_not_after;
    char key[kTlsKeyBytes];
    char certificate[kTlsCertificateBytes];
    char names[kTlsNamesBytes];
    int64_t not_after;
};

// What reading the identity from flash came to. A read that failed is not an identity missing: one
// made in its place would replace the root every browser and program trusts.
enum class StoredRead : uint8_t { Missing, Loaded, Unreadable };

// What NVS said to each step of reading a setting: opening the namespace, and getting a key from it.
enum class NvsAnswer : uint8_t { Ok, NotFound, TooLong, Failed };

// What the setting came to. None there, of the namespace or the key, or one too long for the place it is read
// into, which is another layout's, is Missing. Any other failure says nothing of what is there, and is
// Unreadable: one saved over it would lose it.
constexpr StoredRead stored_read(NvsAnswer opened, NvsAnswer got) {
    if (opened != NvsAnswer::Ok) {
        return opened == NvsAnswer::NotFound ? StoredRead::Missing : StoredRead::Unreadable;
    }
    return got == NvsAnswer::Ok ? StoredRead::Loaded : got == NvsAnswer::Failed ? StoredRead::Unreadable : StoredRead::Missing;
}

// One setting kept in two keys or more, read key by key: unreadable if any is, else there if any is.
constexpr StoredRead stored_read_with(StoredRead read, StoredRead next) {
    return read == StoredRead::Unreadable || next == StoredRead::Unreadable ? StoredRead::Unreadable
           : read == StoredRead::Loaded || next == StoredRead::Loaded     ? StoredRead::Loaded
                                                                           : StoredRead::Missing;
}

constexpr bool tls_may_make_identity(StoredRead read) {
    return read != StoredRead::Unreadable;
}

// Everything that decides the root, read: the identity and the owner's domain. A domain that could
// not be read is no domain chosen, and a root made for none would replace the one trusted.
constexpr bool tls_may_make_identity(StoredRead identity, StoredRead domain) {
    return tls_may_make_identity(identity) && tls_may_make_identity(domain);
}

// What reading a saved key back told: it serves, it can never serve, or the reading itself failed,
// such as for memory, which says nothing of the key.
enum class KeyCheck : uint8_t { Sound, Broken, Unknown };

// After making the certificates failed: try once more with the keys, a new portal key under the root
// kept, which every browser already trusts, start again with new keys altogether, or leave all of it as
// saved for a later start, while HTTP takes the next update.
enum class TlsRecovery : uint8_t { Retry, NewPortalKey, NewKeys, Keep };

constexpr TlsRecovery tls_recovery(KeyCheck root_key, KeyCheck portal_key) {
    if (root_key == KeyCheck::Unknown || portal_key == KeyCheck::Unknown) {
        return TlsRecovery::Keep;
    }
    if (root_key == KeyCheck::Broken) {
        return TlsRecovery::NewKeys;
    }
    return portal_key == KeyCheck::Broken ? TlsRecovery::NewPortalKey : TlsRecovery::Retry;
}

// When the certificates in use start, kept beside the identity, whose layout must stay. All zero
// for those made before this was kept, of which nothing is then assumed.
struct TlsStarts {
    int64_t root_not_before;
    int64_t not_before;
    // Real time has shown one of them to start in the future: only a clock proved right says so,
    // as a start with no clock has nothing but an older date to go by.
    uint8_t ruled_out;
};

constexpr bool tls_starts_after(const TlsStarts& starts, int64_t now_epoch) {
    return starts.root_not_before > now_epoch || starts.not_before > now_epoch;
}

// Stops using, in memory, those that start after what is now known. The mark in starts stays until
// new ones are saved: what flash holds is still the old one until then, and a power cut in between
// has to do this again, as the next start may have no clock to tell it.
inline void tls_drop_ruled_out(TlsIdentity& identity, const TlsStarts& starts, int64_t now_epoch) {
    if (starts.ruled_out == 0) {
        return;
    }
    // The root too if it must be, which browsers then have to be given again.
    if (starts.root_not_before > now_epoch) {
        identity.root_certificate[0] = '\0';
    }
    if (starts.not_before > now_epoch) {
        identity.certificate[0] = '\0';
    }
}

// "20261101120000", as X.509 writing takes it. epoch must satisfy rtc_epoch_is_valid.
inline void tls_format_time(int64_t epoch, char (&out)[kTlsTimeBytes]) {
    uint8_t registers[kRtcTimeRegisters];
    rtc_encode(epoch, registers);
    const auto two_digits = [&registers](int index) { return static_cast<unsigned>(rtc_from_bcd(registers[index])) % 100u; };
    std::snprintf(out, sizeof(out), "20%02u%02u%02u%02u%02u%02u", two_digits(6), two_digits(5), two_digits(4), two_digits(2),
                  two_digits(1), two_digits(0));
}

// A date as the compiler's __DATE__ writes it, "Nov  1 2026": the sources' date, CSIGARLAND_SOURCE_DATE,
// is the latest moment a board with no clock can vouch for.
inline int64_t tls_epoch_from_build_date(const char* date) {
    constexpr char kMonths[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char month_name[4] = {};
    int day = 0;
    int year = 0;
    if (std::sscanf(date, "%3s %d %d", month_name, &day, &year) != 3) {
        return kRtcEarliestEpoch;
    }
    const char* const found = std::strstr(kMonths, month_name);
    if (found == nullptr) {
        return kRtcEarliestEpoch;
    }
    const int month = static_cast<int>(found - kMonths) / 3 + 1;
    return rtc_days_from_date(year, month, day) * 86400;
}

inline bool tls_parse_ipv4(const char* text, uint8_t (&out)[4]) {
    unsigned parts[4] = {};
    int used = 0;
    if (std::sscanf(text, "%3u.%3u.%3u.%3u%n", &parts[0], &parts[1], &parts[2], &parts[3], &used) != 4 ||
        text[used] != '\0') {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (parts[i] > 255) {
            return false;
        }
        out[i] = static_cast<uint8_t>(parts[i]);
    }
    return true;
}

// Four numbers and nothing else, as sscanf alone would let spaces and signs through.
inline bool tls_is_ipv4_text(const char* text, uint8_t (&out)[4]) {
    return std::strspn(text, "0123456789.") == std::strlen(text) && tls_parse_ipv4(text, out);
}

// One domain, one fixed address, or nothing. A domain is lowercase labels with dots between, at
// least two of them, and no top-level domain is all digits. An address is one a client can reach.
inline bool tls_domain_is_valid(const char* text) {
    const size_t length = std::strlen(text);
    if (length == 0) {
        return true;
    }
    if (length >= kTlsDomainBytes || std::strchr(text, '.') == nullptr) {
        return false;
    }
    uint8_t address[4];
    if (tls_is_ipv4_text(text, address)) {
        return address[0] != 0 && address[0] != 127 && address[0] < 224;
    }
    char previous = '.';
    for (size_t i = 0; i < length; ++i) {
        const char c = text[i];
        const bool letter_or_digit = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!letter_or_digit && c != '-' && c != '.') {
            return false;
        }
        // No empty label, and no hyphen at either end of one.
        if ((c == '.' || c == '-') && (previous == '.' || (c == '.' && previous == '-'))) {
            return false;
        }
        previous = c;
    }
    if (previous == '.' || previous == '-') {
        return false;
    }
    for (const char* c = std::strrchr(text, '.') + 1; *c != '\0'; ++c) {
        if (*c < '0' || *c > '9') {
            return true;
        }
    }
    return false;
}

inline void tls_wanted_names(const char* hostname, const char* domain, const char* setup_address, char (&out)[kTlsNamesBytes]) {
    std::snprintf(out, sizeof(out), "%s.local,%s%s%s", hostname, domain, domain[0] != '\0' ? "," : "", setup_address);
}

inline bool tls_root_is_stale(const TlsIdentity& identity, const char* domain, int64_t now_epoch) {
    return identity.root_certificate[0] == '\0' || std::strcmp(identity.root_domain, domain) != 0 ||
           now_epoch + kTlsRenewBeforeSeconds >= identity.root_not_after;
}

inline bool tls_certificate_is_stale(const TlsIdentity& identity, const char* wanted_names, int64_t now_epoch) {
    return identity.certificate[0] == '\0' || std::strcmp(identity.names, wanted_names) != 0 ||
           now_epoch + kTlsRenewBeforeSeconds >= identity.not_after;
}

// The X.509 NameConstraints value: only .local names, the owner's domain or fixed address if
// there is one, and the setup address. Returns its length, or 0 if out is too small or the setup
// address is not one.
inline size_t tls_name_constraints_der(const char* domain, const char* setup_address, uint8_t* out, size_t out_size) {
    constexpr uint8_t kSequence = 0x30;
    constexpr uint8_t kPermitted = 0xA0;
    constexpr uint8_t kDnsName = 0x82;
    constexpr uint8_t kIpAddress = 0x87;
    constexpr char kLocal[] = "local";
    uint8_t address[4];
    if (!tls_parse_ipv4(setup_address, address) || out_size < kTlsConstraintsBytes) {
        return 0;
    }
    const size_t local_bytes = sizeof(kLocal) - 1;
    const size_t domain_bytes = std::strlen(domain);
    if (domain_bytes >= kTlsDomainBytes) {
        return 0;
    }
    uint8_t owner_address[4];
    const bool owner_is_address = tls_is_ipv4_text(domain, owner_address);
    const size_t local_subtree = 2 + local_bytes;
    const size_t address_subtree = 2 + 8;
    const size_t domain_subtree = domain_bytes == 0 ? 0 : owner_is_address ? address_subtree : 2 + domain_bytes;
    const size_t subtrees = (2 + local_subtree) + (domain_bytes != 0 ? 2 + domain_subtree : 0) + (2 + address_subtree);
    // Every length here stays under 128, the short form of DER.
    size_t at = 0;
    out[at++] = kSequence;
    out[at++] = static_cast<uint8_t>(2 + subtrees);
    out[at++] = kPermitted;
    out[at++] = static_cast<uint8_t>(subtrees);
    out[at++] = kSequence;
    out[at++] = static_cast<uint8_t>(local_subtree);
    out[at++] = kDnsName;
    out[at++] = static_cast<uint8_t>(local_bytes);
    std::memcpy(out + at, kLocal, local_bytes);
    at += local_bytes;
    if (domain_bytes != 0) {
        out[at++] = kSequence;
        out[at++] = static_cast<uint8_t>(domain_subtree);
        if (owner_is_address) {
            out[at++] = kIpAddress;
            out[at++] = 8;
            std::memcpy(out + at, owner_address, sizeof(owner_address));
            at += sizeof(owner_address);
            std::memset(out + at, 0xFF, 4);
            at += 4;
        } else {
            out[at++] = kDnsName;
            out[at++] = static_cast<uint8_t>(domain_bytes);
            std::memcpy(out + at, domain, domain_bytes);
            at += domain_bytes;
        }
    }
    out[at++] = kSequence;
    out[at++] = static_cast<uint8_t>(address_subtree);
    out[at++] = kIpAddress;
    out[at++] = 8;
    std::memcpy(out + at, address, sizeof(address));
    at += sizeof(address);
    std::memset(out + at, 0xFF, 4);
    at += 4;
    return at;
}

}  // namespace garland
