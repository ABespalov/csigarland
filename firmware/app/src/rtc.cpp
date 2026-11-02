/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The optional DS3231 clock module on the I2C pads
 * Contributors: None
 */

#include "rtc.h"

#include <cstring>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "rtc_logic.h"

namespace garland {
namespace {

const char* const TAG = "rtc";

constexpr uint16_t kAddress = 0x68;
constexpr uint32_t kBusHz = 100000;
constexpr int kTimeoutMs = 50;
constexpr uint8_t kRegSeconds = 0x00;
constexpr uint8_t kRegControl = 0x0E;
constexpr uint8_t kRegStatus = 0x0F;
// Set, the oscillator stops on battery: perfect time while powered, none after unplugging.
constexpr uint8_t kControlOscillatorOffOnBattery = 0x80;
constexpr uint8_t kStatusOscillatorStopped = 0x80;

i2c_master_dev_handle_t s_device = nullptr;

bool read_registers(uint8_t first, uint8_t* out, size_t count) {
    return s_device != nullptr && i2c_master_transmit_receive(s_device, &first, 1, out, count, kTimeoutMs) == ESP_OK;
}

// One transaction, though the chip takes each byte as it is acknowledged: a failure can still
// leave part of it written, which is why write_rtc_checked marks a setting as under way.
bool write_registers(uint8_t first, const uint8_t* values, size_t count) {
    uint8_t buffer[1 + kRtcTimeRegisters];
    if (s_device == nullptr || count > kRtcTimeRegisters) {
        return false;
    }
    buffer[0] = first;
    std::memcpy(buffer + 1, values, count);
    return i2c_master_transmit(s_device, buffer, count + 1, kTimeoutMs) == ESP_OK;
}

void keep_running_on_battery() {
    uint8_t control = 0;
    if (read_registers(kRegControl, &control, 1) && (control & kControlOscillatorOffOnBattery) != 0) {
        control = static_cast<uint8_t>(control & ~kControlOscillatorOffOnBattery);
        uint8_t read_back = 0;
        if (write_registers(kRegControl, &control, 1) && read_registers(kRegControl, &read_back, 1) &&
            (read_back & kControlOscillatorOffOnBattery) == 0) {
            ESP_LOGI(TAG, "battery oscillator was off; turned on");
        } else {
            ESP_LOGW(TAG, "battery oscillator is off and could not be turned on: time may stop without power");
        }
    }
}

}  // namespace

bool rtc_start() {
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = -1;
    bus_config.sda_io_num = static_cast<gpio_num_t>(CSIGARLAND_RTC_SDA_GPIO);
    bus_config.scl_io_num = static_cast<gpio_num_t>(CSIGARLAND_RTC_SCL_GPIO);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;
    i2c_master_bus_handle_t bus = nullptr;
    const esp_err_t opened = i2c_new_master_bus(&bus_config, &bus);
    if (opened != ESP_OK) {
        ESP_LOGW(TAG, "I2C bus failed: %s", esp_err_to_name(opened));
        return false;
    }
    // Before a device is attached, as a probe changes the bus timing a device was set up with.
    if (i2c_master_probe(bus, kAddress, kTimeoutMs) != ESP_OK) {
        ESP_LOGI(TAG, "no clock module");
        i2c_del_master_bus(bus);
        return false;
    }
    i2c_device_config_t device_config = {};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = kAddress;
    device_config.scl_speed_hz = kBusHz;
    if (i2c_master_bus_add_device(bus, &device_config, &s_device) != ESP_OK) {
        ESP_LOGW(TAG, "clock module could not be attached");
        s_device = nullptr;
        i2c_del_master_bus(bus);
        return false;
    }
    keep_running_on_battery();
    ESP_LOGI(TAG, "clock module found");
    return true;
}

bool rtc_present() {
    return s_device != nullptr;
}

bool rtc_read(int64_t& epoch, bool& trusted) {
    uint8_t status = 0;
    uint8_t registers[kRtcTimeRegisters];
    if (!read_registers(kRegStatus, &status, 1) || !read_registers(kRegSeconds, registers, sizeof(registers)) ||
        !rtc_decode(registers, epoch)) {
        return false;
    }
    trusted = rtc_is_trusted((status & kStatusOscillatorStopped) != 0, epoch);
    return true;
}

bool rtc_write(int64_t epoch) {
    if (!rtc_epoch_is_valid(epoch)) {
        return false;
    }
    uint8_t registers[kRtcTimeRegisters];
    rtc_encode(epoch, registers);
    if (!write_registers(kRegSeconds, registers, sizeof(registers))) {
        return false;
    }
    uint8_t status = 0;
    if (!read_registers(kRegStatus, &status, 1)) {
        return false;
    }
    status = static_cast<uint8_t>(status & ~kStatusOscillatorStopped);
    if (!write_registers(kRegStatus, &status, 1) || !read_registers(kRegStatus, &status, 1)) {
        return false;
    }
    keep_running_on_battery();
    return (status & kStatusOscillatorStopped) == 0;
}

}  // namespace garland
