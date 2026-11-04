/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Push updates: the garland's state sent to pages and programs as it changes
 * Contributors: None
 */

#include "portal_events.h"

#include <array>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "event_stream.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "portal_auth.h"
#include "portal_common.h"
#include "portal_effects.h"
#include "portal_watch.h"

namespace garland {
namespace {

const char* const TAG = "events";

constexpr uint32_t kLookMs = 250;
constexpr uint32_t kStackBytes = 4096;
constexpr uint32_t kLockWaitMs = 100;
constexpr int kStreamSendTimeoutS = 1;
// Below the servers, which this must never hold up.
constexpr UBaseType_t kPriority = tskIDLE_PRIORITY + 2;

// A stream holds its socket for as long as it is open, and the servers have few, each TLS session
// costing memory: two leave the rest for the pages and their requests.
constexpr size_t kMaxStreams = 2;

struct Stream {
    httpd_req_t* req = nullptr;  // The request handed over by httpd_req_async_handler_begin.
    bool fresh = false;          // Not yet sent the state.
    // What it was let in under, held to at every look: a client forgotten, or out of its days, hears
    // nothing more, as a request of its own would be refused.
    AuthGrant grant;
    // When this stream was last sent anything, so each hears its comment within its own 15 s, however
    // often another opens and is sent its first state.
    int64_t sent_us = 0;
    // The socket's own send timeout, given back before the socket goes back to the server.
    timeval server_timeout = {};
    bool timeout_set = false;
};

std::array<Stream, kMaxStreams> s_streams;
SemaphoreHandle_t s_lock = nullptr;
StaticSemaphore_t s_lock_storage;
State* s_state = nullptr;
bool s_started = false;

bool send_text(httpd_req_t* req, const char* text, size_t length) {
    return httpd_resp_send_chunk(req, text, static_cast<ssize_t>(length)) == ESP_OK;
}

// Ends a stream: its last chunk, and the request handed back, which closes nothing if the socket
// has gone already.
// The socket's own send timeout is given back while the stream still holds it, as completing the request
// hands the socket back to the server. The connection is ended while the stream still holds it too, as a
// page or program opens a stream on a connection of its own: shut down both ways, it reads as closed, and
// the server closes its own session of it. Closed by its number after the hand-back instead, it could be
// another connection by then, which the server took in under the same number.
void drop(Stream& stream) {
    const int fd = httpd_req_to_sockfd(stream.req);
    httpd_resp_send_chunk(stream.req, nullptr, 0);
    if (fd >= 0 && stream.timeout_set &&
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &stream.server_timeout, sizeof(stream.server_timeout)) != 0) {
        ESP_LOGW(TAG, "a stream's socket kept its short send timeout");
    }
    if (fd >= 0 && shutdown(fd, SHUT_RDWR) != 0) {
        ESP_LOGW(TAG, "a stream's connection could not be ended: the server closes it when it goes");
    }
    httpd_req_async_handler_complete(stream.req);
    stream = Stream{};
}

void events_task(void*) {
    static char state_json[kStateJsonBytes];
    static char last_json[kStateJsonBytes];
    static char frame[kEventFrameBytes];
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(kLookMs));
        // Only this task empties a slot, and a request only fills an empty one: so the streams are
        // copied under the lock and written to outside it, where a slow reader holds up no request.
        std::array<Stream, kMaxStreams> streams;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        streams = s_streams;
        xSemaphoreGive(s_lock);
        const bool update_running = s_state->snapshot().update_percent != kNoUpdate;
        const int length = portal_state_json(*s_state, state_json, sizeof(state_json));
        const int64_t now_us = esp_timer_get_time();
        const bool changed = length > 0 && std::strcmp(state_json, last_json) != 0;
        const size_t state_frame = length > 0 ? event_frame(frame, sizeof(frame), "state", state_json) : 0;
        std::array<bool, kMaxStreams> ended{};
        std::array<bool, kMaxStreams> told{};
        std::array<bool, kMaxStreams> sent{};
        for (size_t i = 0; i < streams.size(); ++i) {
            Stream& stream = streams[i];
            if (stream.req == nullptr) {
                continue;
            }
            // An update wants every byte of memory and the radio: a page asks again afterwards.
            if (update_running || !auth_grant_holds(stream.grant)) {
                ended[i] = true;
                continue;
            }
            bool alive = true;
            if ((changed || stream.fresh) && state_frame > 0) {
                alive = send_text(stream.req, frame, state_frame);
                told[i] = true;
                sent[i] = true;
            } else if (now_us - stream.sent_us >= int64_t{kEventKeepAliveMs} * 1000) {
                alive = send_text(stream.req, kEventKeepAlive, std::strlen(kEventKeepAlive));
                sent[i] = true;
            }
            ended[i] = !alive;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (size_t i = 0; i < streams.size(); ++i) {
            if (ended[i]) {
                s_streams[i] = Stream{};
            } else {
                if (told[i]) {
                    s_streams[i].fresh = false;
                }
                if (sent[i]) {
                    s_streams[i].sent_us = now_us;
                }
            }
        }
        xSemaphoreGive(s_lock);
        for (size_t i = 0; i < streams.size(); ++i) {
            if (ended[i]) {
                drop(streams[i]);
            }
        }
        if (changed) {
            std::memcpy(last_json, state_json, static_cast<size_t>(length) + 1);
        }

    }
}

}  // namespace

