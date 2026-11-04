/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Watches the portal's sessions, and shuts down one whose request does not come in time
 * Contributors: None
 */

#include "portal_watch.h"

#include <sys/socket.h>
#include <unistd.h>

#include <array>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace garland {
namespace {

const char* const TAG = "watch";

// More than the two servers keep open together.
constexpr size_t kSessions = 12;
// More than one server keeps open.
constexpr size_t kClients = 8;
constexpr uint32_t kLookMs = 250;
constexpr uint32_t kTaskStackBytes = 2048;

struct Session {
    httpd_handle_t server = nullptr;
    int fd = -1;
    int64_t deadline_us = 0;
    bool handed_off = false;
};

// Under s_lock: the servers' tasks change them, the watch's reads them and shuts a socket down. A session
// is let go under it before its socket is closed, so a number taken again by another connection is never
// shut down for the one before.
std::array<Session, kSessions> s_sessions{};
// Under s_lock too: the server's clients not opened yet, and when each was first seen so.
struct Pending {
    int fd = -1;
    int64_t since_us = 0;
};
std::array<Pending, kClients> s_pending{};
httpd_handle_t s_handshakes = nullptr;
SemaphoreHandle_t s_lock = nullptr;
StaticSemaphore_t s_lock_storage;

class Guard {
public:
    Guard() {
        if (s_lock != nullptr) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
        }
    }
    ~Guard() {
        if (s_lock != nullptr) {
            xSemaphoreGive(s_lock);
        }
    }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};

Session* find(httpd_handle_t server, int fd) {
    for (Session& session : s_sessions) {
        if (session.fd == fd && session.server == server) {
            return &session;
        }
    }
    return nullptr;
}

Session* find(httpd_req_t* req) {
    return find(req->handle, httpd_req_to_sockfd(req));
}

// Whether the server lists fd among its clients now.
bool listed(const int* fds, size_t count, int fd) {
    for (size_t i = 0; i < count; ++i) {
        if (fds[i] == fd) {
            return true;
        }
    }
    return false;
}

// Under s_lock: the server's clients that have not reached its open_fn, as one in its handshake has not,
// each from when it was first seen so; one past the limit is listed again before it is shut down, so a
// number the server has let go since is never touched.
void look_at_handshakes(int64_t now) {
    if (s_handshakes == nullptr) {
        return;
    }
    int fds[kClients] = {};
    size_t count = kClients;
    if (httpd_get_client_list(s_handshakes, &count, fds) != ESP_OK) {
        return;
    }
    for (Pending& pending : s_pending) {
        if (pending.fd >= 0 && (!listed(fds, count, pending.fd) || find(s_handshakes, pending.fd) != nullptr)) {
            pending = Pending{};
        }
    }
    for (size_t i = 0; i < count; ++i) {
        if (find(s_handshakes, fds[i]) != nullptr) {
            continue;
        }
        Pending* seen = nullptr;
        Pending* free = nullptr;
        for (Pending& pending : s_pending) {
            seen = pending.fd == fds[i] ? &pending : seen;
            free = free == nullptr && pending.fd < 0 ? &pending : free;
        }
        if (seen == nullptr && free != nullptr) {
            *free = Pending{fds[i], now};
        } else if (seen != nullptr && now - seen->since_us > kHandshakeDeadlineUs) {
            int again[kClients] = {};
            size_t listed_now = kClients;
            if (httpd_get_client_list(s_handshakes, &listed_now, again) == ESP_OK && listed(again, listed_now, seen->fd) &&
                find(s_handshakes, seen->fd) == nullptr) {
                ESP_LOGW(TAG, "a TLS handshake did not finish in time: closing its connection");
                shutdown(seen->fd, SHUT_RDWR);
            }
            *seen = Pending{};
        }
    }
}

void watch_task(void*) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(kLookMs));
        const Guard guard;
        const int64_t now = esp_timer_get_time();
        look_at_handshakes(now);
        for (Session& session : s_sessions) {
            if (session.fd >= 0 && session.deadline_us != 0 && now > session.deadline_us) {
                ESP_LOGW(TAG, "a request did not come in time: closing its connection");
                shutdown(session.fd, SHUT_RDWR);
                session.deadline_us = 0;
            }
        }
    }
}

}  // namespace

bool portal_watch_start() {
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    return xTaskCreate(watch_task, "portal_watch", kTaskStackBytes, nullptr, tskIDLE_PRIORITY + 1, nullptr) == pdPASS;
}

esp_err_t portal_watch_open(httpd_handle_t server, int sockfd) {
    const Guard guard;
    for (Session& session : s_sessions) {
        if (session.fd < 0) {
            session = Session{server, sockfd, esp_timer_get_time() + kHeaderDeadlineUs, false};
            return ESP_OK;
        }
    }
    // More than the servers keep open, which they do not let happen: let in, unwatched.
    return ESP_OK;
}

void portal_watch_close(httpd_handle_t server, int sockfd) {
    {
        const Guard guard;
        if (Session* const session = find(server, sockfd)) {
            *session = Session{};
        }
        // A handshake given up on: its time goes with it, never to the next connection given its number.
        if (server == s_handshakes) {
            for (Pending& pending : s_pending) {
                if (pending.fd == sockfd) {
                    pending = Pending{};
                }
            }
        }
    }
    close(sockfd);
}

void portal_watch_deadline(httpd_req_t* req, int64_t deadline_us) {
    const Guard guard;
    Session* const session = find(req);
    if (session != nullptr && !session->handed_off) {
        session->deadline_us = deadline_us;
    }
}

void portal_watch_route_begins(httpd_req_t* req) {
    portal_watch_deadline(req, 0);
}

void portal_watch_route_ends(httpd_req_t* req) {
    portal_watch_deadline(req, esp_timer_get_time() + kHeaderDeadlineUs);
}

void portal_watch_handshakes(httpd_handle_t server) {
    const Guard guard;
    s_handshakes = server;
    s_pending = {};
}

void portal_watch_hand_off(httpd_req_t* req) {
    const Guard guard;
    if (Session* const session = find(req)) {
        session->handed_off = true;
        session->deadline_us = 0;
    }
}

}  // namespace garland
