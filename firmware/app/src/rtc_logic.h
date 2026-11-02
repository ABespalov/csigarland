/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Hardware-free part of the DS3231 clock: its registers and what to believe
 * Contributors: None
 */

#pragma once

#include <cstdint>
#include <cstdio>

namespace garland {

// Seconds, minutes, hours, weekday, date, month with century, year. The part holds UTC.
constexpr int kRtcTimeRegisters = 7;

// 2026-01-01. A clock reading earlier than this firmware's own age is behind by definition.
constexpr int64_t kRtcEarliestEpoch = 1767225600;
// 2100-01-01. The registers end with 2099 here.
constexpr int64_t kRtcLatestEpoch = 4102444800;
// The part has no sub-second output, so a smaller difference from NTP is left alone.
constexpr int64_t kRtcToleratedDriftSeconds = 2;

constexpr bool rtc_epoch_is_valid(int64_t epoch) {
    return epoch >= kRtcEarliestEpoch && epoch < kRtcLatestEpoch;
}

// For what a browser sends: a whole number the clock can hold,
// looked at before it becomes one, as a cast of what does not fit has no defined result.
constexpr bool rtc_epoch_from_number(double value, int64_t& out) {
    if (!(value >= static_cast<double>(kRtcEarliestEpoch) && value < static_cast<double>(kRtcLatestEpoch))) {
        return false;
    }
    const auto whole = static_cast<int64_t>(value);
    if (static_cast<double>(whole) != value) {
        return false;
    }
    out = whole;
    return true;
}

// The oscillator-stopped flag is the part's own word that it lost time, however good the
// date it holds looks.
constexpr bool rtc_is_trusted(bool oscillator_stopped, int64_t epoch) {
    return !oscillator_stopped && rtc_epoch_is_valid(epoch);
}

constexpr bool rtc_needs_correction(bool trusted, int64_t rtc_epoch, int64_t ntp_epoch) {
    const int64_t drift = rtc_epoch > ntp_epoch ? rtc_epoch - ntp_epoch : ntp_epoch - rtc_epoch;
    return !trusted || drift >= kRtcToleratedDriftSeconds;
}

constexpr bool rtc_is_leap_year(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

constexpr int rtc_days_in_month(int year, int month) {
    constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2 && rtc_is_leap_year(year) ? 29 : kDays[month - 1];
}

// Days since 1970-01-01, for 1970 onwards. A loop: it runs seldom, and is plain to check.
constexpr int64_t rtc_days_from_date(int year, int month, int day) {
    int64_t days = day - 1;
    for (int y = 1970; y < year; ++y) {
        days += rtc_is_leap_year(y) ? 366 : 365;
    }
    for (int m = 1; m < month; ++m) {
        days += rtc_days_in_month(year, m);
    }
    return days;
}

constexpr uint8_t rtc_to_bcd(int value) {
    return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

constexpr int rtc_from_bcd(uint8_t value) {
    return (value >> 4) * 10 + (value & 0x0F);
}

constexpr bool rtc_bcd_is_valid(uint8_t value) {
    return (value & 0x0F) <= 9 && (value >> 4) <= 9;
}

// False for registers that are not a time. 12-hour mode is read too: nothing here writes
// it, but a module may arrive set that way by something else.
inline bool rtc_decode(const uint8_t (&registers)[kRtcTimeRegisters], int64_t& epoch) {
    const uint8_t seconds = registers[0] & 0x7F;
    const uint8_t minutes = registers[1] & 0x7F;
    const uint8_t date = registers[4] & 0x3F;
    const uint8_t month_bits = registers[5] & 0x1F;
    if (!rtc_bcd_is_valid(seconds) || !rtc_bcd_is_valid(minutes) || !rtc_bcd_is_valid(date) ||
        !rtc_bcd_is_valid(month_bits) || !rtc_bcd_is_valid(registers[6])) {
        return false;
    }
    int hour = 0;
    if ((registers[2] & 0x40) != 0) {
        const uint8_t hour_bits = registers[2] & 0x1F;
        if (!rtc_bcd_is_valid(hour_bits) || rtc_from_bcd(hour_bits) < 1 || rtc_from_bcd(hour_bits) > 12) {
            return false;
        }
        hour = rtc_from_bcd(hour_bits) % 12 + ((registers[2] & 0x20) != 0 ? 12 : 0);
    } else {
        const uint8_t hour_bits = registers[2] & 0x3F;
        if (!rtc_bcd_is_valid(hour_bits)) {
            return false;
        }
        hour = rtc_from_bcd(hour_bits);
    }
    const int year = 2000 + ((registers[5] & 0x80) != 0 ? 100 : 0) + rtc_from_bcd(registers[6]);
    const int month = rtc_from_bcd(month_bits);
    const int day = rtc_from_bcd(date);
    if (month < 1 || month > 12 || day < 1 || day > rtc_days_in_month(year, month) || hour > 23 ||
        rtc_from_bcd(minutes) > 59 || rtc_from_bcd(seconds) > 59) {
        return false;
    }
    epoch = rtc_days_from_date(year, month, day) * 86400 + hour * 3600 + rtc_from_bcd(minutes) * 60 + rtc_from_bcd(seconds);
    return true;
}

// epoch must satisfy rtc_epoch_is_valid.
inline void rtc_encode(int64_t epoch, uint8_t (&registers)[kRtcTimeRegisters]) {
    const int64_t days = epoch / 86400;
    const int seconds = static_cast<int>(epoch % 86400);
    int year = 1970;
    int64_t left = days;
    while (left >= (rtc_is_leap_year(year) ? 366 : 365)) {
        left -= rtc_is_leap_year(year) ? 366 : 365;
        ++year;
    }
    int month = 1;
    while (left >= rtc_days_in_month(year, month)) {
        left -= rtc_days_in_month(year, month);
        ++month;
    }
    registers[0] = rtc_to_bcd(seconds % 60);
    registers[1] = rtc_to_bcd(seconds / 60 % 60);
    registers[2] = rtc_to_bcd(seconds / 3600);  // Bit 6 clear: 24-hour mode.
    registers[3] = static_cast<uint8_t>((days + 4) % 7 + 1);  // 1970-01-01 was a Thursday; Sunday is 1.
    registers[4] = rtc_to_bcd(static_cast<int>(left) + 1);
    registers[5] = rtc_to_bcd(month);
    registers[6] = rtc_to_bcd(year - 2000);
}

// What a datetime-local field sends: "2026-10-31T18:05", seconds optional. Checks the
// calendar only; the caller turns it into an epoch in the garland's own time zone.
struct LocalDateTime {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
};

inline bool rtc_parse_local(const char* text, LocalDateTime& out) {
    int used = 0;
    LocalDateTime parsed;
    const int fields = std::sscanf(text, "%4d-%2d-%2dT%2d:%2d%n", &parsed.year, &parsed.month, &parsed.day, &parsed.hour,
                                   &parsed.minute, &used);
    if (fields != 5) {
        return false;
    }
    if (text[used] == ':') {
        int more = 0;
        if (std::sscanf(text + used, ":%2d%n", &parsed.second, &more) != 1) {
            return false;
        }
        used += more;
    }
    if (text[used] != '\0' || parsed.year < 2000 || parsed.year > 2099 || parsed.month < 1 || parsed.month > 12 ||
        parsed.day < 1 || parsed.day > rtc_days_in_month(parsed.year, parsed.month) || parsed.hour < 0 || parsed.hour > 23 ||
        parsed.minute < 0 || parsed.minute > 59 || parsed.second < 0 || parsed.second > 59) {
        return false;
    }
    out = parsed;
    return true;
}

}  // namespace garland
