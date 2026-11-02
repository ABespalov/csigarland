/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: MOSFET power gate and WS2812B output with safe power sequencing
 * Contributors: None
 */

#include "garland_output.h"

#include <algorithm>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip_rmt.h"

namespace garland {
namespace {

const char* const TAG = "output";

constexpr uint32_t kRailSettleMs = 10;
constexpr uint32_t kRailDrainMs = 200;  // For the LED boards' capacitors, by estimate: see the to-do list.
constexpr uint32_t kLatchMs = 2;
constexpr uint32_t kRmtResolutionHz = 10'000'000;
// Two RMT memory blocks: the refill interrupt can arrive later before the
// WS2812B timing breaks, while the display SPI is busy.
constexpr size_t kRmtMemBlockSymbols = 96;

void configure_output_low(gpio_num_t pin) {
    gpio_set_level(pin, 0);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
}

// For what goes on regardless: blanking and letting go, on the way to the rail going off.
void log_failure(esp_err_t err, const char* what) {
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s failed: %s", what, esp_err_to_name(err));
    }
}

}  // namespace

void GarlandOutput::blank_at_boot() {
    gpio_reset_pin(gate_pin_);
    gpio_reset_pin(data_pin_);
    configure_output_low(gate_pin_);
    configure_output_low(data_pin_);
    if (!power_on()) {
        return;
    }
    // Taking over the pin can look like a stray bit to the first LED, which would
    // shift one frame. The second frame, sent by power_off(), starts clean.
    log_failure(led_strip_clear(strip_), "blanking");
    vTaskDelay(pdMS_TO_TICKS(kLatchMs));
    power_off();
    ESP_LOGI(TAG, "blanked up to %lu LEDs", static_cast<unsigned long>(led_count_));
}

bool GarlandOutput::power_on() {
    if (strip_ != nullptr) {
        return true;
    }
    configure_output_low(data_pin_);
    gpio_set_level(gate_pin_, 1);
    vTaskDelay(pdMS_TO_TICKS(kRailSettleMs));

    if (!create_strip()) {
        gpio_set_level(gate_pin_, 0);
        return false;
    }
    ESP_LOGI(TAG, "rail on");
    return true;
}

bool GarlandOutput::create_strip() {
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = data_pin_;
    strip_config.max_leds = led_count_;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = kRmtResolutionHz;
    rmt_config.mem_block_symbols = kRmtMemBlockSymbols;

    const esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &strip_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LED output failed: %s", esp_err_to_name(err));
        strip_ = nullptr;
        return false;
    }
    return true;
}

bool GarlandOutput::set_led_count(uint32_t led_count) {
    if (led_count == led_count_) {
        refused_count_ = 0;
        return true;
    }
    if (strip_ == nullptr) {
        led_count_ = led_count;
        refused_count_ = 0;
        return true;
    }
    if (led_count == refused_count_) {
        return true;
    }
    const uint32_t previous = led_count_;
    const bool blanked = led_strip_clear(strip_) == ESP_OK;
    led_count_ = led_count;
    remove_strip();
    // The freed RMT channel leaves the pin undriven until the new one takes it.
    configure_output_low(data_pin_);
    if (!blanked && led_count < previous) {
        ESP_LOGW(TAG, "the LEDs past %lu may still be lit: cutting the rail", static_cast<unsigned long>(led_count));
        gpio_set_level(gate_pin_, 0);
        vTaskDelay(pdMS_TO_TICKS(kRailDrainMs));
        gpio_set_level(gate_pin_, 1);
        vTaskDelay(pdMS_TO_TICKS(kRailSettleMs));
    }
    if (create_strip()) {
        refused_count_ = 0;
        return true;
    }
    ESP_LOGW(TAG, "keeping %lu LEDs", static_cast<unsigned long>(previous));
    refused_count_ = led_count;
    led_count_ = previous;
    if (create_strip()) {
        return true;
    }
    gpio_set_level(gate_pin_, 0);
    ESP_LOGE(TAG, "no LED output left, rail off");
    return false;
}

void GarlandOutput::remove_strip() {
    if (strip_ != nullptr) {
        log_failure(led_strip_del(strip_), "removing the output");
        strip_ = nullptr;
    }
}

void GarlandOutput::power_off() {
    if (strip_ != nullptr) {
        log_failure(led_strip_clear(strip_), "blanking");
        remove_strip();
    }
    configure_output_low(data_pin_);
    gpio_set_level(gate_pin_, 0);
    ESP_LOGI(TAG, "rail off");
}

void GarlandOutput::release_data_pin() {
    remove_strip();
}

bool GarlandOutput::reclaim_data_pin() {
    return create_strip();
}

bool GarlandOutput::restart() {
    remove_strip();
    configure_output_low(data_pin_);
    return create_strip();
}

bool GarlandOutput::show(std::span<const Rgb> frame) {
    return strip_ != nullptr && write_frame(frame);
}

bool GarlandOutput::write_frame(std::span<const Rgb> frame) {
    uint32_t index = 0;
    esp_err_t err = ESP_OK;
    // A chain kept at the previous length is shorter than the frame.
    for (const Rgb& led : frame.first(std::min<size_t>(frame.size(), led_count_))) {
        const esp_err_t pixel = led_strip_set_pixel(strip_, index++, led.r, led.g, led.b);
        err = err == ESP_OK ? pixel : err;
    }
    return err == ESP_OK && led_strip_refresh(strip_) == ESP_OK;
}

}  // namespace garland
