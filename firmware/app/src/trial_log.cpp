/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The log of a new firmware on trial, readable over the network
 * Contributors: None
 */

#include "trial_log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "api_logic.h"
#include "effect_pack_store.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ota.h"
#include "portal_common.h"

namespace garland {
namespace {

constexpr size_t kKeptBytes = 6144;
constexpr size_t kLineBytes = 200;
constexpr size_t kHeadBytes = 256;

// From the heap, once, as only a firmware on trial, or one whose log someone has read, pays for it.
char* s_text = nullptr;
size_t s_used = 0;
// All that was ever taken in, so that a reader can say where it got to.
uint32_t s_total = 0;
// Lines that found the buffer being read, which are not in s_total and no reader would hear of.
std::atomic<uint32_t> s_missed{0};
SemaphoreHandle_t s_lock = nullptr;
StaticSemaphore_t s_lock_storage;
// Copied under s_lock and sent without it, so a slow reader does not hold up a line being logged.
// s_send_lock keeps the copy of one reader while another waits, the two servers each having a task. Made with
// s_text, at the first look, so that, as it, it costs nothing until someone reads.
char* s_send = nullptr;
SemaphoreHandle_t s_send_lock = nullptr;
StaticSemaphore_t s_send_lock_storage;
// What ESP-IDF starts with, for a line logged between setting the hook and hearing what it replaced.
vprintf_like_t s_original = vprintf;

// Never waits, and gives up in an interrupt: losing a line beats holding up who logged it.
int keep_line(const char* format, va_list args) {
    va_list again;
    va_copy(again, args);
    if (!xPortInIsrContext() && xSemaphoreTake(s_lock, 0) == pdTRUE) {
        static std::array<char, kLineBytes> s_line;
        const int written = std::vsnprintf(s_line.data(), s_line.size(), format, again);
        if (written > 0) {
            const size_t length = std::min(static_cast<size_t>(written), s_line.size() - 1);
            if (s_used + length > kKeptBytes) {
                // Keeps the newer half, and with it how the story ends.
                const size_t kept = std::min(s_used, kKeptBytes / 2);
                std::memmove(s_text, s_text + s_used - kept, kept);
                s_used = kept;
            }
            std::memcpy(s_text + s_used, s_line.data(), length);
            s_used += length;
            s_total += static_cast<uint32_t>(length);
        }
        xSemaphoreGive(s_lock);
    } else {
        s_missed.fetch_add(1);
    }
    va_end(again);
    return s_original(format, args);
}

bool start_keeping() {
    if (s_text != nullptr) {
        return true;
    }
    s_text = static_cast<char*>(heap_caps_malloc(kKeptBytes, MALLOC_CAP_8BIT));
    s_send = static_cast<char*>(heap_caps_malloc(kKeptBytes, MALLOC_CAP_8BIT));
    if (s_text == nullptr || s_send == nullptr) {
        heap_caps_free(s_text);
        heap_caps_free(s_send);
        s_text = nullptr;
        s_send = nullptr;
        return false;
    }
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    s_send_lock = xSemaphoreCreateMutexStatic(&s_send_lock_storage);
    s_original = esp_log_set_vprintf(keep_line);
    return true;
}

// The bytes to send, copied under the log's lock and held in s_send until release_send. False when the
// copy could not be taken: the caller answers 503. missed, when given, takes the lines missed and starts their
// count again: only the paired reader's, which says them in X-Log-Missed. The open trial log leaves the count
// to it, or anyone reading that would hide from it the lines it missed.
bool copy_for_send(uint32_t after, uint32_t& total, uint32_t& lost, uint32_t* missed, size_t& bytes) {
    if (s_send_lock == nullptr || xSemaphoreTake(s_send_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }
    if (s_lock == nullptr || xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        xSemaphoreGive(s_send_lock);
        return false;
    }
    total = s_total;
    const LogWindow window = log_window(after, total, s_used);
    lost = window.lost;
    if (missed != nullptr) {
        *missed = s_missed.exchange(0);
    }
    bytes = s_used - window.skip;
    if (bytes > 0) {
        std::memcpy(s_send, s_text + window.skip, bytes);
    }
    xSemaphoreGive(s_lock);
    return true;
}

void release_send() {
    if (s_send_lock != nullptr) {
        xSemaphoreGive(s_send_lock);
    }
}

esp_err_t log_busy(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Retry-After", "1");
    return portal_send_status(req, "503 Service Unavailable", "the log is being read: try again");
}

}  // namespace

void trial_log_start() {
    if (ota_on_trial()) {
        start_keeping();
    }
}

// Query: ?after=<position>. Keeping starts at the first look, so it costs nothing until someone
// reads, and what came before then is not there. The answer's headers say where it got to, and how
// much the reader missed by coming back late.
esp_err_t public_log_handle(httpd_req_t* req) {
    if (!start_keeping()) {
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory to keep the log");
    }
    uint32_t after = 0;
    char query[32];
    char value[12];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK && httpd_query_key_value(query, "after", value, sizeof(value)) == ESP_OK) {
        after = static_cast<uint32_t>(std::strtoul(value, nullptr, 10));
    }
    uint32_t total = 0;
    uint32_t lost_bytes = 0;
    uint32_t missed_lines = 0;
    size_t bytes = 0;
    if (!copy_for_send(after, total, lost_bytes, &missed_lines, bytes)) {
        return log_busy(req);
    }
    // Here until the reply is out, as long as the headers need them.
    char position[12];
    char lost[12];
    std::snprintf(position, sizeof(position), "%lu", static_cast<unsigned long>(total));
    std::snprintf(lost, sizeof(lost), "%lu", static_cast<unsigned long>(lost_bytes));
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Log-Position", position);
    httpd_resp_set_hdr(req, "X-Log-Lost", lost);
    httpd_resp_set_hdr(req, "X-Log-Boot", ota_boot_id());
    char missed[12];
    std::snprintf(missed, sizeof(missed), "%lu", static_cast<unsigned long>(missed_lines));
    httpd_resp_set_hdr(req, "X-Log-Missed", missed);
    // Lines logged meanwhile are lost rather than waited for, as with the trial's log below.
    const esp_err_t err = httpd_resp_send(req, s_send, static_cast<ssize_t>(bytes));
    release_send();
    return err;
}

esp_err_t trial_log_handle(httpd_req_t* req, bool https_running) {
    const bool on_trial = ota_on_trial();
    if (!on_trial && https_running) {
        return httpd_resp_send_404(req);
    }
    char head[kHeadBytes];
    std::snprintf(head, sizeof(head), "version=%s on_trial=%d https=%d boot=%s image=%s pack=%s\n", ota_running_version(),
                  on_trial ? 1 : 0, https_running ? 1 : 0, ota_boot_id(), ota_running_image(), pack_store_active_digest());
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send_chunk(req, head, HTTPD_RESP_USE_STRLEN);
    // Outside a trial the status line is the whole answer. The kept log stays for a paired client.
    if (err == ESP_OK && trial_log_sends_kept_log(on_trial) && s_lock != nullptr) {
        uint32_t total = 0;
        uint32_t lost_bytes = 0;
        size_t bytes = 0;
        // The head is sent already, with its status: a reader that cannot have the log now is told so in it.
        if (!copy_for_send(0, total, lost_bytes, nullptr, bytes)) {
            err = httpd_resp_send_chunk(req, "(the log is being read: try again)\n", HTTPD_RESP_USE_STRLEN);
            return err == ESP_OK ? httpd_resp_send_chunk(req, nullptr, 0) : err;
        }
        if (bytes > 0) {
            err = httpd_resp_send_chunk(req, s_send, static_cast<ssize_t>(bytes));
        }
        release_send();
    }
    return err == ESP_OK ? httpd_resp_send_chunk(req, nullptr, 0) : err;
}

}  // namespace garland
