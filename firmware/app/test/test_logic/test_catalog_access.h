/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: How the host tests reach the shipped effects
 * Contributors: None
 */

#pragma once

// A parsed copy, of the few that a test holds on to at a time.
static const EffectSpec& catalog_effect(size_t index) {
    static std::array<EffectSpec, 8> s_copies;
    static size_t s_next = 0;
    EffectSpec& copy = s_copies[s_next++ % s_copies.size()];
    catalog_copy_spec(index, copy);
    return copy;
}

static size_t load_shipped_catalog(CatalogErrorHandler on_error = nullptr) {
    return catalog_load(reinterpret_cast<const char*>(kShippedPack), sizeof(kShippedPack), on_error);
}
