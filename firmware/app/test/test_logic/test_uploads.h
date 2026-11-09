/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Host tests for effects uploaded to the catalog
 * Contributors: None
 */

#pragma once

#include <string>

#include "effect_store_keys.h"
#include "json_limits.h"

static EffectSpec uploaded_spec(const char* id, const char* name = "Mine") {
    char json[128];
    std::snprintf(json, sizeof(json), R"({"format":"csigarland.effect/1","name":"%s","layers":[{"fx":"solid"}]})", name);
    EffectSpec spec;
    const char* error = nullptr;
    TEST_ASSERT_TRUE_MESSAGE(parse_effect(json, id, spec, error), error);
    return spec;
}

static void test_uploaded_ids_are_checked() {
    TEST_ASSERT_TRUE(uploaded_id_is_valid("my-effect-2"));
    TEST_ASSERT_FALSE(uploaded_id_is_valid(""));
    TEST_ASSERT_FALSE(uploaded_id_is_valid("My-Effect"));
    TEST_ASSERT_FALSE(uploaded_id_is_valid("my effect"));
    TEST_ASSERT_FALSE(uploaded_id_is_valid("a\"b"));
    TEST_ASSERT_FALSE(uploaded_id_is_valid("0123456789012345678901234567890123"));
    TEST_ASSERT_FALSE(uploaded_id_is_valid(kPreviewEffectId));
}

static void test_an_upload_gets_a_position_that_does_not_move() {
    load_shipped_catalog();
    const size_t built_in = catalog_size();
    const int first = catalog_install(uploaded_spec("first"));
    const int second = catalog_install(uploaded_spec("second"));
    TEST_ASSERT_EQUAL_INT(built_in, first);
    TEST_ASSERT_EQUAL_INT(built_in + 1, second);
    TEST_ASSERT_EQUAL_INT(first, catalog_find("first"));
    TEST_ASSERT_TRUE(catalog_is_uploaded(first));
    TEST_ASSERT_FALSE(catalog_is_built_in(first));
    TEST_ASSERT_EQUAL_STRING("Uploaded", catalog_group(first));

    catalog_remove(first);
    TEST_ASSERT_EQUAL_INT(-1, catalog_find("first"));
    TEST_ASSERT_EQUAL_INT(second, catalog_find("second"));
    TEST_ASSERT_EQUAL_STRING("", catalog_effect(first).id);
    TEST_ASSERT_EQUAL_INT(-1, catalog_find(""));

    // The free position is used again.
    TEST_ASSERT_EQUAL_INT(first, catalog_install(uploaded_spec("third")));
    load_shipped_catalog();
}

// A replacement for a built-in effect is uploaded too, and is found with the others until restored.
static void test_a_replacement_is_in_the_uploaded_group_until_restored() {
    load_shipped_catalog();
    const std::string own_group = catalog_group(0);
    const std::string id = catalog_id(0).c_str();
    TEST_ASSERT_FALSE(own_group == "Uploaded");
    const int position = catalog_install(uploaded_spec(id.c_str()));
    TEST_ASSERT_EQUAL_INT(0, position);
    TEST_ASSERT_EQUAL_STRING("Uploaded", catalog_group(0));
    TEST_ASSERT_TRUE(catalog_is_uploaded(0));
    TEST_ASSERT_TRUE(catalog_is_built_in(0));
    // Others stay where they were.
    TEST_ASSERT_FALSE(std::string(catalog_group(1)) == "Uploaded");
    catalog_remove(0);
    TEST_ASSERT_EQUAL_STRING(own_group.c_str(), catalog_group(0));
    TEST_ASSERT_FALSE(catalog_is_uploaded(0));
    load_shipped_catalog();
}

static void test_replacing_never_touches_what_is_playing() {
    load_shipped_catalog();
    const int position = catalog_install(uploaded_spec("mine", "Old"));
    const uint32_t playing = catalog_revision(position);
    TEST_ASSERT_EQUAL_INT(position, catalog_install(uploaded_spec("mine", "New")));
    TEST_ASSERT_TRUE(catalog_revision(position) != playing);
    TEST_ASSERT_EQUAL_STRING("New", catalog_name(position).c_str());
    load_shipped_catalog();
}

