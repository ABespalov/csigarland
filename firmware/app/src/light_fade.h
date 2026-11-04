/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Hardware-free fades: the light rising and falling at a turn on or off, and one effect into the next
 * Contributors: None
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "effects.h"

namespace garland {

// The whole way, dark to lit or back. A fade begun part of the way takes its share of it.
constexpr uint32_t kFadeOnMs = 800;
constexpr uint32_t kFadeOffMs = 800;
// A scheduled turn on that rises: the morning's slow light, over a minute.
constexpr uint32_t kRiseMs = 60'000;

// From one effect to the next, the last frame of the old one giving way to the new. 0 cuts.
constexpr uint32_t kCrossFadeMaxMs = 5000;
constexpr uint32_t kCrossFadeDefaultMs = 1000;

constexpr bool cross_fade_ms_is_valid(int ms) {
    return ms >= 0 && ms <= static_cast<int>(kCrossFadeMaxMs);
}

// How lit the garland is on its way between dark and its brightness, by time. Linear in its level,
// and squared into light, so that the eye sees it go evenly: the dim end is where it would jump.
class LightFade {
public:
    static constexpr uint32_t kFull = 65535;

    uint32_t level(int64_t now_us) const {
        if (now_us >= end_us_ || end_us_ == start_us_) {
            return to_;
        }
        const int64_t gone = std::max<int64_t>(now_us - start_us_, 0);
        const int64_t span = end_us_ - start_us_;
        const int64_t delta = static_cast<int64_t>(to_) - static_cast<int64_t>(from_);
        return static_cast<uint32_t>(static_cast<int64_t>(from_) + delta * gone / span);
    }

    // Toward to from wherever it is now, at the pace of full_ms for the whole way.
    void go(uint32_t to, uint32_t full_ms, int64_t now_us) {
        const uint32_t from = level(now_us);
        const uint32_t way = from > to ? from - to : to - from;
        from_ = from;
        to_ = to;
        start_us_ = now_us;
        end_us_ = now_us + static_cast<int64_t>(full_ms) * 1000 * way / kFull;
    }

    // At once, as a turn off that cannot wait.
    void set(uint32_t level) {
        from_ = to_ = level;
        start_us_ = end_us_ = 0;
    }

    uint32_t target() const { return to_; }
    bool dark(int64_t now_us) const { return to_ == 0 && level(now_us) == 0; }

    // The owner's brightness gain, as far as the fade has come.
    uint32_t apply(uint32_t gain, int64_t now_us) const {
        const uint64_t level_now = this->level(now_us);
        const uint64_t light = level_now * level_now / kFull;
        return static_cast<uint32_t>(uint64_t{gain} * light / kFull);
    }

private:
    uint32_t from_ = 0;
    uint32_t to_ = 0;
    int64_t start_us_ = 0;
    int64_t end_us_ = 0;
};

// The old effect's last frame and the new one's live frame, mixed by weight: 0 is all old, 65536 all
// new. A chain longer now than the old frame shows the new one where the old had nothing.
inline void cross_fade(std::span<const Rgb> from, std::span<Rgb> frame, uint32_t weight) {
    weight = std::min<uint32_t>(weight, 65536);
    const size_t shared = std::min(from.size(), frame.size());
    for (size_t i = 0; i < shared; ++i) {
        const auto mix = [weight](uint8_t old_value, uint8_t new_value) {
            return static_cast<uint8_t>((old_value * (65536 - weight) + new_value * weight + 32768) >> 16);
        };
        frame[i] = Rgb{mix(from[i].r, frame[i].r), mix(from[i].g, frame[i].g), mix(from[i].b, frame[i].b)};
    }
}

// How far a cross-fade of span_ms has come after elapsed_ms, as cross_fade's weight.
// Whether the lighting task renders a frame now: every period, and at once when the chain on the wire is
// no longer the length the last frame was rendered and held to the current limit for. Sent before that,
// a longer chain would go out with a tail from an older frame, which the limit never saw whole.
constexpr bool frame_render_due(int64_t now_us, int64_t last_frame_us, int64_t period_us, size_t led_count, size_t limited_count) {
    return now_us - last_frame_us >= period_us || led_count != limited_count;
}

constexpr uint32_t cross_fade_weight(uint32_t elapsed_ms, uint32_t span_ms) {
    if (span_ms == 0 || elapsed_ms >= span_ms) {
        return 65536;
    }
    return static_cast<uint32_t>(uint64_t{elapsed_ms} * 65536 / span_ms);
}

}  // namespace garland
