/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: MOSFET power gate and WS2812B output with safe power sequencing
 * Contributors: None
 */

#pragma once

#include <span>

#include "driver/gpio.h"
#include "effects.h"
#include "led_strip.h"

namespace garland {

class GarlandOutput {
public:
    GarlandOutput(gpio_num_t data_pin, gpio_num_t gate_pin, uint32_t led_count)
        : data_pin_(data_pin), gate_pin_(gate_pin), led_count_(led_count) {}

    // Run once at boot, before anything else: blanks the chain, then leaves the rail
    // off and the data pin low. LEDs on an ungated rail keep their last colours
    // through a reset, so opening the gate alone would not put them out. The frames
    // go out on a live rail to avoid feeding an unpowered chain through its data pin.
    void blank_at_boot();

    // specification.md 3.3: data low, gate high, 10 ms, then RMT. Returns false
    // with the rail off if the output could not be started.
    bool power_on();

    // specification.md 3.3: blackout frame, data low, gate low.
    void power_off();

    // Changes the chain length, on a live rail too. LEDs past a shorter length are
    // blanked first, because nothing will address them afterwards, and if that frame does not
    // go out the rail is cut for a moment, as they would stay lit and uncounted. A length the output
    // cannot be started for is not tried again, and the previous one stays. Returns false
    // with the rail off if no output could be started at all.
    bool set_led_count(uint32_t led_count);

    // The length asked for was refused, so the chain's far end is getting nothing.
    bool count_refused() const { return refused_count_ != 0; }

    // The length frames go out at, which after a refusal is not the one asked for.
    uint32_t led_count() const { return led_count_; }

    // False if the frame did not go out.
    bool show(std::span<const Rgb> frame);

    // Starts the output again, for one that has stopped sending. The LEDs hold their last
    // frame meanwhile. False if it could not.
    bool restart();

    // Lend the data pin for a moment, on a live rail too. The LEDs hold their last frame
    // meanwhile. Returns false if the output could not be started again.
    void release_data_pin();
    bool reclaim_data_pin();

private:
    bool create_strip();
    void remove_strip();
    bool write_frame(std::span<const Rgb> frame);

    gpio_num_t data_pin_;
    gpio_num_t gate_pin_;
    uint32_t led_count_;
    uint32_t refused_count_ = 0;
    led_strip_handle_t strip_ = nullptr;
};

}  // namespace garland
