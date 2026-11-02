/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Hardware-free part of noticing that frames no longer reach the LEDs
 * Contributors: None
 */

#pragma once

#include <cstdint>

namespace garland {

// A few hundred frames go out each second, so one failure says little and a line for each says
// too much. About a quarter of a second of nothing but failures is a failed output.
constexpr uint32_t kOutputFailedAfterFrames = 100;
constexpr uint32_t kOutputRetryMs = 5000;

class OutputHealth {
public:
    struct Verdict {
        bool report = false;   // Say so in the log.
        bool recreate = false; // Start the output again.
    };

    // Time wraps safely.
    Verdict on_frame(bool sent, uint32_t now_ms) {
        Verdict verdict;
        if (sent) {
            failures_ = 0;
            return verdict;
        }
        if (failures_ < kOutputFailedAfterFrames) {
            ++failures_;
        }
        if (failures_ == 1 && !reported_ever_) {
            reported_ever_ = true;
            last_report_ms_ = now_ms;
            verdict.report = true;
        } else if (now_ms - last_report_ms_ >= kOutputRetryMs) {
            last_report_ms_ = now_ms;
            verdict.report = true;
        }
        if (failed() && (!recreated_ever_ || now_ms - last_recreate_ms_ >= kOutputRetryMs)) {
            recreated_ever_ = true;
            last_recreate_ms_ = now_ms;
            verdict.recreate = true;
        }
        return verdict;
    }

    bool failed() const { return failures_ >= kOutputFailedAfterFrames; }

private:
    uint32_t failures_ = 0;
    bool reported_ever_ = false;
    uint32_t last_report_ms_ = 0;
    bool recreated_ever_ = false;
    uint32_t last_recreate_ms_ = 0;
};

}  // namespace garland
