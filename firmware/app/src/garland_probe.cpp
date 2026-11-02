/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Detect a garland on the data pin while the LED rail is off
 * Contributors: None
 */

#include "garland_probe.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "garland_probe_math.h"

namespace garland {
namespace {

const char* const TAG = "probe";

constexpr uint32_t kRailSettleMs = 500;
constexpr uint32_t kFirstReadDelayUs = 200;
constexpr uint32_t kRiseToMeasureMv = 300;
constexpr int64_t kRiseTimeoutUs = 200'000;
constexpr int kBusyPolls = 50;
constexpr int kSamplesPerRead = 4;
constexpr int kAdcFullScaleRaw = 4095;

uint32_t read_mv(adc_oneshot_unit_handle_t adc, adc_channel_t channel) {
    int total = 0;
    for (int i = 0; i < kSamplesPerRead; ++i) {
        int raw = 0;
        if (adc_oneshot_read(adc, channel, &raw) != ESP_OK) {
            return kProbePullupMv;
        }
        total += raw;
    }
    return static_cast<uint32_t>(total / kSamplesPerRead) * kProbePullupMv / kAdcFullScaleRaw;
}

void drive_pin_low(gpio_num_t pin) {
    gpio_pullup_dis(pin);
    gpio_set_level(pin, 0);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
}

ProbeResult measure(adc_oneshot_unit_handle_t adc, adc_channel_t channel, gpio_num_t pin) {
    // Weak on purpose: at most about 70 uA can reach the unpowered rail.
    gpio_pullup_en(pin);
    esp_rom_delay_us(kFirstReadDelayUs);

    const uint32_t start_mv = read_mv(adc, channel);
    const int64_t start_us = esp_timer_get_time();
    if (!probe_says_present(start_mv)) {
        ESP_LOGI(TAG, "no garland: pin rose to %lu mV", static_cast<unsigned long>(start_mv));
        return ProbeResult{ProbeStatus::Absent, 0};
    }

    // One LED board crosses the window in under a millisecond and a full garland
    // in tens of them. Busy polls catch the first; the later polls sleep, so a
    // slow rail does not starve the UI.
    uint32_t end_mv = start_mv;
    int64_t elapsed_us = 0;
    int polls = 0;
    while (end_mv < start_mv + kRiseToMeasureMv && elapsed_us < kRiseTimeoutUs) {
        if (polls++ < kBusyPolls) {
            esp_rom_delay_us(100);
        } else {
            vTaskDelay(1);
        }
        end_mv = read_mv(adc, channel);
        elapsed_us = esp_timer_get_time() - start_us;
    }

    if (end_mv < start_mv + kRiseToMeasureMv) {
        // Nothing charged: the rail is live, so its capacitance cannot be measured.
        ESP_LOGI(TAG, "LEDs present, count unknown: %lu -> %lu mV in %lld us, rail is not charging",
                 static_cast<unsigned long>(start_mv), static_cast<unsigned long>(end_mv), elapsed_us);
        return ProbeResult{ProbeStatus::Present, 0};
    }
    const uint16_t estimate = probe_estimate_leds(start_mv, end_mv, static_cast<uint32_t>(elapsed_us));
    ESP_LOGI(TAG, "LEDs present: %lu -> %lu mV in %lld us, about %u LEDs",
             static_cast<unsigned long>(start_mv), static_cast<unsigned long>(end_mv), elapsed_us,
             estimate);
    return ProbeResult{ProbeStatus::Present, estimate};
}

// Runs check with the data pin on the ADC, then leaves the pin driven low.
template <typename Check>
void with_adc_on_pin(gpio_num_t data_pin, Check check) {
    adc_unit_t unit{};
    adc_channel_t channel{};
    adc_oneshot_unit_handle_t adc = nullptr;
    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    esp_err_t err = adc_oneshot_io_to_channel(data_pin, &unit, &channel);
    if (err == ESP_OK) {
        adc_oneshot_unit_init_cfg_t unit_config = {};
        unit_config.unit_id = unit;
        err = adc_oneshot_new_unit(&unit_config, &adc);
    }
    if (err == ESP_OK) {
        // Also floats the pad and clears its pulls, so check() sets the pull-up.
        err = adc_oneshot_config_channel(adc, channel, &channel_config);
    }
    if (err == ESP_OK) {
        check(adc, channel);
    } else {
        ESP_LOGW(TAG, "ADC setup failed: %s", esp_err_to_name(err));
    }
    if (adc != nullptr) {
        adc_oneshot_del_unit(adc);
    }
    drive_pin_low(data_pin);
}

}  // namespace

ProbeResult probe_garland(gpio_num_t data_pin) {
    vTaskDelay(pdMS_TO_TICKS(kRailSettleMs));

    // Empties the cable capacitance; the rail cannot discharge through this pin.
    drive_pin_low(data_pin);
    vTaskDelay(pdMS_TO_TICKS(1));

    ProbeResult result{ProbeStatus::NotChecked, 0};
    with_adc_on_pin(data_pin, [&](adc_oneshot_unit_handle_t adc, adc_channel_t channel) {
        result = measure(adc, channel, data_pin);
    });
    return result;
}

PresenceReading probe_presence(gpio_num_t data_pin) {
    drive_pin_low(data_pin);
    esp_rom_delay_us(kFirstReadDelayUs);

    PresenceReading reading{ProbeStatus::NotChecked, 0};
    with_adc_on_pin(data_pin, [&](adc_oneshot_unit_handle_t adc, adc_channel_t channel) {
        gpio_pullup_en(data_pin);
        esp_rom_delay_us(kFirstReadDelayUs);
        reading.mv = read_mv(adc, channel);
        reading.status = probe_says_present(reading.mv) ? ProbeStatus::Present : ProbeStatus::Absent;
    });
    return reading;
}

}  // namespace garland