static void test_two_changes_between_looks_are_still_seen() {
    load_shipped_catalog();
    // With nothing else uploaded the second replacement lands in the buffer of the first effect,
    // so its address says nothing: only the revision tells it from what was copied.
    const size_t preview = catalog_preview(uploaded_spec(kPreviewEffectId, "A"));
    const uint32_t seen = catalog_revision(preview);
    catalog_preview(uploaded_spec(kPreviewEffectId, "B"));
    catalog_preview(uploaded_spec(kPreviewEffectId, "C"));
    TEST_ASSERT_TRUE(catalog_revision(preview) != seen);
    TEST_ASSERT_EQUAL_STRING("C", catalog_effect(preview).name);

    // A copy taken earlier stays what it was while the catalog moves on.
    const CatalogText held = catalog_name(preview);
    catalog_preview(uploaded_spec(kPreviewEffectId, "D"));
    catalog_preview(uploaded_spec(kPreviewEffectId, "E"));
    TEST_ASSERT_EQUAL_STRING("C", held.c_str());
    TEST_ASSERT_EQUAL_STRING("E", catalog_name(preview).c_str());

    // Another position's change is not this one's, so what plays there does not start over.
    const uint32_t first = catalog_revision(0);
    catalog_install(uploaded_spec("other"));
    TEST_ASSERT_EQUAL_UINT32(first, catalog_revision(0));
    load_shipped_catalog();
}

static void test_ids_that_share_a_hash_keep_their_own_keys() {
    char first[kStoreKeyBytes];
    char second[kStoreKeyBytes];
    store_key_for("costarring", 0, first);
    store_key_for("liquid", 0, second);
    TEST_ASSERT_EQUAL_STRING(first, second);

    const auto all_free = [](const char*) { return true; };
    StoreKeys keys;
    TEST_ASSERT_TRUE(keys.choose("costarring", first, all_free));
    TEST_ASSERT_TRUE(keys.add("costarring", first));
    TEST_ASSERT_TRUE(keys.choose("liquid", second, all_free));
    TEST_ASSERT_TRUE(keys.add("liquid", second));
    TEST_ASSERT_TRUE(std::strcmp(first, second) != 0);
    TEST_ASSERT_EQUAL_STRING(first, keys.find("costarring"));
    TEST_ASSERT_EQUAL_STRING(second, keys.find("liquid"));

    // Read back from flash after a restart, each stays where it is, in either order.
    StoreKeys reopened;
    TEST_ASSERT_TRUE(reopened.add("liquid", second));
    TEST_ASSERT_TRUE(reopened.add("costarring", first));
    TEST_ASSERT_EQUAL_STRING(second, reopened.find("liquid"));

    // Deleting one leaves the other's key alone, and the freed key is not taken while in use.
    keys.remove("costarring");
    TEST_ASSERT_NULL(keys.find("costarring"));
    TEST_ASSERT_EQUAL_STRING(second, keys.find("liquid"));

    // A key flash holds for something unknown here is passed over, and none free is a failure.
    char other[kStoreKeyBytes];
    const auto first_taken = [&first](const char* key) { return std::strcmp(key, first) != 0; };
    StoreKeys unknown_in_flash;
    TEST_ASSERT_TRUE(unknown_in_flash.choose("costarring", other, first_taken));
    TEST_ASSERT_TRUE(std::strcmp(other, first) != 0);
    TEST_ASSERT_FALSE(unknown_in_flash.choose("costarring", other, [](const char*) { return false; }));
}

static void test_the_store_has_room_for_every_upload_beside_refused_files() {
    StoreKeys keys;
    char id[16];
    for (size_t i = 0; i < kMaxUploadedEffects + kMaxRefusedStoreKeys; ++i) {
        std::snprintf(id, sizeof(id), "mine-%u", static_cast<unsigned>(i));
        TEST_ASSERT_TRUE(keys.has_room());
        TEST_ASSERT_TRUE(keys.add(id, "e00000000"));
    }
    TEST_ASSERT_FALSE(keys.has_room());
    TEST_ASSERT_FALSE(keys.add("one-more", "e00000000"));
}

