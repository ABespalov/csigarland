/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Draws every screen of the garland to a file, for the documents
 * Contributors: None
 *
 * After csiPass's screenshot tool. It draws nothing itself: it gives a State the firmware's own
 * class to src/screen_model.h and src/display_view.cpp, which the garland uses too, with a display
 * in memory in place of the panel. So a picture can show nothing the firmware would not, and
 * this fails when a screen has no picture. Each file is kScreenSize squared, RGB, three bytes a
 * pixel, which deploy/screens_to_png.py turns into a PNG. Run by deploy/new-doc-images.ps1.
 */

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

#include "display_view.h"
#include "garland_state.h"
#include "network_logic.h"
#include "screen_model.h"

using namespace garland;

namespace {

constexpr size_t kPixels = static_cast<size_t>(kScreenSize) * kScreenSize;
constexpr uint32_t kSampleAddress = 192u | 168u << 8 | 1u << 16 | 50u << 24;  // 192.168.1.50, in network order.

std::array<uint16_t, kPixels> s_draw;
std::array<uint16_t, kPixels> s_frame;

uint32_t now_ms() {
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels) {
    const auto* from = reinterpret_cast<const uint16_t*>(pixels);
    const int width = lv_area_get_width(area);
    for (int y = area->y1; y <= area->y2; ++y) {
        for (int x = 0; x < width; ++x) {
            s_frame[static_cast<size_t>(y) * kScreenSize + area->x1 + x] = *from++;
        }
    }
    lv_display_flush_ready(display);
}

bool write_frame(const std::string& path) {
    FILE* const file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    bool written = true;
    for (const uint16_t pixel : s_frame) {
        const uint8_t rgb[3] = {static_cast<uint8_t>((pixel >> 11) * 255 / 31), static_cast<uint8_t>(((pixel >> 5) & 0x3F) * 255 / 63),
                                static_cast<uint8_t>((pixel & 0x1F) * 255 / 31)};
        written = written && std::fwrite(rgb, sizeof(rgb), 1, file) == 1;
    }
    return std::fclose(file) == 0 && written;
}

// A garland playing, on the home network, with its 32 LEDs detected. Each picture starts from this.
void set_up(State& state) {
    state.set_effect_count(3);
    state.set_led_setting(kLedCountAuto);
    state.set_probe(ProbeStatus::Present, 32);
    state.set_station_ip(kSampleAddress);
    state.set_brightness(75);
    state.set_speed(kNormalSpeed);
    state.set_update_percent(kNoUpdate);
    state.set_clock_module(true);
    state.set_action_soon(true);
    state.set_power(true);
}

struct Picture {
    const char* name;
    ScreenId id;
    void (*change)(State&);
};

const Picture kPictures[] = {
    {"playing", ScreenId::Playing, [](State&) {}},
    {"standby", ScreenId::Standby, [](State& state) {
         state.set_power(false);
         state.set_action_soon(false);
     }},
    {"standby-action-soon", ScreenId::Standby, [](State& state) { state.set_power(false); }},
    {"setup-network", ScreenId::SetupNetwork, [](State& state) {
         state.set_station_ip(0);
         state.set_setup_network(true);
     }},
    {"no-leds", ScreenId::NoLeds, [](State& state) { state.set_probe(ProbeStatus::Absent, 0); }},
    {"access", ScreenId::Access, [](State& state) { state.set_pair_display(pair_display_for_code(4827)); }},
    {"power-off", ScreenId::PowerOff, [](State& state) {
         state.button_down(ButtonId::Power);
         state.long_press_power(0);
     }},
    {"update-receiving", ScreenId::UpdateReceiving, [](State& state) { state.set_update_percent(42); }},
    {"update-applying", ScreenId::UpdateApplying, [](State& state) { state.set_update_percent(kUpdateApplying); }},
    {"update-restarting", ScreenId::UpdateRestarting, [](State& state) { state.set_update_percent(kUpdateRestarting); }},
    {"making-key", ScreenId::MakingKey, [](State& state) { state.set_making_key(true); }},
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: screens <folder> <version>\n");
        return 2;
    }
    const std::string folder = argv[1];
    const std::string label = argv[2];

    lv_init();
    lv_tick_set_cb(now_ms);
    lv_display_t* const display = lv_display_create(kScreenSize, kScreenSize);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, s_draw.data(), nullptr, sizeof(s_draw), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    display_view_build(label.c_str());

    // A sample, in place of the one each board makes for itself.
    const ScreenInputs inputs = {"Combination", kApSsid, "SAMPLE-PASS", "192.168.4.1"};
    std::array<bool, static_cast<size_t>(ScreenId::Count)> drawn{};
    for (const Picture& picture : kPictures) {
        State state(32);
        set_up(state);
        picture.change(state);
        const ScreenModel model = build_screen_model(state.snapshot(), inputs);
        if (model.id != picture.id) {
            std::fprintf(stderr, "%s: the state chose another screen\n", picture.name);
            return 1;
        }
        drawn[static_cast<size_t>(model.id)] = true;
        display_view_draw(model);
        lv_obj_invalidate(display_view_root());
        lv_refr_now(display);
        if (!write_frame(folder + "/" + picture.name + ".rgb")) {
            std::fprintf(stderr, "%s: could not write\n", picture.name);
            return 1;
        }
    }
    for (size_t i = 0; i < drawn.size(); ++i) {
        if (!drawn[i]) {
            std::fprintf(stderr, "the screen \"%s\" has no picture\n", screen_row(static_cast<ScreenId>(i)).title);
            return 1;
        }
    }
    return 0;
}