bool portal_events_start(State& state) {
    if (s_started) {
        return true;
    }
    s_state = &state;
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    s_started = xTaskCreate(events_task, "events", kStackBytes, nullptr, kPriority, nullptr) == pdPASS;
    if (!s_started) {
        ESP_LOGE(TAG, "no task for push updates: pages go on asking");
    }
    return s_started;
}

// The request goes on in the events task, so the server is free at once. Its headers go out with the
// first event.
esp_err_t portal_handle_events(httpd_req_t* req) {
    if (!s_started) {
        return portal_send_status(req, "503 Service Unavailable", "No push updates here: ask now and then");
    }
    if (s_state->snapshot().update_percent != kNoUpdate) {
        return portal_send_status(req, "503 Service Unavailable", "An update is under way: ask again once it is over");
    }
    const AuthGrant grant = auth_grant_of(req);
    if (grant.slot == -1) {
        return portal_send_status(req, "401 Unauthorized", "this browser is not paired");
    }
    // The events task holds the lock only to copy or empty a slot: a wait longer than this is not
    // worth holding a server for.
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(kLockWaitMs)) != pdTRUE) {
        return portal_send_status(req, "503 Service Unavailable", "Busy: ask again in a moment");
    }
    Stream* free_stream = nullptr;
    for (Stream& stream : s_streams) {
        if (stream.req == nullptr) {
            free_stream = &stream;
            break;
        }
    }
    if (free_stream == nullptr) {
        xSemaphoreGive(s_lock);
        return portal_send_status(req, "503 Service Unavailable", "Two streams are open already: ask now and then");
    }
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_req_t* handed = nullptr;
    const esp_err_t err = httpd_req_async_handler_begin(req, &handed);
    if (err == ESP_OK) {
        // The stream's socket is the events task's from here, with no deadline of a request's.
        portal_watch_hand_off(handed);
        *free_stream = Stream{handed, true, grant};
        // The events task writes to the streams in turn: a reader that takes nothing for a second loses
        // its stream, rather than holding up the other's for the server's own send timeout.
        const int fd = httpd_req_to_sockfd(handed);
        const timeval send_timeout = {kStreamSendTimeoutS, 0};
        socklen_t length = sizeof(free_stream->server_timeout);
        free_stream->timeout_set = fd >= 0 && getsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &free_stream->server_timeout, &length) == 0 &&
                                   setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout)) == 0;
        if (!free_stream->timeout_set) {
            ESP_LOGW(TAG, "a stream's send timeout could not be set");
        }
    }
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "a stream could not be handed over: %s", esp_err_to_name(err));
        return portal_send_status(req, "503 Service Unavailable", "No room for a stream: ask now and then");
    }
    return ESP_OK;
}

}  // namespace garland