static void test_files_not_read_at_the_start_hold_back_uploads_until_read() {
    UnreadStoreKeys unread;
    TEST_ASSERT_FALSE(unread.any());
    unread.add("e00000001");
    unread.add("e00000002");
    TEST_ASSERT_TRUE(unread.any());
    // One read now, one not: the other still holds uploads under ids not known back.
    unread.read_again([](const char* key) { return std::strcmp(key, "e00000001") == 0; });
    TEST_ASSERT_TRUE(unread.any());
    unread.read_again([](const char*) { return true; });
    TEST_ASSERT_FALSE(unread.any());
    // More than it can hold: no reading again can say the rest were all read.
    UnreadStoreKeys many;
    for (size_t i = 0; i <= kMaxUnreadStoreKeys; ++i) {
        many.add("e00000003");
    }
    many.read_again([](const char*) { return true; });
    TEST_ASSERT_TRUE(many.any());
    // A list that could not be had at all, or stopped short, holds back as one not read, whatever was read.
    UnreadStoreKeys unlisted;
    unlisted.mark_index_unread();
    unlisted.read_again([](const char*) { return true; });
    TEST_ASSERT_TRUE(unlisted.index_unread());
    TEST_ASSERT_TRUE(unlisted.any());
    // Read again whole, it starts afresh.
    unlisted = UnreadStoreKeys{};
    TEST_ASSERT_FALSE(unlisted.any());
}

static void test_a_partition_not_readied_is_no_empty_store() {
    // There, but not readied, as when memory ran short: it may hold any upload, so nothing is whole yet, and
    // it is readied again.
    UnreadStoreKeys unready;
    note_store_partition(StorePartition::Unready, unready);
    TEST_ASSERT_TRUE(unready.any());
    TEST_ASSERT_TRUE(unready.index_unread());
    TEST_ASSERT_TRUE(store_partition_retried(StorePartition::Unready));
    // Missing from the partition table: nothing could be saved there, a store known empty, never retried.
    UnreadStoreKeys missing;
    note_store_partition(StorePartition::Missing, missing);
    TEST_ASSERT_FALSE(missing.any());
    TEST_ASSERT_FALSE(store_partition_retried(StorePartition::Missing));
    // Readied: what it holds is what its list says.
    UnreadStoreKeys ready;
    note_store_partition(StorePartition::Ready, ready);
    TEST_ASSERT_FALSE(ready.any());
    TEST_ASSERT_FALSE(store_partition_retried(StorePartition::Ready));
}

static void test_an_entry_known_is_passed_by_when_the_list_is_read_again() {
    StoreKeys keys;
    TEST_ASSERT_TRUE(keys.add("mine", "e00000001"));
    TEST_ASSERT_TRUE(keys.knows_key("e00000001"));
    TEST_ASSERT_FALSE(keys.knows_key("e00000002"));
    keys.remove("mine");
    TEST_ASSERT_FALSE(keys.knows_key("e00000001"));
}

static void test_a_built_in_effect_can_be_replaced_and_restored() {
    load_shipped_catalog();
    const CatalogText id = catalog_id(0);
    const uint32_t own_revision = catalog_revision(0);
    char own_name[kMaxEffectNameLength + 1];
    std::snprintf(own_name, sizeof(own_name), "%s", catalog_effect(0).name);
    TEST_ASSERT_EQUAL_INT(0, catalog_install(uploaded_spec(id.c_str(), "Mine")));
    TEST_ASSERT_EQUAL_STRING("Mine", catalog_effect(0).name);
    TEST_ASSERT_TRUE(catalog_is_uploaded(0));
    TEST_ASSERT_TRUE(catalog_is_built_in(0));
    TEST_ASSERT_EQUAL_size_t(1, catalog_uploaded_count());

    const uint32_t replaced_revision = catalog_revision(0);
    TEST_ASSERT_TRUE(replaced_revision != own_revision);
    catalog_remove(0);
    TEST_ASSERT_TRUE(catalog_revision(0) != replaced_revision);
    TEST_ASSERT_EQUAL_STRING(own_name, catalog_effect(0).name);
    TEST_ASSERT_EQUAL_STRING(own_name, catalog_name(0).c_str());
    TEST_ASSERT_FALSE(catalog_is_uploaded(0));
    TEST_ASSERT_EQUAL_size_t(0, catalog_uploaded_count());
    load_shipped_catalog();
}

