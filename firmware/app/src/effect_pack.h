/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Hardware-free part of the effects pack: its layout and its slots
 * Contributors: None
 *
 * A pack is the header (magic, format, total bytes, little-endian) and then one record after
 * another: a kind letter, then id, group, name and text, each ending in a NUL. The text is an
 * effect's minified JSON, empty for a planned one. scripts/effect_pack_tool.py writes it.
 *
 * Each of the two flash slots holds a commit record in its first sector and the pack after
 * it. The record goes in last, so a slot cut short by a power loss is never taken for good.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace garland {

constexpr std::array<uint8_t, 4> kPackMagic = {'C', 'G', 'P', 'K'};
constexpr uint32_t kPackFormat = 2;
constexpr size_t kPackHeaderBytes = 12;

constexpr char kPackKindEffect = 'e';
constexpr char kPackKindFavorite = 'f';  // An effect marked as a default favourite.
constexpr char kPackKindPlanned = 'p';

constexpr std::array<uint8_t, 4> kPackSlotMagic = {'C', 'G', 'P', 'S'};
constexpr size_t kPackDigestBytes = 32;
constexpr size_t kPackSlotRecordBytes = 4 + 4 + 4 + kPackDigestBytes;
constexpr size_t kPackSlotHeaderBytes = 4096;  // One sector: the record, then the pack.
// As partitions.csv and scripts/effect_pack_tool.py have it, which the source rules hold together.
constexpr size_t kPackSlotBytes = 0x18000;

constexpr uint32_t pack_read_u32(const uint8_t* bytes) {
    return uint32_t{bytes[0]} | uint32_t{bytes[1]} << 8 | uint32_t{bytes[2]} << 16 | uint32_t{bytes[3]} << 24;
}

constexpr void pack_write_u32(uint8_t* bytes, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        bytes[i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

struct PackRecord {
    char kind = '\0';
    const char* id = nullptr;
    const char* group = nullptr;
    const char* name = nullptr;
    const char* text = nullptr;
};

// Walks the records of a pack. The strings point into the pack, so they last as long as it.
class PackReader {
public:
    PackReader(const char* data, size_t bytes) : cursor_(data + kPackHeaderBytes), end_(data + bytes) {}

    // False at the end, and on a record cut short, which leaves damaged() true.
    bool next(PackRecord& out) {
        if (cursor_ >= end_) {
            return false;
        }
        out.kind = *cursor_++;
        out.id = take_string();
        out.group = out.id != nullptr ? take_string() : nullptr;
        out.name = out.group != nullptr ? take_string() : nullptr;
        out.text = out.name != nullptr ? take_string() : nullptr;
        damaged_ = out.text == nullptr;
        return !damaged_;
    }

    bool damaged() const { return damaged_; }

private:
    const char* take_string() {
        const auto* const terminator =
            static_cast<const char*>(std::memchr(cursor_, '\0', static_cast<size_t>(end_ - cursor_)));
        if (terminator == nullptr) {
            return nullptr;
        }
        const char* const start = cursor_;
        cursor_ = terminator + 1;
        return start;
    }

    const char* cursor_;
    const char* end_;
    bool damaged_ = false;
};

// The whole pack is sound and of a format this firmware reads. Returns the problem as text,
// or nullptr. A single effect this firmware refuses is no problem of the pack's.
inline const char* pack_check(const char* data, size_t bytes) {
    if (data == nullptr || bytes < kPackHeaderBytes || std::memcmp(data, kPackMagic.data(), kPackMagic.size()) != 0) {
        return "not an effects pack";
    }
    const auto* const raw = reinterpret_cast<const uint8_t*>(data);
    if (pack_read_u32(raw + 4) != kPackFormat) {
        return "this effects pack is for another firmware";
    }
    if (pack_read_u32(raw + 8) != bytes) {
        return "the effects pack is incomplete";
    }
    PackReader reader(data, bytes);
    PackRecord record;
    size_t effects = 0;
    while (reader.next(record)) {
        if (record.kind == kPackKindEffect || record.kind == kPackKindFavorite) {
            ++effects;
        } else if (record.kind != kPackKindPlanned) {
            return "the effects pack is damaged";
        }
    }
    if (reader.damaged()) {
        return "the effects pack is damaged";
    }
    return effects > 0 ? nullptr : "the effects pack holds no effects";
}

struct PackSlot {
    bool usable = false;
    uint32_t sequence = 0;
    uint32_t bytes = 0;
    std::array<uint8_t, kPackDigestBytes> digest{};
};

constexpr void pack_slot_write(uint8_t* raw, uint32_t sequence, uint32_t bytes, const uint8_t* digest) {
    for (size_t i = 0; i < kPackSlotMagic.size(); ++i) {
        raw[i] = kPackSlotMagic[i];
    }
    pack_write_u32(raw + 4, sequence);
    pack_write_u32(raw + 8, bytes);
    for (size_t i = 0; i < kPackDigestBytes; ++i) {
        raw[12 + i] = digest[i];
    }
}

// raw holds kPackSlotRecordBytes from the start of a slot of slot_bytes. Erased flash and a
// record that claims more than the slot holds both come back false.
inline bool pack_slot_read(const uint8_t* raw, size_t slot_bytes, PackSlot& out) {
    if (std::memcmp(raw, kPackSlotMagic.data(), kPackSlotMagic.size()) != 0 || slot_bytes < kPackSlotHeaderBytes) {
        return false;
    }
    out.sequence = pack_read_u32(raw + 4);
    out.bytes = pack_read_u32(raw + 8);
    std::memcpy(out.digest.data(), raw + 12, kPackDigestBytes);
    return out.bytes <= slot_bytes - kPackSlotHeaderBytes;
}

// The newest usable slot, or -1. Being newest is not enough: a pack this firmware cannot read,
// left by a newer one it went back from, gives way to the one before it.
constexpr int pack_choose_slot(const PackSlot& first, const PackSlot& second) {
    if (first.usable && second.usable) {
        return first.sequence >= second.sequence ? 0 : 1;
    }
    if (first.usable) {
        return 0;
    }
    return second.usable ? 1 : -1;
}

}  // namespace garland
