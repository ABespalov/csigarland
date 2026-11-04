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
#include "nvs.h"
#include "ota.h"
#include "playlist_logic.h"
#include "playlist_resolver.h"
#include "portal.h"
#include "portal_auth.h"
#include "rtc.h"
#include "scheduler.h"
#include "settings.h"
#include "trial_log.h"

static const char* TAG = "csiGarland";

static garland::State s_state(CSIGARLAND_LED_COUNT);
// When the remembered choice was made, as read at the start, for the scheduler: see start_scheduler.
static int64_t s_choice_epoch_at_start = garland::kNoChoiceYet;
static bool s_remembered_unread = false;

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
        // A reading that fails is tried again, here and later: a list left out is no empty list, and the
        // schedule and Play would find it empty.
        garland::playlist_resolve(s_state);
        garland::DisplaySettings display;
        garland::settings_load_display(display);
        s_state.set_saver(display.saver, display.saver_delay_seconds);
        s_state.set_backlight_levels(display.screen_percent, display.saver_percent);
    } else {
        ESP_LOGE(TAG, "settings unavailable: %s", esp_err_to_name(err));
    }
    s_state.set_favorites(favorites);
    s_state.select_first_favorite();
    // Read once, for the state and the scheduler alike, and tried again if the reading fails: the owner's
    // choice outranks the schedule's steps from before it, and the scheduler must know when it was made.
    garland::StoredSettings remembered;
    esp_err_t remembered_read = ESP_ERR_NVS_NOT_FOUND;
    for (int attempt = 0; err == ESP_OK && attempt < 3; ++attempt) {
        remembered_read = garland::settings_read_remembered(remembered);
        if (remembered_read == ESP_OK || remembered_read == ESP_ERR_NVS_NOT_FOUND) {
            break;
        }
        ESP_LOGW(TAG, "reading the remembered choice failed: %s", esp_err_to_name(remembered_read));
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    s_choice_epoch_at_start = remembered_read == ESP_OK            ? remembered.choice_epoch
                              : remembered_read == ESP_ERR_NVS_NOT_FOUND ? garland::kNoChoiceYet
                                                                        : 0;
    if (err == ESP_OK && remembered_read == ESP_OK) {
        garland::remembered_apply(s_state, remembered);
        garland::playlist_restore_playing(s_state);
    }
    // Not read, and there: nothing is saved over it until it is, as the scheduler tries again.
    s_remembered_unread = err == ESP_OK && remembered_read != ESP_OK && remembered_read != ESP_ERR_NVS_NOT_FOUND;

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
        scheduler_running = garland::start_scheduler(s_state, s_choice_epoch_at_start, s_remembered_unread);
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
