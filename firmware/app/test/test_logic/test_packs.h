/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Host tests for the effects pack and its flash slots
 * Contributors: None
 */

#pragma once

static const char* shipped_pack() {
    return reinterpret_cast<const char*>(kShippedPack);
}

// A pack of these records, with a header that agrees with them.
static std::string make_pack(const std::string& records, uint32_t format = kPackFormat) {
    std::string pack(kPackHeaderBytes, '\0');
    std::memcpy(pack.data(), kPackMagic.data(), kPackMagic.size());
    pack += records;
    auto* const raw = reinterpret_cast<uint8_t*>(pack.data());
    pack_write_u32(raw + 4, format);
    pack_write_u32(raw + 8, static_cast<uint32_t>(pack.size()));
    return pack;
}

// A planned effect's text is its name here, which the pack keeps in the name's place.
static std::string pack_record(char kind, const char* id, const char* text) {
    const bool planned = kind == kPackKindPlanned;
    std::string record(1, kind);
    record += id;
    record += '\0';
    record += "Group";
    record += '\0';
    record += planned ? text : "Named";
    record += '\0';
    record += planned ? "" : text;
    record += '\0';
    return record;
}

constexpr char kGoodEffect[] = R"({"format":"csigarland.effect/1","name":"Good","layers":[{"fx":"solid"}]})";
constexpr char kUnknownAlgorithm[] = R"({"format":"csigarland.effect/1","name":"Later","layers":[{"fx":"from-the-future"}]})";

static void test_shipped_pack_is_sound_and_fits_a_slot() {
    TEST_ASSERT_NULL(pack_check(shipped_pack(), sizeof(kShippedPack)));
    TEST_ASSERT_LESS_OR_EQUAL_size_t(kPackSlotBytes - kPackSlotHeaderBytes, sizeof(kShippedPack));
    TEST_ASSERT_LESS_OR_EQUAL_size_t(kMaxPackEffects, kShippedEffectCount);
}

static void test_pack_check_refuses_what_it_should() {
    const std::string good = make_pack(pack_record(kPackKindEffect, "a", kGoodEffect));
    TEST_ASSERT_NULL(pack_check(good.data(), good.size()));

    TEST_ASSERT_NOT_NULL(pack_check(nullptr, 0));
    TEST_ASSERT_NOT_NULL(pack_check(good.data(), kPackHeaderBytes - 1));
    // Cut short: the size in the header no longer agrees.
    TEST_ASSERT_NOT_NULL(pack_check(good.data(), good.size() - 1));
    std::string bad = good;
    bad[0] = 'X';
    TEST_ASSERT_NOT_NULL(pack_check(bad.data(), bad.size()));
    // A later format, which this firmware leaves to the firmware that knows it.
    bad = make_pack(pack_record(kPackKindEffect, "a", kGoodEffect), kPackFormat + 1);
    TEST_ASSERT_NOT_NULL(pack_check(bad.data(), bad.size()));
    // The last string never ends.
    bad = make_pack(pack_record(kPackKindEffect, "a", kGoodEffect));
    bad.back() = 'x';
    TEST_ASSERT_NOT_NULL(pack_check(bad.data(), bad.size()));
    bad = make_pack("e");
    TEST_ASSERT_NOT_NULL(pack_check(bad.data(), bad.size()));
    // An unknown kind of record, nothing to play, nothing at all.
    bad = make_pack(pack_record('x', "a", kGoodEffect));
    TEST_ASSERT_NOT_NULL(pack_check(bad.data(), bad.size()));
    bad = make_pack(pack_record(kPackKindPlanned, "a", "Later"));
    TEST_ASSERT_NOT_NULL(pack_check(bad.data(), bad.size()));
    bad = make_pack("");
    TEST_ASSERT_NOT_NULL(pack_check(bad.data(), bad.size()));
}

