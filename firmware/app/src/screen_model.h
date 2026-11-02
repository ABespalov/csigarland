/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The one inventory of screens: which one is up, and what it shows
 * Contributors: None
 *
 * After csiPass's screen catalog. Each screen has one row, and the row is its behaviour: its
 * colour, whether a screensaver may cover it, what it shows. choose_screen is the only place
 * that decides between screens, and display.cpp only draws the ScreenModel built here, so a
 * new screen or a change of colour is a change to this file. Nothing here touches the panel.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "garland_state.h"

namespace garland {

// In the order choose_screen prefers them.
enum class ScreenId : uint8_t {
    UpdateReceiving,
    UpdateApplying,
    UpdateRestarting,
    MakingKey,
    Access,
    PowerOff,
    SetupNetwork,
    NoLeds,
    Playing,
    Standby,
    Count,
};

// What the accent colour says. docs/architecture.md holds the policy.
enum class ScreenTone : uint8_t {
    Idle,       // Nothing is happening.
    Normal,     // Running, and all is well.
    Attention,  // The owner has something to do, or must not interfere.
    Success,    // An operation has finished well.
};

enum class ScreenIcon : uint8_t { Update, Power, Play, Wifi, Warning };

struct ScreenRow {
    ScreenId id;
    ScreenTone tone;
    ScreenIcon icon;
    const char* title;
    const char* text;    // nullptr: built from the state.
    const char* footer;  // nullptr: the address of the portal.
    bool keeps_awake;    // No screensaver over it.
    bool shows_stats;
    bool shows_progress;
};

constexpr const char* kUpdateFooter = "DO NOT TURN OFF";

inline constexpr std::array<ScreenRow, static_cast<size_t>(ScreenId::Count)> kScreens = {{
    {ScreenId::UpdateReceiving, ScreenTone::Attention, ScreenIcon::Update, "Update", "Receiving update\nPlease wait",
     kUpdateFooter, true, false, true},
    {ScreenId::UpdateApplying, ScreenTone::Attention, ScreenIcon::Update, "Update", "Applying update\nPlease wait",
     kUpdateFooter, true, false, true},
    {ScreenId::UpdateRestarting, ScreenTone::Success, ScreenIcon::Update, "Update", "Update installed\nRestarting",
     kUpdateFooter, true, false, true},
    {ScreenId::MakingKey, ScreenTone::Attention, ScreenIcon::Update, "HTTPS", "Making the\nHTTPS key\nPlease wait", kUpdateFooter, true, false, false},
    {ScreenId::Access, ScreenTone::Attention, ScreenIcon::Warning, "Access", nullptr, nullptr, true, false, false},
    {ScreenId::PowerOff, ScreenTone::Attention, ScreenIcon::Power, "Power off", nullptr, "PRESS TO CANCEL", true, false, false},
    {ScreenId::SetupNetwork, ScreenTone::Attention, ScreenIcon::Wifi, "Wi-Fi setup", nullptr, nullptr, false, true, false},
    {ScreenId::NoLeds, ScreenTone::Attention, ScreenIcon::Warning, "No LEDs", "Garland not\ndetected", nullptr, false, true, false},
    {ScreenId::Playing, ScreenTone::Normal, ScreenIcon::Play, "Playing", nullptr, nullptr, false, true, false},
    {ScreenId::Standby, ScreenTone::Idle, ScreenIcon::Power, "Standby", "Press POWER\nto turn on", nullptr, false, true, false},
}};

constexpr bool screens_are_in_order() {
    for (size_t i = 0; i < kScreens.size(); ++i) {
        if (static_cast<size_t>(kScreens[i].id) != i) {
            return false;
        }
    }
    return true;
}
static_assert(screens_are_in_order(), "kScreens has one row per ScreenId, in order");

constexpr const ScreenRow& screen_row(ScreenId id) {
    return kScreens[static_cast<size_t>(id)];
}

constexpr ScreenId choose_screen(const Snapshot& s) {
    switch (s.update_percent) {
        case kNoUpdate:
            break;
        case kUpdateApplying:
            return ScreenId::UpdateApplying;
        case kUpdateRestarting:
            return ScreenId::UpdateRestarting;
        default:
            return ScreenId::UpdateReceiving;
    }
    if (s.making_key) {
        return ScreenId::MakingKey;
    }
    if (s.pair_display != kPairDisplayNone) {
        return ScreenId::Access;
    }
    if (s.off_seconds != 0) {
        return ScreenId::PowerOff;
    }
    if (s.setup_network) {
        return ScreenId::SetupNetwork;
    }
    if (s.led_setting == kLedCountAuto && s.probe == ProbeStatus::Absent) {
        return ScreenId::NoLeds;
    }
    return s.power_requested ? ScreenId::Playing : ScreenId::Standby;
}

// What the state cannot say by itself. Read only by the screens that show them.
struct ScreenInputs {
    const char* effect_name;
    const char* setup_ssid;
    const char* setup_password;
    const char* portal_ip;
};

constexpr int kScreenStatCount = 3;

// Owns its text, so drawing it reads nothing that may have changed since.
struct ScreenModel {
    ScreenId id = ScreenId::Standby;
    ScreenTone tone = ScreenTone::Idle;
    ScreenIcon icon = ScreenIcon::Power;
    const char* title = "";
    char text[72] = {};
    char footer[24] = {};
    char stats[kScreenStatCount][12] = {};
    uint8_t percent = 0;
    bool shows_stats = false;
    bool shows_progress = false;
    bool shows_clock_glyph = false;
    bool clock_glyph_lit = false;  // A scheduled action comes within a day.
};

inline void fill_screen_text(const Snapshot& s, const ScreenRow& row, const ScreenInputs& inputs, ScreenModel& out) {
    if (row.text != nullptr) {
        std::snprintf(out.text, sizeof(out.text), "%s", row.text);
    } else if (row.id == ScreenId::Access) {
        if (s.pair_display == kPairDisplayForgotten) {
            std::snprintf(out.text, sizeof(out.text), "All browsers\nforgotten");
        } else if (s.pair_display == kPairDisplayForgetFailed) {
            std::snprintf(out.text, sizeof(out.text), "Forgotten until\nrestart: not saved");
        } else {
            std::snprintf(out.text, sizeof(out.text), "Code %04u\nPOWER+SPEED\nto allow", s.pair_display - 1u);
        }
    } else if (row.id == ScreenId::PowerOff) {
        std::snprintf(out.text, sizeof(out.text), "Off in %u s\nPress a button\nto stay on", s.off_seconds);
    } else if (row.id == ScreenId::SetupNetwork) {
        // The only place the board's own password can be read, and only while it is needed.
        std::snprintf(out.text, sizeof(out.text), "%s\nPass %s", inputs.setup_ssid, inputs.setup_password);
    } else {
        std::snprintf(out.text, sizeof(out.text), "%s", inputs.effect_name);
    }
}

inline void fill_screen_footer(const Snapshot& s, const ScreenRow& row, const ScreenInputs& inputs, ScreenModel& out) {
    if (row.id == ScreenId::Access) {
        std::snprintf(out.footer, sizeof(out.footer), "%s", s.pair_display >= kPairDisplayForgetFailed ? "" : "ONE BUTTON: REFUSE");
    } else if (row.footer != nullptr) {
        std::snprintf(out.footer, sizeof(out.footer), "%s", row.footer);
    } else if (s.station_ip != 0) {
        // In network byte order, so the first octet is the lowest byte on this chip.
        std::snprintf(out.footer, sizeof(out.footer), "%u.%u.%u.%u", static_cast<unsigned>(s.station_ip & 0xFF),
                      static_cast<unsigned>((s.station_ip >> 8) & 0xFF), static_cast<unsigned>((s.station_ip >> 16) & 0xFF),
                      static_cast<unsigned>(s.station_ip >> 24));
    } else {
        std::snprintf(out.footer, sizeof(out.footer), "%s", inputs.portal_ip);
    }
}

inline void fill_screen_stats(const Snapshot& s, ScreenModel& out) {
    std::snprintf(out.stats[0], sizeof(out.stats[0]), "%u%%", s.brightness_percent);
    char speed[kSpeedTextBytes];
    speed_to_text(s.speed_x4, speed);
    std::snprintf(out.stats[1], sizeof(out.stats[1]), "%sx", speed);

    // A plain number is the owner's setting; the tilde marks the probe's estimate.
    char* const leds = out.stats[2];
    const size_t room = sizeof(out.stats[2]);
    if (s.led_kept != 0) {
        // What the LEDs are getting, and the mark says it is not what was asked for.
        std::snprintf(leds, room, "!%u", s.led_kept);
    } else if (s.led_setting != kLedCountAuto) {
        std::snprintf(leds, room, "%u", s.led_count);
    } else if (s.probe == ProbeStatus::NotChecked) {
        std::snprintf(leds, room, "...");
    } else if (s.probe == ProbeStatus::Absent) {
        std::snprintf(leds, room, "0");
    } else if (s.led_estimate == 0) {
        std::snprintf(leds, room, "?");
    } else {
        std::snprintf(leds, room, "~%u", s.led_estimate);
    }
}

inline ScreenModel build_screen_model(const Snapshot& s, const ScreenInputs& inputs) {
    const ScreenRow& row = screen_row(choose_screen(s));
    ScreenModel model;
    model.id = row.id;
    model.tone = row.tone;
    model.icon = row.icon;
    model.title = row.title;
    model.percent = update_shown_percent(s.update_percent);
    model.shows_stats = row.shows_stats;
    model.shows_progress = row.shows_progress;
    model.shows_clock_glyph = s.clock_module;
    model.clock_glyph_lit = s.action_soon;
    fill_screen_text(s, row, inputs, model);
    fill_screen_footer(s, row, inputs, model);
    fill_screen_stats(s, model);
    return model;
}

}  // namespace garland
