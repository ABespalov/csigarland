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
#include "looks.h"
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
static garland::Remembered s_saved_at_start;

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

    const esp_err_t err = garland::settings_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "settings unavailable: %s", esp_err_to_name(err));
    }
    // Whatever came of the settings, which do not hold these: their own partition. Before favourites, the
    // playlist and the remembered effect, which may name them.
    garland::effect_store_start();
    // With the settings unreadable too, as each of these: a reading that fails is tried again, here and later,
    // and nothing is saved over what is there meanwhile. A list left out is no empty list, and the schedule,
    // Play and a list left playing would find it empty.
    garland::looks_load(s_state);
    garland::playlist_resolve(s_state);
    s_state.select_first_favorite();
    // Read once, for the state and the scheduler alike, and tried again if the reading fails: the owner's
    // choice outranks the schedule's steps from before it, and the scheduler must know when it was made.
    garland::StoredSettings remembered;
    // Settings that could not be read at all may hold a choice all the same: not to be saved over.
    esp_err_t remembered_read = err == ESP_OK ? ESP_ERR_NVS_NOT_FOUND : err;
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
    s_remembered_unread = remembered_read != ESP_OK && remembered_read != ESP_ERR_NVS_NOT_FOUND;
    // What flash holds of it, as now on the state, before any button can change that: see start_scheduler.
    const garland::Snapshot at_start = s_state.snapshot();
    s_saved_at_start = garland::remembered_from(at_start, at_start.effect);

    // The lighting task comes first: it probes the garland before the first button or the
    // remembered state can turn it on.
    const bool lighting_running = garland::start_lighting_task(s_state);
    if (!lighting_running) {
        ESP_LOGE(TAG, "no lighting task");
    }
    const bool display_running = garland::start_display(s_state);
    // Before the buttons, which ask it about requests from their first press.
    garland::auth_init(s_state);
    // Before the buttons too: a choice they make before the scheduler starts still has its moment.
    s_state.set_choice_clock(garland::choice_clock_ms);
    const bool buttons_running = garland::start_buttons(s_state);
    // With the settings unreadable too. Their reads fail and are tried again, the time is UTC, and the
    // scheduler and the portal start all the same, as the portal's pages share their locks. A closed
    // garland has no other way in: with no home network known it opens its setup network, whose password
    // is on the screen, and plain HTTP there takes the next update.
    garland::time_load_zone();
    garland::time_load_from_rtc();
    s_state.set_clock_module(garland::rtc_present());
    const bool scheduler_running = garland::start_scheduler(s_state, s_choice_epoch_at_start, s_saved_at_start, s_remembered_unread);
    if (!scheduler_running) {
        ESP_LOGE(TAG, "no scheduler task");
    }
    const bool portal_running = garland::start_portal(s_state);
    // A firmware without its portal could never be replaced by the next update, one that cannot read its
    // settings may have broken them, one short of its tasks is out of memory at every start, and one
    // without its buttons or screen cannot stand alone or approve a browser. Outside a trial it carries
    // on as far as it can.
    garland::ota_start_trial(err == ESP_OK && portal_running && lighting_running && scheduler_running && display_running &&
                             buttons_running);
}