static void test_catalog_reads_groups_favourites_and_planned_from_the_pack() {
    const std::string pack = make_pack(pack_record(kPackKindEffect, "plain", kGoodEffect) +
                                       pack_record(kPackKindFavorite, "liked", kGoodEffect) +
                                       pack_record(kPackKindPlanned, "soon", "Coming soon"));
    TEST_ASSERT_EQUAL_size_t(2, catalog_load(pack.data(), pack.size(), nullptr));
    TEST_ASSERT_EQUAL_STRING("plain", catalog_effect(0).id);
    TEST_ASSERT_EQUAL_STRING("plain", catalog_id(0).c_str());
    TEST_ASSERT_EQUAL_STRING("Named", catalog_name(0).c_str());
    // Kept as pointers, and parsed only when asked for.
    TEST_ASSERT_EQUAL_STRING("Good", catalog_effect(0).name);
    TEST_ASSERT_TRUE(catalog_id(5).empty());
    TEST_ASSERT_EQUAL_STRING("Group", catalog_group(1));
    TEST_ASSERT_FALSE(catalog_default_favorites().test(0));
    TEST_ASSERT_TRUE(catalog_default_favorites().test(1));
    TEST_ASSERT_EQUAL_size_t(1, catalog_planned_size());
    TEST_ASSERT_EQUAL_STRING("soon", catalog_planned_id(0));
    TEST_ASSERT_EQUAL_STRING("Coming soon", catalog_planned_name(0));
    load_shipped_catalog();
}

// A pack newer than the firmware that went back to: it loses those effects, not the rest.
static void test_catalog_refuses_an_unknown_algorithm_alone() {
    static int s_reports;
    s_reports = 0;
    const std::string pack = make_pack(pack_record(kPackKindEffect, "later", kUnknownAlgorithm) +
                                       pack_record(kPackKindEffect, "good", kGoodEffect) +
                                       pack_record(kPackKindEffect, "Bad Id", kGoodEffect));
    const size_t loaded = catalog_load(pack.data(), pack.size(), [](const char*, const char*) { ++s_reports; });
    TEST_ASSERT_EQUAL_size_t(1, loaded);
    TEST_ASSERT_EQUAL_STRING("good", catalog_effect(0).id);
    TEST_ASSERT_EQUAL_INT(2, s_reports);
    TEST_ASSERT_EQUAL_INT(-1, catalog_find("later"));
    load_shipped_catalog();
}

static void test_catalog_falls_back_on_one_effect_with_no_good_pack() {
    TEST_ASSERT_EQUAL_size_t(1, catalog_load(nullptr, 0, nullptr));
    TEST_ASSERT_GREATER_THAN_size_t(0, std::strlen(catalog_effect(0).id));
    TEST_ASSERT_TRUE(catalog_default_favorites().test(0));
    TEST_ASSERT_EQUAL_size_t(0, catalog_planned_size());
    // Uploads still work beside it.
    TEST_ASSERT_EQUAL_INT(1, catalog_install(uploaded_spec("mine")));

    const std::string nothing_playable = make_pack(pack_record(kPackKindEffect, "later", kUnknownAlgorithm));
    TEST_ASSERT_EQUAL_size_t(1, catalog_load(nothing_playable.data(), nothing_playable.size(), nullptr));
    TEST_ASSERT_NOT_EQUAL(0, std::strcmp("later", catalog_effect(0).id));
    load_shipped_catalog();
}

// Past what one byte could count, and what the partition table leaves room for.
static void test_the_state_reaches_every_position() {
    TEST_ASSERT_TRUE(kMaxCatalogEffects > 256);
    TEST_ASSERT_TRUE(EffectMask::kBits >= 512 + kMaxUploadedEffects + 1);
    State state(32);
    const auto last = static_cast<uint16_t>(kMaxCatalogEffects - 1);
    state.set_effect_count(static_cast<uint16_t>(kMaxCatalogEffects));
    state.set_power(true);
    state.set_brightness(100);
    state.set_speed(kManualSpeedMax);
    state.activate_effect(last);
    Snapshot now = state.snapshot();
    TEST_ASSERT_EQUAL_UINT16(last, now.effect);
    TEST_ASSERT_EQUAL_UINT8(100, now.brightness_percent);
    TEST_ASSERT_EQUAL_UINT8(kManualSpeedMax, now.speed_x4);
    TEST_ASSERT_TRUE(now.power_chosen);

    // The most the word will ever hold, and nothing spills into its neighbours.
    constexpr uint16_t kMostEffects = 1023;
    state.set_effect(kMostEffects);
    now = state.snapshot();
    TEST_ASSERT_EQUAL_UINT16(kMostEffects, now.effect);
    TEST_ASSERT_EQUAL_UINT8(100, now.brightness_percent);

    // The EFFECT button finds a favourite beyond the first byte, and comes round again.
    EffectMask favorites;
    favorites.set(3, true);
    favorites.set(last, true);
    state.set_favorites(favorites);
    state.set_effect(3);
    state.click_effect();
    TEST_ASSERT_EQUAL_UINT16(last, state.snapshot().effect);
    state.click_effect();
    TEST_ASSERT_EQUAL_UINT16(3, state.snapshot().effect);
    TEST_ASSERT_EQUAL_UINT16(last, next_favorite(300, favorites, static_cast<uint16_t>(kMaxCatalogEffects)));
}