static void test_uploads_stop_at_the_limit_and_replacing_still_works() {
    load_shipped_catalog();
    char id[16];
    for (size_t i = 0; i < kMaxUploadedEffects; ++i) {
        std::snprintf(id, sizeof(id), "mine-%u", static_cast<unsigned>(i));
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, catalog_install(uploaded_spec(id)));
    }
    TEST_ASSERT_FALSE(catalog_can_install("one-more"));
    TEST_ASSERT_EQUAL_INT(-1, catalog_install(uploaded_spec("one-more")));
    // Nor does replacing a built-in effect get round the limit.
    TEST_ASSERT_EQUAL_INT(-1, catalog_install(uploaded_spec(catalog_effect(0).id)));
    TEST_ASSERT_TRUE(catalog_can_install("mine-0"));
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, catalog_install(uploaded_spec("mine-0", "Again")));
    TEST_ASSERT_EQUAL_STRING("Again", catalog_effect(catalog_find("mine-0")).name);

    // With every slot full, a preview still has room, and its replacement too.
    const size_t preview = catalog_preview(uploaded_spec(kPreviewEffectId, "One"));
    TEST_ASSERT_EQUAL_STRING("One", catalog_effect(preview).name);
    catalog_preview(uploaded_spec(kPreviewEffectId, "Two"));
    TEST_ASSERT_EQUAL_STRING("Two", catalog_effect(preview).name);
    TEST_ASSERT_EQUAL_STRING("Again", catalog_effect(catalog_find("mine-0")).name);
    load_shipped_catalog();
}

