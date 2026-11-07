/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: ST7789V2 status screen drawn with LVGL
 * Contributors: None
 */

#include "display.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "display_view.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "effect_catalog.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "firmware_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_logic.h"
#include "saver_schedule.h"
#include "saver_view.h"
#include "scheduler.h"
#include "screensaver.h"
#include "settings.h"

namespace garland {
namespace {

const char* const TAG = "ui";

constexpr int kRotatedRowOffset = 320 - kScreenSize;
constexpr int kSpiClockHz = 40'000'000;
constexpr size_t kDrawBufferPixels = kScreenSize * 40;
// Often enough for the breathing dot's 25 ms frames.
constexpr uint32_t kPollMs = 25;
// LVGL draws on this stack. Nested objects and text go deep: one frame of this screen
// overran the 4 KB of the main task.
constexpr int kLvglStackBytes = 10240;
constexpr uint32_t kBacklightPwmHz = 5000;
constexpr uint32_t kBacklightFullDuty = 1023;  // 10 bits.

// flash-device.ps1 reads this exact string out of the built image.
const char kFirmwareLabel[] = "FW " CSIGARLAND_VERSION;
// The screen shows the version alone, from the same string.
constexpr size_t kFirmwareLabelPrefixBytes = 3;

bool s_first_frame_drawn = false;
bool s_backlight_dims = false;
uint8_t s_backlight_percent = 0;
bool s_saver_shown = false;
uint32_t s_activity = 0;
// The second Scheduler24's words were last worked out for: once a second is enough.
int64_t s_step_second = -1;
// What happened since the start, as note_events saw it, on the uptime clock.
bool s_events_seen = false;
bool s_seen_power = false;
bool s_seen_rail = false;
int64_t s_power_changed_ms = 0;
SaverMoment s_power_event;
SaverMoment s_rail_event;
Snapshot s_shown;
bool s_shown_valid = false;

esp_err_t init_panel(esp_lcd_panel_io_handle_t* out_io, esp_lcd_panel_handle_t* out_panel) {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = CSIGARLAND_LCD_MOSI;
    bus.miso_io_num = -1;
    bus.sclk_io_num = CSIGARLAND_LCD_SCK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = kDrawBufferPixels * sizeof(uint16_t);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "SPI bus");

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.dc_gpio_num = static_cast<gpio_num_t>(CSIGARLAND_LCD_DC);
    io_config.cs_gpio_num = static_cast<gpio_num_t>(CSIGARLAND_LCD_CS);
    io_config.pclk_hz = kSpiClockHz;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.spi_mode = 0;
    io_config.trans_queue_depth = 10;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, out_io), TAG, "panel IO");

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = static_cast<gpio_num_t>(CSIGARLAND_LCD_RST);
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(*out_io, &panel_config, out_panel), TAG, "panel");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*out_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*out_panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(*out_panel, true), TAG, "invert");
    // The board sits in the enclosure with its USB end up, which is 180 degrees
    // from the panel's own orientation. The controller's memory is 320 rows tall,
    // so mirroring both axes moves the visible window to its other end.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(*out_panel, true, true), TAG, "mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(*out_panel, 0, kRotatedRowOffset), TAG, "gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(*out_panel, true), TAG, "on");
    return ESP_OK;
}

esp_err_t init_lvgl(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel) {
    lvgl_port_cfg_t port_config = ESP_LVGL_PORT_INIT_CONFIG();
    port_config.task_stack = kLvglStackBytes;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_config), TAG, "LVGL port");

    lvgl_port_display_cfg_t display_config = {};
    display_config.io_handle = io;
    display_config.panel_handle = panel;
    display_config.buffer_size = kDrawBufferPixels;
    display_config.hres = kScreenSize;
    display_config.vres = kScreenSize;
    display_config.color_format = LV_COLOR_FORMAT_RGB565;
    // Must repeat init_panel's mirroring: the port re-applies these to the panel.
    display_config.rotation.mirror_x = true;
    display_config.rotation.mirror_y = true;
    display_config.flags.buff_dma = true;
    display_config.flags.swap_bytes = true;
    ESP_RETURN_ON_FALSE(lvgl_port_add_disp(&display_config) != nullptr, ESP_FAIL, TAG, "LVGL display");
    return ESP_OK;
}

const char* probe_name(ProbeStatus status) {
    switch (status) {
        case ProbeStatus::Present:
            return "found";
        case ProbeStatus::Absent:
            return "none";
        default:
            return "unchecked";
    }
}

void show_snapshot(const Snapshot& s) {
    const CatalogText effect_name = catalog_name(s.effect);
    const ScreenInputs inputs = {effect_name.c_str(), kApSsid, settings_ap_password(), CSIGARLAND_PORTAL_IP};
    display_view_draw(build_screen_model(s, inputs));
}

