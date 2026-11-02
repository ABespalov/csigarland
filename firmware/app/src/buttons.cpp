/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Wire the three panel buttons to the garland state
 * Contributors: None
 */

#include "buttons.h"

#include "button_gpio.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "iot_button.h"
#include "portal_auth.h"

namespace garland {
namespace {

const char* const TAG = "buttons";

constexpr uint16_t kLongPressMs = 800;
constexpr int64_t kForgetHoldUs = 10000000;

using Action = void (*)(State&);

struct Binding {
    int gpio;
    Action down;
    Action click;
    Action long_press;
};

constexpr Binding kBindings[] = {
    {CSIGARLAND_BUTTON_RED_GPIO, [](State& s) { s.button_down(ButtonId::Power); }, [](State& s) { s.click_power(); },
     [](State& s) { s.long_press_power(static_cast<uint32_t>(esp_timer_get_time() / 1000)); }},
    {CSIGARLAND_BUTTON_WHITE_GPIO, [](State& s) { s.button_down(ButtonId::Effect); },
     [](State& s) { s.click_effect(); }, [](State& s) { s.long_press_effect(); }},
    {CSIGARLAND_BUTTON_SPEED_GPIO, [](State& s) { s.button_down(ButtonId::Speed); },
     [](State& s) { s.click_speed(); }, [](State& s) { s.long_press_speed(); }},
};

// Callback arguments live here so they outlast start_buttons().
struct CallbackArg {
    State* state;
    Action action;
    bool is_down;
    bool is_click;
};

constexpr size_t kBindingCount = sizeof(kBindings) / sizeof(kBindings[0]);
CallbackArg s_down_args[kBindingCount];
CallbackArg s_click_args[kBindingCount];
CallbackArg s_long_args[kBindingCount];

// POWER and SPEED together answer a browser's request, and held on forget every browser. None
// of the presses that make up the pair does what it would alone, up to the next fresh press,
// as a click comes a moment after its release.
bool s_pair_held = false;
int64_t s_pair_since_us = 0;
bool s_forgot = false;

bool pair_is_down() {
    return gpio_get_level(static_cast<gpio_num_t>(CSIGARLAND_BUTTON_RED_GPIO)) == 0 &&
           gpio_get_level(static_cast<gpio_num_t>(CSIGARLAND_BUTTON_SPEED_GPIO)) == 0;
}

void on_button_event(void*, void* user_data) {
    const auto* arg = static_cast<const CallbackArg*>(user_data);
    if (arg->is_down) {
        s_pair_held = pair_is_down();
        if (s_pair_held) {
            s_pair_since_us = esp_timer_get_time();
            // A hold that answered a request is not also asking to forget every browser, the
            // one just approved among them, however long an owner keeps pressing.
            s_forgot = auth_buttons_approve();
        }
        // Every press wakes the screen and ends a countdown, the pair's as well.
        arg->action(*arg->state);
        return;
    }
    if (s_pair_held) {
        return;
    }
    if (auth_request_is_waiting()) {
        if (arg->is_click) {
            auth_button_refuse();
        }
        return;
    }
    arg->action(*arg->state);
}

void on_long_hold(void*, void*) {
    if (s_pair_held && !s_forgot && pair_is_down() && esp_timer_get_time() - s_pair_since_us >= kForgetHoldUs) {
        s_forgot = true;
        auth_forget_all_soon();
    }
}

esp_err_t add_button(const Binding& binding, CallbackArg& down_arg, CallbackArg& click_arg, CallbackArg& long_arg) {
    const button_config_t config = {
        .long_press_time = kLongPressMs,
        .short_press_time = 0,
    };
    const button_gpio_config_t gpio_config = {
        .gpio_num = binding.gpio,
        .active_level = 0,
        .enable_power_save = false,
        .disable_pull = false,
    };
    button_handle_t button = nullptr;
    ESP_RETURN_ON_ERROR(iot_button_new_gpio_device(&config, &gpio_config, &button), TAG, "GPIO %d",
                        binding.gpio);
    ESP_RETURN_ON_ERROR(iot_button_register_cb(button, BUTTON_PRESS_DOWN, nullptr, on_button_event, &down_arg), TAG,
                        "press %d", binding.gpio);
    ESP_RETURN_ON_ERROR(iot_button_register_cb(button, BUTTON_SINGLE_CLICK, nullptr, on_button_event, &click_arg),
                        TAG, "click %d", binding.gpio);
    ESP_RETURN_ON_ERROR(iot_button_register_cb(button, BUTTON_LONG_PRESS_START, nullptr, on_button_event, &long_arg),
                        TAG, "long press %d", binding.gpio);
    ESP_RETURN_ON_ERROR(iot_button_register_cb(button, BUTTON_LONG_PRESS_HOLD, nullptr, on_long_hold, nullptr), TAG,
                        "hold %d", binding.gpio);
    return ESP_OK;
}

}  // namespace

bool start_buttons(State& state) {
    bool all_started = true;
    for (size_t i = 0; i < kBindingCount; ++i) {
        s_down_args[i] = CallbackArg{&state, kBindings[i].down, true, false};
        s_click_args[i] = CallbackArg{&state, kBindings[i].click, false, true};
        s_long_args[i] = CallbackArg{&state, kBindings[i].long_press, false, false};
        const esp_err_t err = add_button(kBindings[i], s_down_args[i], s_click_args[i], s_long_args[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "button on GPIO %d failed: %s", kBindings[i].gpio, esp_err_to_name(err));
            all_started = false;
        }
    }
    return all_started;
}

}  // namespace garland
