/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: High-priority task that owns the LED rail, the probe and the frames
 * Contributors: None
 */

#include "lighting_task.h"

#include <algorithm>
#include <array>
#include <span>

#include "effect_catalog.h"
#include "effect_renderer.h"
#include "effects.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "garland_output.h"
#include "garland_probe.h"
#include "light_fade.h"
#include "output_health.h"

namespace garland {
namespace {

const char* const TAG = "lighting";

constexpr int64_t kRenderPeriodUs = 16'000;
constexpr uint64_t kDitherPeriodUs = 2500;  // 400 frames a second, where the chain allows it.
constexpr TickType_t kWaitLitTicks = pdMS_TO_TICKS(4);  // Only if the timer did not start.
constexpr TickType_t kWaitDarkTicks = pdMS_TO_TICKS(16);
constexpr int64_t kLedWireUs = 30;  // 24 bits at 1.25 us.
constexpr int64_t kLatchUs = 400;   // The LEDs take a frame as done after 280 us of silence.
constexpr int64_t kPresenceCheckUs = 3'000'000;
constexpr uint32_t kStackBytes = 6144;
constexpr UBaseType_t kPriority = configMAX_PRIORITIES - 5;
constexpr int64_t kParseRetryUs = 1'000'000;

void run_probe(State& state) {
    const ProbeResult result = probe_garland(static_cast<gpio_num_t>(CSIGARLAND_GARLAND_GPIO));
    state.set_probe(result.status, result.led_estimate);
}

// Returns false if the LED output could not be started again afterwards.
bool check_presence(State& state, GarlandOutput& output, bool rail_on, ProbeStatus previous) {
    const auto pin = static_cast<gpio_num_t>(CSIGARLAND_GARLAND_GPIO);
    output.release_data_pin();
    const PresenceReading reading = probe_presence(pin);
    const bool restarted = !rail_on || output.reclaim_data_pin();
    if (reading.status != ProbeStatus::NotChecked && reading.status != previous) {
        ESP_LOGI(TAG, "garland %s: pin at %lu mV", reading.status == ProbeStatus::Present ? "connected" : "disconnected",
                 static_cast<unsigned long>(reading.mv));
        state.set_presence(reading.status);
    }
    return restarted;
}

void wake_lighting_task(void* task) {
    xTaskNotifyGive(static_cast<TaskHandle_t>(task));
}

// Runs only while the rail is on: in standby nothing needs sending.
void set_dither_timer(esp_timer_handle_t timer, bool on) {
    if (timer == nullptr || esp_timer_is_active(timer) == on) {
        return;
    }
    if (on) {
        esp_timer_start_periodic(timer, kDitherPeriodUs);
    } else {
        esp_timer_stop(timer);
    }
}

esp_timer_handle_t create_dither_timer() {
    esp_timer_create_args_t args = {};
    args.callback = wake_lighting_task;
    args.arg = xTaskGetCurrentTaskHandle();
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "dither";
    args.skip_unhandled_events = true;
    esp_timer_handle_t timer = nullptr;
    const esp_err_t err = esp_timer_create(&args, &timer);
    if (err != ESP_OK) {
        // The task's wait running out still sends frames, only less often.
        ESP_LOGW(TAG, "no dither timer: %s", esp_err_to_name(err));
        return nullptr;
    }
    return timer;
}

void lighting_task(void* arg) {
    State& state = *static_cast<State*>(arg);
    GarlandOutput output(static_cast<gpio_num_t>(CSIGARLAND_GARLAND_GPIO),
                         static_cast<gpio_num_t>(CSIGARLAND_POWER_GATE_GPIO),
                         state.snapshot().led_count);
    run_probe(state);

    // Static: the longest chain would take a large part of this task's stack.
    static std::array<Rgb, CSIGARLAND_MAX_LEDS> s_frame;
    static std::array<RgbQ8, CSIGARLAND_MAX_LEDS> s_targets;
    static std::array<Rgb, CSIGARLAND_MAX_LEDS> s_output;
    // The old effect's last frame, while the new one takes over from it.
    static std::array<Rgb, CSIGARLAND_MAX_LEDS> s_from;
    static FrameDither s_dither;
    LightFade fade;
    bool crossing = false;
    int64_t cross_start_us = 0;
    uint32_t cross_span_ms = 0;
    size_t from_count = 0;
    bool drawn_since_on = false;  // So a cross-fade never starts from what was lit before a turn off.
    int64_t effect_since_us = 0;  // When what plays began, for the playlist.
    uint8_t playlist_mark = state.snapshot().playlist_mark;
    bool paused = false;  // An update held the task up: see where the playlist is timed.
    s_dither.restart();
    const esp_timer_handle_t dither_timer = create_dither_timer();
    uint8_t gain_percent = 0;
    uint32_t gain = brightness_gain(gain_percent);
    int64_t last_sent_us = 0;
    uint16_t shown_count = 0;
    size_t limited_count = 0;  // The chain length the targets were last rendered and limited for.
    bool rail_on = false;
    bool clamp_reported = false;
    OutputHealth health;
    static EffectRenderer s_renderer(static_cast<uint32_t>(esp_random()));
    // The effect's own clock: the SPEED button scales it, so every algorithm
    // follows the button without knowing about it.
    uint64_t effect_time_us = 0;
    // Only what is playing is held parsed. Parsing takes less than a frame, and comes at the
    // cut from one effect to the next, so nothing can be seen of it.
    static EffectSpec s_effect;
    int last_effect = -1;
    uint32_t last_revision = 0;
    int64_t next_parse_us = 0;
    int64_t last_frame_us = esp_timer_get_time();
    int64_t last_presence_check_us = last_frame_us;
    bool probed = false;  // For the turn on now asked for, whose rail comes on in the next turn.

    while (true) {
        ulTaskNotifyTake(pdTRUE, rail_on ? kWaitLitTicks : kWaitDarkTicks);
        state.tick_off_countdown(static_cast<uint32_t>(esp_timer_get_time() / 1000));
        const Snapshot now = state.snapshot();
        // While an update writes to flash nothing here touches the output, and the LEDs hold their
        // last frame: it is a long stretch of writes, and leaves the radio all the time it can
        // have. What was ordered meanwhile takes effect afterwards. The update waits for this word.
        const bool update_running = now.update_percent != kNoUpdate;
        state.set_lighting_paused(update_running);
        if (update_running) {
            paused = true;
            continue;
        }
        if (!output.set_led_count(now.led_count)) {
            state.refuse_power();
            rail_on = false;
            state.set_rail_on(false);
            set_dither_timer(dither_timer, false);
            continue;
        }
        if (now.led_count != shown_count) {
            shown_count = now.led_count;
            s_dither.resend();
        }
        // What is really on the wire, so that effects and the current limit go by it. The screen
        // and the portal hear when that is not what was asked for.
        const auto led_count = static_cast<uint16_t>(output.led_count());
        state.set_led_kept(output.count_refused() ? led_count : 0);
        const std::span<Rgb> frame(s_frame.data(), led_count);
        const std::span<RgbQ8> targets(s_targets.data(), led_count);
        const std::span<Rgb> dithered(s_output.data(), led_count);

        const int64_t check_us = esp_timer_get_time();
        if (check_us - last_presence_check_us >= kPresenceCheckUs) {
            last_presence_check_us = check_us;
            if (!check_presence(state, output, rail_on, now.probe)) {
                state.refuse_power();
                output.power_off();
                rail_on = false;
                state.set_rail_on(false);
                set_dither_timer(dither_timer, false);
                continue;
            }
            s_dither.resend();
        }

        if (!now.power_requested) {
            probed = false;
        }
        const int64_t turn_us = esp_timer_get_time();
        if (now.power_requested && !rail_on) {
            if (!probed) {
                // The rail must be off to measure, so this is the only moment to count. Most of a
                // second goes by, in which the garland may be turned off, an update begin, or the
                // count or effect change: so the rail comes on only in a turn that reads all of it
                // again, and that turn starts at once.
                run_probe(state);
                probed = true;
                xTaskNotifyGive(xTaskGetCurrentTaskHandle());
                continue;
            }
            probed = false;
            rail_on = output.power_on();
            if (!rail_on) {
                state.refuse_power();
            }
            effect_time_us = 0;
            s_renderer.reset();
            s_dither.restart();
            // From dark: in a moment, or over a minute for a scheduled rise.
            fade.set(0);
            fade.go(LightFade::kFull, now.rise ? kRiseMs : kFadeOnMs, turn_us);
            crossing = false;
            drawn_since_on = false;
            effect_since_us = turn_us;
            // So the first pass renders at once, with nothing old in the targets.
            last_presence_check_us = esp_timer_get_time();
            last_frame_us = last_presence_check_us - kRenderPeriodUs;
            state.set_rail_on(rail_on);
            set_dither_timer(dither_timer, rail_on);
        } else if (now.power_requested && fade.target() == 0) {
            // Turned on again on its way to dark: back up from where it got to.
            fade.go(LightFade::kFull, kFadeOnMs, turn_us);
        } else if (!now.power_requested && rail_on) {
            // The rail stays on while the light goes, and the power sequence follows once it has.
            if (fade.target() != 0) {
                fade.go(0, kFadeOffMs, turn_us);
            }
            if (fade.dark(turn_us)) {
                output.power_off();
                rail_on = false;
                state.set_rail_on(false);
                set_dither_timer(dither_timer, false);
            }
        }

        if (!rail_on && now.output_failed) {
            // Nothing is being sent, so nothing is failing, and the next turn on starts clean.
            health = OutputHealth{};
            state.set_output_failed(false);
        }
        if (rail_on) {
            const int64_t now_us = esp_timer_get_time();
            // Not by position alone: an upload can replace the effect at the same one.
            const uint32_t revision = catalog_revision(now.effect);
            if ((now.effect != last_effect || revision != last_revision) && now_us >= next_parse_us) {
                // Parsing a built-in effect can run out of memory for a moment: it tries again, not
                // every frame, as this task must leave time for what would free that memory. What
                // played before goes on meanwhile.
                const bool copied = catalog_copy_spec(now.effect, s_effect);
                last_effect = copied ? now.effect : -1;
                next_parse_us = copied ? 0 : now_us + kParseRetryUs;
                last_revision = revision;
                if (copied) {
                    // What showed last gives way to the new effect, unless nothing has shown since
                    // the garland came on.
                    cross_span_ms = state.cross_fade_ms();
                    crossing = drawn_since_on && cross_span_ms > 0;
                    if (crossing) {
                        // As long as it was drawn: past that, the targets hold an older, longer chain's
                        // frame, which the new LEDs fade in from dark rather than show.
                        from_count = std::min(frame.size(), limited_count);
                        std::copy(frame.begin(), frame.begin() + static_cast<std::ptrdiff_t>(from_count), s_from.begin());
                        cross_start_us = now_us;
                    }
                    effect_since_us = now_us;
                    effect_time_us = 0;
                    s_renderer.reset();
                }
            }
            // A start of the playlist times its first effect from now, though it was playing already.
            // Back from a pause too: the starts made meanwhile, as many as they were, are not counted
            // past what two bits hold, and an effect stopped that long has its dwell whole again.
            if (now.playlist_mark != playlist_mark || paused) {
                playlist_mark = now.playlist_mark;
                effect_since_us = now_us;
                paused = false;
            }
            // The playlist moves on by itself, never while the light goes, and never over a choice
            // made since: advance_playlist checks that in the same word.
            if (now.playlist && fade.target() != 0 &&
                now_us - effect_since_us >= int64_t{state.playlist_dwell_seconds()} * 1'000'000) {
                uint16_t effects[kMaxPlaylistEffects] = {};
                const int next = playlist_next(effects, state.playlist(effects), now.effect);
                if (next >= 0 && next != now.effect) {
                    state.advance_playlist(now.effect, static_cast<uint16_t>(next), now.playlist_mark);
                }
                effect_since_us = now_us;
            }
            if (frame_render_due(now_us, last_frame_us, kRenderPeriodUs, led_count, limited_count)) {
                effect_time_us += static_cast<uint64_t>(now_us - last_frame_us) * now.speed_x4 / 4;
                last_frame_us = now_us;
                if (now.brightness_percent != gain_percent) {
                    gain_percent = now.brightness_percent;
                    gain = brightness_gain(gain_percent);
                }
                s_renderer.render(s_effect, static_cast<uint32_t>(effect_time_us / 1000), frame);
                if (crossing) {
                    const auto elapsed_ms = static_cast<uint32_t>((now_us - cross_start_us) / 1000);
                    const uint32_t weight = cross_fade_weight(elapsed_ms, cross_span_ms);
                    cross_fade(std::span<const Rgb>(s_from.data(), from_count), frame, weight);
                    crossing = weight < 65536;
                }
                drawn_since_on = true;
                apply_brightness(frame, fade.apply(gain, now_us), targets);
                const uint32_t current_ma = limit_current(targets, CSIGARLAND_POWER_LIMIT_MA);
                limited_count = led_count;
                if (current_ma > CSIGARLAND_POWER_LIMIT_MA && !clamp_reported) {
                    ESP_LOGW(TAG, "clamped %lu mA to %d mA", static_cast<unsigned long>(current_ma),
                             CSIGARLAND_POWER_LIMIT_MA);
                    clamp_reported = true;
                }
            }
            // A long chain takes longer than the dither period to send: it dithers as fast
            // as it can, and its last frame must be latched before the next one starts.
            if (now_us - last_sent_us >= led_count * kLedWireUs + kLatchUs) {
                if (s_dither.step(targets, dithered)) {
                    const bool sent = output.show(dithered);
                    if (!sent) {
                        // It did not go out, so the same frame next time is not "no change".
                        s_dither.resend();
                    }
                    const OutputHealth::Verdict verdict = health.on_frame(sent, static_cast<uint32_t>(now_us / 1000));
                    state.set_output_failed(health.failed() || output.count_refused());
                    if (verdict.report) {
                        ESP_LOGE(TAG, "frames are not reaching the LEDs");
                    }
                    if (verdict.recreate) {
                        ESP_LOGW(TAG, "starting the LED output again: %s", output.restart() ? "done" : "failed");
                        s_dither.resend();
                    }
                    last_sent_us = now_us;
                }
            }
        }
    }
}

}  // namespace

bool start_lighting_task(State& state) {
    return xTaskCreate(lighting_task, "lighting", kStackBytes, &state, kPriority, nullptr) == pdPASS;
}

}  // namespace garland