static void test_json_nested_too_deep_is_refused_before_it_can_overflow_a_stack() {
    TEST_ASSERT_TRUE(json_depth_is_safe("[[[[[[[[1]]]]]]]]"));
    TEST_ASSERT_FALSE(json_depth_is_safe("[[[[[[[[[1]]]]]]]]]"));
    TEST_ASSERT_FALSE(json_depth_is_safe("{\"a\":{\"a\":{\"a\":{\"a\":{\"a\":{\"a\":{\"a\":{\"a\":{\"a\":1}}}}}}}}}"));
    // Brackets in a string are text, escaped quotes do not end one, and unclosed is still counted.
    TEST_ASSERT_TRUE(json_depth_is_safe("{\"name\":\"[[[[[[[[[[[[\"}"));
    TEST_ASSERT_TRUE(json_depth_is_safe("{\"name\":\"\\\"[[[[[[[[[[[[\"}"));
    TEST_ASSERT_FALSE(json_depth_is_safe("{\"name\":\"\\\"\",\"a\":[[[[[[[[["));
    TEST_ASSERT_TRUE(json_depth_is_safe("]]]]]]]]]]"));
    TEST_ASSERT_NULL(json_parse_shallow("[[[[[[[[[1]]]]]]]]]"));
    TEST_ASSERT_TRUE(json_values_are_bounded("{\"brightness\":25}"));
    TEST_ASSERT_TRUE(json_values_are_bounded("\"[[[{{\""));
    TEST_ASSERT_TRUE(json_values_are_bounded("[1,2,true,null,\"a\"]"));
    std::string many = "[";
    for (int i = 0; i < kMaxJsonValues; ++i) {
        if (i != 0) {
            many += ',';
        }
        many += '1';
    }
    many += ']';
    TEST_ASSERT_FALSE(json_values_are_bounded(many.c_str()));
    TEST_ASSERT_NULL(json_parse_shallow(many.c_str()));
    // What cJSON skips before and between values, a BOM and every byte up to 32, leaves no value uncounted:
    // a form feed, a vertical tab or a BOM in front of too many values is refused as they are without.
    for (const char* prefix : {"\f", "\v", "\x01", "\x1F", "\xEF\xBB\xBF", "\xEF\xBB\xBF\f "}) {
        const std::string prefixed = std::string(prefix) + "{\"entries\":" + many + "}";
        TEST_ASSERT_FALSE_MESSAGE(json_values_are_bounded(prefixed.c_str()), prefix);
        TEST_ASSERT_NULL(json_parse_shallow(prefixed.c_str()));
    }
    std::string spaced = "[";
    for (int i = 0; i < kMaxJsonValues; ++i) {
        spaced += i != 0 ? "\f,\v1" : "1";
    }
    spaced += ']';
    TEST_ASSERT_FALSE(json_values_are_bounded(spaced.c_str()));
    // Within the bound, the same prefixes and separators parse as cJSON takes them.
    for (const char* in_bound : {"\f{\"brightness\":25}", "\xEF\xBB\xBF{\"brightness\":25}", "{\x01\"a\"\x1F:\v[1\f,2]}"}) {
        TEST_ASSERT_TRUE_MESSAGE(json_values_are_bounded(in_bound), in_bound);
        cJSON* parsed = json_parse_shallow(in_bound);
        TEST_ASSERT_NOT_NULL_MESSAGE(parsed, in_bound);
        cJSON_Delete(parsed);
    }
    // A string with an escaped NUL is refused, key or value: cJSON would end it there, and "bri\u0000x" be a
    // second "bri" that a program reading the whole key takes for another. An escaped backslash before u0000 is
    // a backslash and text, and the escape of any other character is kept.
    for (const char* nul : {"{\"bri\\u0000alias\":1,\"bri\":255}", "{\"fx\":\"solid\\u0000x\"}", "[\"\\u0000\"]"}) {
        TEST_ASSERT_TRUE_MESSAGE(json_has_escaped_nul(nul), nul);
        TEST_ASSERT_NULL(json_parse_shallow(nul));
    }
    for (const char* clean : {"{\"name\":\"a\\\\u0000b\"}", "{\"name\":\"\\u0001\\u00e9\"}", "{\"u0000\":1}", "{\"name\":\"\\\"\\u0041\"}"}) {
        TEST_ASSERT_FALSE_MESSAGE(json_has_escaped_nul(clean), clean);
        cJSON* parsed = json_parse_shallow(clean);
        TEST_ASSERT_NOT_NULL_MESSAGE(parsed, clean);
        cJSON_Delete(parsed);
    }
    // What it cannot follow it refuses, rather than leave the rest uncounted: none of it is JSON cJSON takes.
    for (const char* unknown : {"+1", ".5", "{1:2}", "{\"a\" 1}", "[1 2]", "tru", "[1],[2]", "\x7F[1]", "\xC2\xA0[1]", "]"}) {
        TEST_ASSERT_FALSE_MESSAGE(json_values_are_bounded(unknown), unknown);
        TEST_ASSERT_NULL(json_parse_shallow(unknown));
    }

    // A body is one value and whitespace: no garbage after it, no second one, no NUL in it.
    cJSON* body = json_parse_body("{\"brightness\":25} \r\n", 20);
    TEST_ASSERT_NOT_NULL(body);
    cJSON_Delete(body);
    TEST_ASSERT_NULL(json_parse_body("{\"brightness\":25}garbage", 24));
    TEST_ASSERT_NULL(json_parse_body("{\"a\":1}{\"a\":2}", 14));
    TEST_ASSERT_NULL(json_parse_body("{\"a\":1}\0{\"a\":2}", 15));
    // An effect's file is read as text all the way, as its routes take it: a NUL anywhere refuses it.
    TEST_ASSERT_TRUE(body_is_text("{\"layers\":[]}", 13));
    TEST_ASSERT_FALSE(body_is_text("{\"layers\":[]}\0garbage", 21));
    TEST_ASSERT_FALSE(body_is_text("\0", 1));

    std::string deep;
    deep.append(4000, '[');
    EffectSpec spec;
    const char* error = nullptr;
    TEST_ASSERT_FALSE(parse_effect(deep.c_str(), "deep", spec, error));
    TEST_ASSERT_EQUAL_STRING("not valid JSON", error);

    // The deepest thing a real effect holds: a colour in a gradient in a layer.
    TEST_ASSERT_TRUE_MESSAGE(parse_effect(
        R"({"format":"csigarland.effect/1","name":"Deep","layers":[{"fx":"solid","zone":{"every":2,"offset":0},"pal":[0,[1,2,3],255,[4,5,6]]}]})",
        "deep", spec, error), error);
}