static void test_catalog_stops_at_its_limit() {
    std::string records;
    char id[16];
    for (size_t i = 0; i < kMaxPackEffects + 3; ++i) {
        std::snprintf(id, sizeof(id), "e-%u", static_cast<unsigned>(i));
        records += pack_record(kPackKindEffect, id, kGoodEffect);
    }
    const std::string pack = make_pack(records);
    TEST_ASSERT_EQUAL_size_t(kMaxPackEffects, catalog_load(pack.data(), pack.size(), nullptr));
    TEST_ASSERT_EQUAL_size_t(kMaxCatalogEffects, catalog_positions());
    // A full pack leaves every upload and the preview their places.
    for (size_t i = 0; i < kMaxUploadedEffects; ++i) {
        std::snprintf(id, sizeof(id), "mine-%u", static_cast<unsigned>(i));
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, catalog_install(uploaded_spec(id)));
    }
    TEST_ASSERT_TRUE(catalog_is_preview(catalog_preview(uploaded_spec(kPreviewEffectId))));
    load_shipped_catalog();
}

static void test_pack_slot_record_round_trip() {
    std::array<uint8_t, kPackDigestBytes> digest{};
    digest[0] = 0xAB;
    digest[kPackDigestBytes - 1] = 0xCD;
    std::array<uint8_t, kPackSlotRecordBytes> raw{};
    pack_slot_write(raw.data(), 7, 1234, digest.data());
    PackSlot slot;
    TEST_ASSERT_TRUE(pack_slot_read(raw.data(), 0x8000, slot));
    TEST_ASSERT_EQUAL_UINT32(7, slot.sequence);
    TEST_ASSERT_EQUAL_UINT32(1234, slot.bytes);
    TEST_ASSERT_TRUE(slot.digest == digest);
    TEST_ASSERT_FALSE(slot.usable);
}

static void test_pack_slot_record_refuses_what_it_should() {
    std::array<uint8_t, kPackDigestBytes> digest{};
    std::array<uint8_t, kPackSlotRecordBytes> raw;
    PackSlot slot;
    // Erased flash.
    raw.fill(0xFF);
    TEST_ASSERT_FALSE(pack_slot_read(raw.data(), 0x8000, slot));
    // Claims more than the slot holds, by a byte.
    pack_slot_write(raw.data(), 1, 0x8000 - kPackSlotHeaderBytes + 1, digest.data());
    TEST_ASSERT_FALSE(pack_slot_read(raw.data(), 0x8000, slot));
    pack_slot_write(raw.data(), 1, 0x8000 - kPackSlotHeaderBytes, digest.data());
    TEST_ASSERT_TRUE(pack_slot_read(raw.data(), 0x8000, slot));
}

static void test_the_newest_usable_slot_is_chosen() {
    PackSlot older;
    older.usable = true;
    older.sequence = 4;
    PackSlot newer;
    newer.usable = true;
    newer.sequence = 5;
    TEST_ASSERT_EQUAL_INT(1, pack_choose_slot(older, newer));
    TEST_ASSERT_EQUAL_INT(0, pack_choose_slot(newer, older));
    // Cut short by a power loss, or of a format this firmware cannot read.
    newer.usable = false;
    TEST_ASSERT_EQUAL_INT(0, pack_choose_slot(older, newer));
    TEST_ASSERT_EQUAL_INT(1, pack_choose_slot(newer, older));
    older.usable = false;
    TEST_ASSERT_EQUAL_INT(-1, pack_choose_slot(older, newer));
}

static void run_pack_tests() {
    RUN_TEST(test_shipped_pack_is_sound_and_fits_a_slot);
    RUN_TEST(test_pack_check_refuses_what_it_should);
    RUN_TEST(test_catalog_reads_groups_favourites_and_planned_from_the_pack);
    RUN_TEST(test_catalog_refuses_an_unknown_algorithm_alone);
    RUN_TEST(test_catalog_falls_back_on_one_effect_with_no_good_pack);
    RUN_TEST(test_the_state_reaches_every_position);
    RUN_TEST(test_catalog_stops_at_its_limit);
    RUN_TEST(test_pack_slot_record_round_trip);
    RUN_TEST(test_pack_slot_record_refuses_what_it_should);
    RUN_TEST(test_the_newest_usable_slot_is_chosen);
}
