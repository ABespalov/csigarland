/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Tell whether a garland is on the data pin
 * Contributors: None
 */

#include "garland_probe.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "garland_probe_math.h"

namespace garland {
namespace {

const char* const TAG = "probe";

constexpr uint32_t kFirstReadDelayUs = 200;
constexpr int kSamplesPerRead = 4;
constexpr int kAdcFullScaleRaw = 4095;

// False when the ADC did not answer: no reading, which must not pass for an open pin.
bool read_mv(adc_oneshot_unit_handle_t adc, adc_channel_t channel, uint32_t& mv) {
    int total = 0;
    for (int i = 0; i < kSamplesPerRead; ++i) {
        int raw = 0;
        if (adc_oneshot_read(adc, channel, &raw) != ESP_OK) {
            return false;
        }
        total += raw;
    }
    mv = static_cast<uint32_t>(total / kSamplesPerRead) * kProbePullupMv / kAdcFullScaleRaw;
    return true;
}

void drive_pin_low(gpio_num_t pin) {
    gpio_pullup_dis(pin);
    gpio_set_level(pin, 0);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
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

PresenceReading probe_presence(gpio_num_t data_pin) {
    drive_pin_low(data_pin);
    esp_rom_delay_us(kFirstReadDelayUs);

    PresenceReading reading{ProbeStatus::NotChecked, 0};
    with_adc_on_pin(data_pin, [&](adc_oneshot_unit_handle_t adc, adc_channel_t channel) {
        // Weak on purpose: at most about 70 uA can reach an unpowered rail.
        gpio_pullup_en(data_pin);
        esp_rom_delay_us(kFirstReadDelayUs);
        if (read_mv(adc, channel, reading.mv)) {
            reading.status = probe_says_present(reading.mv) ? ProbeStatus::Present : ProbeStatus::Absent;
        }
    });
    return reading;
}

}  // namespace garland
