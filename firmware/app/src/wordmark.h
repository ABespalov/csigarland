/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The csiGarland wordmark in the csiPass 5x7 pixel face
 * Contributors: None
 */

#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace garland {

// Whole screen pixels per font cell. Scale 4 would not fit the panel window.
constexpr int kWordmarkScale = 3;

constexpr int kWordmarkChars = 10;
constexpr int kWordmarkWhiteChars = 3;  // "csi" white, "Garland" blue.
constexpr int kWordmarkGlyphColumns = 5;
constexpr int kWordmarkGlyphRows = 7;
constexpr int kWordmarkPitchColumns = 6;

constexpr int kWordmarkWidth = (kWordmarkChars * kWordmarkPitchColumns - 1) * kWordmarkScale;
constexpr int kWordmarkHeight = kWordmarkGlyphRows * kWordmarkScale;
constexpr int kWordmarkPixels = kWordmarkWidth * kWordmarkHeight;

// Same glyphs as the enclosure lid (3d-model/control_enclosure.scad), the portal, where
// scripts/portal_pack.py compares them, and the utility, where a test does: five column bytes
// per letter, bit 0 is the top row.
constexpr std::array<std::array<uint8_t, kWordmarkGlyphColumns>, kWordmarkChars> kWordmarkGlyphs = {{
    {56, 68, 68, 68, 32},    // c
    {72, 84, 84, 84, 32},    // s
    {0, 68, 125, 64, 0},     // i
    {62, 65, 73, 73, 122},   // G
    {32, 84, 84, 84, 120},   // a
    {124, 8, 4, 4, 8},       // r
    {0, 65, 127, 64, 0},     // l
    {32, 84, 84, 84, 120},   // a
    {124, 8, 4, 4, 120},     // n
    {56, 68, 68, 72, 127},   // d
}};

// Fills an RGB565 image of kWordmarkWidth x kWordmarkHeight.
inline void render_wordmark(std::span<uint16_t, kWordmarkPixels> pixels, uint16_t background,
                            uint16_t white, uint16_t blue) {
    for (uint16_t& pixel : pixels) {
        pixel = background;
    }
    for (int letter = 0; letter < kWordmarkChars; ++letter) {
        const uint16_t ink = letter < kWordmarkWhiteChars ? white : blue;
        for (int column = 0; column < kWordmarkGlyphColumns; ++column) {
            for (int row = 0; row < kWordmarkGlyphRows; ++row) {
                if ((kWordmarkGlyphs[letter][column] & (1u << row)) == 0) {
                    continue;
                }
                const int left = (letter * kWordmarkPitchColumns + column) * kWordmarkScale;
                const int top = row * kWordmarkScale;
                for (int y = top; y < top + kWordmarkScale; ++y) {
                    for (int x = left; x < left + kWordmarkScale; ++x) {
                        pixels[y * kWordmarkWidth + x] = ink;
                    }
                }
            }
        }
    }
}

}  // namespace garland