static void test_a_preview_is_not_an_effect_of_the_catalog() {
    load_shipped_catalog();
    const size_t preview = catalog_preview(uploaded_spec(kPreviewEffectId));
    TEST_ASSERT_EQUAL_size_t(catalog_positions() - 1, preview);
    TEST_ASSERT_TRUE(catalog_is_preview(preview));
    TEST_ASSERT_FALSE(catalog_is_uploaded(preview));
    TEST_ASSERT_EQUAL_INT(-1, catalog_find(kPreviewEffectId));
    TEST_ASSERT_EQUAL_size_t(0, catalog_uploaded_count());
    load_shipped_catalog();
}

static void test_favourites_follow_uploads_by_id() {
    load_shipped_catalog();
    const int position = catalog_install(uploaded_spec("mine"));
    EffectMask mask;
    mask.set(0, true);
    mask.set(static_cast<size_t>(position), true);
    char text[kFavoritesTextBytes];
    TEST_ASSERT_TRUE(favorites_to_text(mask, text, sizeof(text)));
    const EffectMask read_back = favorites_from_text(text);
    TEST_ASSERT_TRUE(read_back.test(0));
    TEST_ASSERT_TRUE(read_back.test(static_cast<size_t>(position)));
    TEST_ASSERT_FALSE(favorites_name_unknown(text));

    // Gone from the catalog, it is simply no favourite any more, and the list is told to name one it lacks,
    // which, while uploaded effects are not all read, may be one of them: then no save drops its mark.
    catalog_remove(position);
    TEST_ASSERT_FALSE(favorites_from_text(text).test(static_cast<size_t>(position)));
    TEST_ASSERT_TRUE(favorites_name_unknown(text));
    // Back, as when its file is read late, it is a favourite again from the same text.
    const int back = catalog_install(uploaded_spec("mine"));
    TEST_ASSERT_TRUE(favorites_from_text(text).test(static_cast<size_t>(back)));
    TEST_ASSERT_FALSE(favorites_name_unknown(text));
    load_shipped_catalog();
}

static void run_upload_tests() {
    RUN_TEST(test_uploaded_ids_are_checked);
    RUN_TEST(test_an_upload_gets_a_position_that_does_not_move);
    RUN_TEST(test_a_replacement_is_in_the_uploaded_group_until_restored);
    RUN_TEST(test_replacing_never_touches_what_is_playing);
    RUN_TEST(test_two_changes_between_looks_are_still_seen);
    RUN_TEST(test_ids_that_share_a_hash_keep_their_own_keys);
    RUN_TEST(test_the_store_has_room_for_every_upload_beside_refused_files);
    RUN_TEST(test_files_not_read_at_the_start_hold_back_uploads_until_read);
    RUN_TEST(test_a_partition_not_readied_is_no_empty_store);
    RUN_TEST(test_an_entry_known_is_passed_by_when_the_list_is_read_again);
    RUN_TEST(test_a_built_in_effect_can_be_replaced_and_restored);
    RUN_TEST(test_uploads_stop_at_the_limit_and_replacing_still_works);
    RUN_TEST(test_json_nested_too_deep_is_refused_before_it_can_overflow_a_stack);
    RUN_TEST(test_a_preview_is_not_an_effect_of_the_catalog);
    RUN_TEST(test_favourites_follow_uploads_by_id);
}