// Starts dark, so the panel's power-on noise is never seen.
void init_backlight() {
    const auto pin = static_cast<gpio_num_t>(CSIGARLAND_LCD_BL);
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);

    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = kBacklightPwmHz;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ledc_channel_config_t channel = {};
    channel.gpio_num = pin;
    channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_0;
    channel.timer_sel = LEDC_TIMER_0;
    channel.duty = 0;
    esp_err_t err = ledc_timer_config(&timer);
    if (err == ESP_OK) {
        err = ledc_channel_config(&channel);
    }
    s_backlight_dims = err == ESP_OK;
    if (!s_backlight_dims) {
        ESP_LOGW(TAG, "backlight cannot dim: %s", esp_err_to_name(err));
    }
}

// Without dimming, any level above zero means fully on: a lit screen beats a dark one.
void set_backlight_level(uint8_t percent) {
    if (percent == s_backlight_percent) {
        return;
    }
    s_backlight_percent = percent;
    if (!s_backlight_dims) {
        gpio_set_level(static_cast<gpio_num_t>(CSIGARLAND_LCD_BL), percent > 0 ? 1 : 0);
        return;
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, backlight_duty(percent, kBacklightFullDuty));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void update_backlight(const State& state) {
    if (!s_saver_shown) {
        set_backlight_level(state.screen_percent());
    } else if (saver_view_engine().kind() == SaverKind::BlackScreen) {
        set_backlight_level(0);
    } else {
        set_backlight_level(state.saver_percent());
    }
}

void update_clock_layout() {
    char now[12];
    const bool synced = time_format_clock(now, sizeof(now));
    saver_view_take_clock(now, synced);
}

void effect_name(int effect, char* out, size_t size) {
    std::snprintf(out, size, "%s", catalog_name(static_cast<size_t>(effect)).c_str());
}

// What happened since the start, for Scheduler24, noted at every poll: the garland turned on or off, by a
// button, a page or a step, and the LEDs' rail on or off apart from that, as when the power settings hold it
// off. A rail that follows the turn on or off, or the start, within a few seconds is part of it.
void note_events(const Snapshot& now, int64_t now_ms) {
    if (!s_events_seen) {
        s_events_seen = true;
        s_seen_power = now.power_chosen;
        s_seen_rail = now.rail_on;
        return;
    }
    if (now.power_chosen != s_seen_power) {
        s_seen_power = now.power_chosen;
        s_power_event = saver_moment(now_ms, s_seen_power ? "On" : "Off");
        s_power_changed_ms = now_ms;
    }
    if (now.rail_on != s_seen_rail) {
        s_seen_rail = now.rail_on;
        const bool follows = now_ms - s_power_changed_ms < kSaverFollowsMs && s_seen_rail == s_seen_power;
        if (!follows) {
            s_rail_event = saver_moment(now_ms, s_seen_rail ? "LEDs on" : "LEDs off");
        }
    }
}

// Scheduler24's words, worked out again each second as the time to or since counts: the soonest to come of the
// schedule's next step and the playlist's next effect, or with neither the latest of what happened.
void update_scheduler_text(const State& state, const Snapshot& now, int64_t now_ms) {
    if (now_ms / 1000 == s_step_second) {
        return;
    }
    s_step_second = now_ms / 1000;
    char name[kSaverNameBytes];
    SaverMoment ahead[2];
    int64_t minute = -1;
    ResolvedAction action;
    if (schedule_next_step(now.playlist_size > 0, now.catalog_revision, minute, action)) {
        saver_action_name(action, effect_name, name, sizeof(name));
        ahead[0] = saver_moment(now_ms + (minute * 60 - static_cast<int64_t>(std::time(nullptr))) * 1000, name);
    }
    const uint32_t playlist_next_ms = state.playlist_next_ms();
    if (now.playlist && playlist_next_ms != 0) {
        uint16_t effects[kMaxPlaylistEffects] = {};
        const int following = playlist_next(effects, state.playlist(effects), now.effect);
        // A list of one effect, or of one the catalog still has, never moves on: nothing comes of its dwell.
        if (following >= 0 && following != static_cast<int>(now.effect)) {
            effect_name(following, name, sizeof(name));
            ahead[1] = saver_moment(now_ms + static_cast<int32_t>(playlist_next_ms - static_cast<uint32_t>(now_ms)), name);
        }
    }
    SaverMoment behind[4] = {saver_moment(0, "Start"), s_power_event, s_rail_event};
    // Whole, and named as it was: it may stay the last thing that happened for weeks, and its effect's place
    // in the catalog be another's by now.
    int64_t done_ms = 0;
    if (schedule_last_step(done_ms, name, sizeof(name))) {
        behind[3] = saver_moment(done_ms, name);
    }
    // The turn on or off a step made, seen at the next poll, is that step.
    saver_fold_into_step(behind[3], &behind[1], 2);
    char text[kSaverTextBytes];
    saver_text(ahead, 2, behind, 4, now_ms, text, sizeof(text));
    saver_view_take_text(text);
}

void enter_saver(const ScreensaverView& view, uint32_t now_ms, State& state) {
    saver_view_show_kind();
    saver_view_draw(view, now_ms);
    lv_screen_load(saver_view_root());
    s_saver_shown = true;
    state.set_saver_active(true);
}

void leave_saver(State& state) {
    lv_screen_load(display_view_root());
    s_saver_shown = false;
    state.set_saver_active(false);
}

void poll_saver(State& state, const Snapshot& now) {
    const auto now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    note_events(now, static_cast<int64_t>(now_ms));
    Screensaver& engine = saver_view_engine();
    const uint32_t activity = state.activity();
    if (activity != s_activity) {
        s_activity = activity;
        engine.note_activity(now_ms);
    }
    // Scheduler24 needs the clock module, which keeps the time through a power cut: with none, as one taken
    // out since it was chosen, it runs as the Flying Clock.
    const SaverKind chosen = state.saver_kind();
    const SaverKind kind = chosen == SaverKind::Scheduler24 && !now.clock_module ? SaverKind::FlyingClock : chosen;
    if (kind != engine.kind()) {
        // The clock and Scheduler24 share the bouncing sprite, each of its own size: laid out again. One changed
        // while running starts afresh, its boxes shown or hidden for it.
        saver_view_forget_layout();
        s_step_second = -1;
        if (s_saver_shown) {
            engine.note_activity(now_ms);
        }
    }
    engine.set_kind(kind);
    engine.set_idle_delay(uint32_t{state.saver_delay_seconds()} * 1000);
    if (kind == SaverKind::FlyingClock) {
        update_clock_layout();
    } else if (kind == SaverKind::Scheduler24) {
        update_scheduler_text(state, now, static_cast<int64_t>(now_ms));
    }

    const ScreensaverView view = engine.poll(now_ms, screen_row(choose_screen(now)).keeps_awake);
    if (view.active != s_saver_shown) {
        if (view.active) {
            enter_saver(view, static_cast<uint32_t>(now_ms), state);
        } else {
            leave_saver(state);
        }
    } else if (view.active && view.changed) {
        saver_view_draw(view, static_cast<uint32_t>(now_ms));
    }
}

void poll_state(lv_timer_t* timer) {
    if (!s_first_frame_drawn) {
        // Before the backlight comes on below, and here and not in start_screen: drawing
        // needs far more stack than the main task has.
        s_first_frame_drawn = true;
        lv_refr_now(nullptr);
        ESP_LOGI(TAG, "first frame drawn, %u bytes of stack to spare",
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    }
    State& state = *static_cast<State*>(lv_timer_get_user_data(timer));
    const Snapshot now = state.snapshot();
    poll_saver(state, now);
    update_backlight(state);
    if (s_shown_valid && now == s_shown) {
        return;
    }
    s_shown = now;
    s_shown_valid = true;
    show_snapshot(now);
    if (now.update_percent != kNoUpdate) {
        return;  // A line per percent would say nothing.
    }
    char speed[kSpeedTextBytes];
    speed_to_text(now.speed_x4, speed);
    ESP_LOGI(TAG, "power=%d rail=%d mode=%s bright=%u speed=%s probe=%s/%u setting=%u leds=%u off=%u",
             now.power_requested, now.rail_on, catalog_id(now.effect).c_str(), now.brightness_percent,
             speed, probe_name(now.probe), now.led_estimate,
             now.led_setting, now.led_count, now.off_seconds);
}

esp_err_t start_screen(State& state) {
    init_backlight();

    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_handle_t panel = nullptr;
    ESP_RETURN_ON_ERROR(init_panel(&io, &panel), TAG, "panel");
    ESP_RETURN_ON_ERROR(init_lvgl(io, panel), TAG, "LVGL");

    ESP_RETURN_ON_FALSE(lvgl_port_lock(0), ESP_FAIL, TAG, "LVGL lock");
    display_view_build(kFirmwareLabel + kFirmwareLabelPrefixBytes);
    saver_view_build();
    show_snapshot(state.snapshot());
    s_activity = state.activity();
    saver_view_engine().note_activity(static_cast<uint64_t>(esp_timer_get_time() / 1000));
    const lv_timer_t* const poll = lv_timer_create(poll_state, kPollMs, &state);
    lvgl_port_unlock();
    // Without it nothing updates the screen or turns its backlight on.
    ESP_RETURN_ON_FALSE(poll != nullptr, ESP_ERR_NO_MEM, TAG, "poll timer");
    return ESP_OK;
}

}  // namespace

bool start_display(State& state) {
    const esp_err_t err = start_screen(state);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display failed: %s", esp_err_to_name(err));
    }
    return err == ESP_OK;
}

}  // namespace garland
