/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Main application entry point for the csiGarland firmware
 * Contributors: Anton Bespalov
 */

#include "buttons.h"
#include "display.h"
#include "effect_catalog.h"
#include "effect_pack_store.h"
#include "effect_store.h"
#include "esp_log.h"
#include "firmware_version.h"
#include "garland_output.h"
#include "garland_state.h"
#include "lighting_task.h"
#include "ota.h"
#include "portal.h"
#include "portal_auth.h"
#include "rtc.h"
#include "scheduler.h"
#include "settings.h"
#include "trial_log.h"

static const char* TAG = "csiGarland";

static garland::State s_state(CSIGARLAND_LED_COUNT);

extern "C" void app_main(void) {
    garland::trial_log_start();
    // First of all, at the longest supported chain: the configured count is not known
    // yet, and data past the end of a shorter chain goes nowhere.
    garland::GarlandOutput(static_cast<gpio_num_t>(CSIGARLAND_GARLAND_GPIO),
                           static_cast<gpio_num_t>(CSIGARLAND_POWER_GATE_GPIO), CSIGARLAND_MAX_LEDS)
        .blank_at_boot();

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " csiGarland Firmware Starting (v%s)", CSIGARLAND_VERSION);
    ESP_LOGI(TAG, " Default LEDs: %d units", CSIGARLAND_LED_COUNT);
    ESP_LOGI(TAG, " Power Limit: %d mA", CSIGARLAND_POWER_LIMIT_MA);
    ESP_LOGI(TAG, "========================================");

    const garland::PackView pack = garland::pack_store_open();
    const size_t effects = garland::catalog_load(pack.data, pack.bytes, [](const char* id, const char* error) {
        ESP_LOGE(TAG, "effect %s rejected: %s", id, error);
    });
    ESP_LOGI(TAG, " Effects: %u", static_cast<unsigned>(effects));
    s_state.set_effect_count(static_cast<uint16_t>(garland::catalog_positions()));
    garland::EffectMask favorites = garland::catalog_default_favorites();

    const esp_err_t err = garland::settings_init();
    if (err == ESP_OK) {
        // Before favourites and the remembered effect, which may be among these.
        garland::effect_store_start();
        s_state.set_led_setting(garland::settings_load_led_count());
        static char s_favorites_text[garland::kFavoritesTextBytes];
        if (garland::settings_load_favorites(s_favorites_text, sizeof(s_favorites_text))) {
            // Every saved effect may be gone from a newer catalog; then the defaults count again.
            const garland::EffectMask saved = garland::favorites_from_text(s_favorites_text);
            favorites = saved.any() ? saved : favorites;
        }
        garland::DisplaySettings display;
        garland::settings_load_display(display);
        s_state.set_saver(display.saver, display.saver_delay_seconds);
        s_state.set_backlight_levels(display.screen_percent, display.saver_percent);
    } else {
        ESP_LOGE(TAG, "settings unavailable: %s", esp_err_to_name(err));
    }
    s_state.set_favorites(favorites);
    s_state.select_first_favorite();
    garland::StoredSettings remembered;
    if (err == ESP_OK && garland::settings_load_remembered(remembered)) {
        // Whatever this firmware no longer accepts keeps its default, such as an effect
        // gone from a newer catalog.
        const int index = garland::catalog_find(remembered.effect_id);
        if (index >= 0) {
            s_state.set_effect(static_cast<uint16_t>(index));
        }
        if (garland::remembered_brightness_is_valid(remembered.brightness_percent)) {
            s_state.set_brightness(remembered.brightness_percent);
        }
        if (garland::manual_speed_x4_is_valid(remembered.speed_x4)) {
            s_state.set_speed(remembered.speed_x4);
        }
        s_state.set_power(remembered.playing != 0);
    }

    // The lighting task comes first: it probes the garland before the first button or the
    // remembered state can turn it on.
    const bool lighting_running = garland::start_lighting_task(s_state);
    if (!lighting_running) {
        ESP_LOGE(TAG, "no lighting task");
    }
    const bool display_running = garland::start_display(s_state);
    // Before the buttons, which ask it about requests from their first press.
    garland::auth_init(s_state);
    const bool buttons_running = garland::start_buttons(s_state);
    bool portal_running = false;
    bool scheduler_running = false;
    if (err == ESP_OK) {
        garland::time_load_zone();
        garland::time_load_from_rtc();
        s_state.set_clock_module(garland::rtc_present());
        scheduler_running = garland::start_scheduler(s_state);
        if (!scheduler_running) {
            ESP_LOGE(TAG, "no scheduler task");
        }
        // The Wi-Fi driver needs NVS.
        portal_running = garland::start_portal(s_state);
    }
    // A firmware without its portal could never be replaced by the next update, one short of its
    // tasks is out of memory at every start, and one without its buttons or screen cannot stand
    // alone or approve a browser. Outside a trial it carries on as far as it can.
    garland::ota_start_trial(portal_running && lighting_running && scheduler_running && display_running && buttons_running);
}
