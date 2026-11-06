/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: What the files of the settings share of NVS: their namespace, and how a reading's answer counts
 * Contributors: None
 */

#pragma once

#include "esp_err.h"
#include "nvs.h"
#include "tls_logic.h"

namespace garland {

// Every setting of the owner's is in this one namespace of the default partition.
inline constexpr char kSettingsNamespace[] = "garland";

// What NVS answered, for stored_read.
constexpr NvsAnswer nvs_answer(esp_err_t err) {
    return err == ESP_OK                         ? NvsAnswer::Ok
           : err == ESP_ERR_NVS_NOT_FOUND        ? NvsAnswer::NotFound
           : err == ESP_ERR_NVS_INVALID_LENGTH   ? NvsAnswer::TooLong
                                                 : NvsAnswer::Failed;
}

}  // namespace garland
