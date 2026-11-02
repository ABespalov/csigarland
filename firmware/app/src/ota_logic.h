/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Hardware-free part of the update file: its header and progress
 * Contributors: None
 *
 * An update file is this header, the signing certificate (DER), the effects pack and
 * firmware.bin. A file may hold only one of the two, and scripts/ota_sign_tool.py writes it.
 * The header gives each part's size and SHA-256. The signature is ECDSA P-256 over the
 * SHA-256 of the header up to the signature, by the certificate's key, and the firmware holds
 * the root that issued it. So a stranger's file is refused before any flash is touched.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace garland {

constexpr std::array<uint8_t, 4> kOtaMagic = {'C', 'G', 'O', 'T'};
constexpr uint32_t kOtaFormat = 3;
constexpr size_t kOtaDigestBytes = 32;
constexpr size_t kOtaSignatureBytes = 64;  // r then s, big-endian.
constexpr size_t kOtaSignedBytes = 4 + 4 + 4 + 4 + 4 + kOtaDigestBytes + kOtaDigestBytes;
constexpr size_t kOtaHeaderBytes = kOtaSignedBytes + kOtaSignatureBytes;
constexpr size_t kOtaMaxCertificateBytes = 1024;

struct OtaHeader {
    uint32_t certificate_bytes = 0;
    uint32_t pack_bytes = 0;
    uint32_t image_bytes = 0;
    std::array<uint8_t, kOtaDigestBytes> pack_digest{};
    std::array<uint8_t, kOtaDigestBytes> image_digest{};
    std::array<uint8_t, kOtaSignatureBytes> signature{};
};

constexpr uint32_t read_little_endian(const uint8_t* bytes) {
    return uint32_t{bytes[0]} | uint32_t{bytes[1]} << 8 | uint32_t{bytes[2]} << 16 | uint32_t{bytes[3]} << 24;
}

// header holds kOtaHeaderBytes. The file's length has to agree with the header, so a
// truncated upload fails here, before any flash is touched. Returns the problem as text,
// or nullptr.
inline const char* ota_parse_header(const uint8_t* header, size_t file_bytes, size_t slot_bytes, size_t pack_room,
                                    OtaHeader& out) {
    if (file_bytes < kOtaHeaderBytes || std::memcmp(header, kOtaMagic.data(), kOtaMagic.size()) != 0) {
        return "not a csiGarland update file";
    }
    if (read_little_endian(header + 4) != kOtaFormat) {
        return "update file format not supported";
    }
    out.certificate_bytes = read_little_endian(header + 8);
    out.pack_bytes = read_little_endian(header + 12);
    out.image_bytes = read_little_endian(header + 16);
    if (out.certificate_bytes == 0 || out.certificate_bytes > kOtaMaxCertificateBytes) {
        return "update file has no usable certificate";
    }
    const uint64_t expected =
        uint64_t{kOtaHeaderBytes} + out.certificate_bytes + uint64_t{out.pack_bytes} + out.image_bytes;
    if ((out.pack_bytes == 0 && out.image_bytes == 0) || expected != file_bytes) {
        return "update file is incomplete";
    }
    if (out.image_bytes > slot_bytes) {
        return "update does not fit the update slot";
    }
    if (out.pack_bytes > pack_room) {
        return "the effects pack does not fit";
    }
    std::memcpy(out.pack_digest.data(), header + 20, kOtaDigestBytes);
    std::memcpy(out.image_digest.data(), header + 20 + kOtaDigestBytes, kOtaDigestBytes);
    std::memcpy(out.signature.data(), header + kOtaSignedBytes, kOtaSignatureBytes);
    return nullptr;
}

constexpr uint8_t ota_percent(uint64_t written, uint64_t total) {
    return total == 0 ? 0 : static_cast<uint8_t>(written >= total ? 100 : written * 100 / total);
}

// What the flash says of the running image.
enum class OtaRecord : uint8_t {
    Pending,
    Valid,
    Other,
    Absent,      // No record of it at all: as a board first flashed, with no update behind it.
    Unreadable,  // Which says nothing, so it is not taken to mean out of trial.
};

constexpr bool ota_record_means_trial(OtaRecord record) {
    return record == OtaRecord::Pending || record == OtaRecord::Unreadable;
}

// The whole seconds of a trial left at now_ms, of one that began at start_ms, by a clock in ms that may
// wrap. Rounded up, so it says 0 only once the trial's time is out: the confirming then takes a moment.
constexpr uint32_t ota_trial_seconds_left(uint32_t start_ms, uint32_t now_ms, uint32_t trial_seconds) {
    const uint32_t spent = now_ms - start_ms;
    const uint64_t length = uint64_t{trial_seconds} * 1000;
    return spent >= length ? 0 : static_cast<uint32_t>((length - spent + 999) / 1000);
}

// Confirming answers OK for whichever record it finds, and with this image's record lost in a failed
// write, that is the other image's. So it counts only if the flash then says so of this one.
constexpr bool ota_confirm_held(OtaRecord record, bool boots_this_image) {
    return record == OtaRecord::Valid && boots_this_image;
}

// The status line of what ota_receive refused with, for a client to tell a file to fix from a garland
// to try again or look into. These are this firmware's own words: a test holds each of them.
inline const char* ota_problem_status(const char* problem) {
    const auto starts = [problem](const char* words) { return std::strncmp(problem, words, std::strlen(words)) == 0; };
    if (starts("the connection was lost")) {
        return "408 Request Timeout";
    }
    if (starts("the running firmware is still on trial") || starts("the garland is restarting")) {
        return "409 Conflict";
    }
    if (starts("could not") || starts("no update slot") || starts("the effects pack did not reach")) {
        return "500 Internal Server Error";
    }
    return "400 Bad Request";
}

}  // namespace garland
