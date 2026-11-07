/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The web portal: its routes, its two servers and the reading of requests
 * Contributors: None
 */

#include "portal.h"

#include <cJSON.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "api_logic.h"
#include "auth_logic.h"
#include "effect_catalog.h"
#include "effect_pack_store.h"
#include "esp_check.h"
#include "esp_https_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "json_limits.h"
#include "network.h"
#include "ota.h"
#include "ota_logic.h"
#include "portal_auth.h"
#include "portal_common.h"
#include "portal_effects.h"
#include "portal_events.h"
#include "portal_info.h"
#include "portal_playlist.h"
#include "portal_settings.h"
#include "portal_tls.h"
#include "portal_watch.h"
#include "portal_web.h"
#include "trial_log.h"

namespace garland {
namespace {

const char* const TAG = "portal";

constexpr int kReceiveTimeoutsAllowed = 3;  // Of the server's 5 s each.
constexpr size_t kMaxRequestBytes = 192;
constexpr uint32_t kServerStackBytes = 8192;  // Checking an update's signature runs here.
constexpr uint32_t kSecureServerStackBytes = 11264;  // And a TLS handshake.
// The server's table of handlers, a few dozen bytes each, on both servers. Room for the playlist's.
constexpr uint16_t kMaxRoutes = 56;
// Below the screen's task: decrypting an upload would otherwise leave it no time to show one.
constexpr UBaseType_t kServerPriority = 3;
// Each TLS session costs RAM, and a browser opens several if it may.
constexpr uint16_t kSecureServerSockets = 4;
constexpr uint16_t kPlainServerSockets = 3;
// Each server needs its own, and HTTPD_SSL_CONFIG_DEFAULT does not leave the default's.
constexpr uint16_t kSecureServerControlPort = ESP_HTTPD_DEF_CTRL_PORT + 1;
constexpr uint16_t kPlainServerControlPort = ESP_HTTPD_DEF_CTRL_PORT + 2;
static_assert(kSecureServerControlPort != kPlainServerControlPort, "two servers, two control ports");

// A route of the portal. Open: asks for no paired browser.
struct Route {
    const char* uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t*);
    bool open;
};

State* s_state = nullptr;
httpd_handle_t s_plain_server = nullptr;

// While a route has a request: its headers are in, and what it reads has limits of its own, which it
// gives the watch; once it ends, the next request's headers are timed. See portal_watch.h.
class Handling {
public:
    explicit Handling(httpd_req_t* req) : req_(req) { portal_watch_route_begins(req_); }
    ~Handling() { portal_watch_route_ends(req_); }
    Handling(const Handling&) = delete;
    Handling& operator=(const Handling&) = delete;

private:
    httpd_req_t* req_;
};
const TlsIdentity* s_identity = nullptr;
bool s_secure_running = false;
constexpr uint32_t kTrialNetworkWaitMs = 20000;
constexpr uint32_t kTrialStepMs = 200;
constexpr uint32_t kTrialLogWindowMs = 3000;
SemaphoreHandle_t s_handler_lock = nullptr;
StaticSemaphore_t s_handler_lock_storage;

// user_ctx is the file, already gzipped: every browser takes that, and the flash holds no other form.
esp_err_t handle_portal_file(httpd_req_t* req) {
    const auto* const file = static_cast<const PortalFile*>(req->user_ctx);
    httpd_resp_set_type(req, file->type);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", file->cache);
    return httpd_resp_send(req, reinterpret_cast<const char*>(file->data), static_cast<ssize_t>(file->bytes));
}

esp_err_t handle_ota_get(httpd_req_t* req) {
    const JsonPtr root(cJSON_CreateObject());
    if (root == nullptr) {
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }
    cJSON_AddStringToObject(root.get(), "version", ota_running_version());
    cJSON_AddBoolToObject(root.get(), "on_trial", ota_on_trial());
    cJSON_AddNumberToObject(root.get(), "trial_left", static_cast<double>(ota_trial_seconds_left()));
    cJSON_AddNumberToObject(root.get(), "effects", static_cast<double>(catalog_size()));
    cJSON_AddStringToObject(root.get(), "pack_digest", pack_store_active_digest());
    cJSON_AddStringToObject(root.get(), "image", ota_running_image());
    cJSON_AddStringToObject(root.get(), "boot", ota_boot_id());
    return send_json(req, root.get());
}

// Body: the update file itself, as written by scripts/ota_sign_tool.py.
esp_err_t handle_ota_set(httpd_req_t* req) {
    OtaInstalled installed;
    const char* const problem = ota_receive(req, *static_cast<State*>(req->user_ctx), installed);
    if (problem != nullptr) {
        ESP_LOGW(TAG, "update failed: %s", problem);
        return portal_send_status(req, ota_problem_status(problem), problem);
    }
    // All of it was read, so the connection may stay.
    portal_mark_body_read(req);
    httpd_resp_set_type(req, "application/json");
    char reply[48];
    std::snprintf(reply, sizeof(reply), "{\"firmware\":%s,\"effects\":%s}", installed.firmware ? "true" : "false",
                  installed.effects ? "true" : "false");
    const esp_err_t sent = httpd_resp_sendstr(req, reply);
    ota_restart_soon();
    return sent;
}

esp_err_t handle_trial_log(httpd_req_t* req) {
    return trial_log_handle(req, s_secure_running);
}

// What a program asks first: what it is talking to, and whether it can. Open, holding nothing private.
esp_err_t handle_about(httpd_req_t* req) {
    const JsonPtr root(cJSON_CreateObject());
    if (root == nullptr) {
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }
    cJSON_AddStringToObject(root.get(), "product", "csiGarland");
    cJSON_AddNumberToObject(root.get(), "api", kApiVersion);
    cJSON_AddStringToObject(root.get(), "version", ota_running_version());
    cJSON_AddBoolToObject(root.get(), "on_trial", ota_on_trial());
    cJSON_AddNumberToObject(root.get(), "trial_left", static_cast<double>(ota_trial_seconds_left()));
    cJSON_AddNumberToObject(root.get(), "effects", static_cast<double>(catalog_size()));
    cJSON_AddStringToObject(root.get(), "pack_digest", pack_store_active_digest());
    cJSON_AddStringToObject(root.get(), "image", ota_running_image());
    cJSON_AddStringToObject(root.get(), "boot", ota_boot_id());
    return send_json(req, root.get());
}

constexpr Route kRoutes[] = {
    {"/api/state", HTTP_GET, portal_handle_state, false},
    {"/api/events", HTTP_GET, portal_handle_events, false},
    {"/api/state", HTTP_POST, portal_handle_mode_set, false},
    {"/api/info", HTTP_GET, portal_handle_info, false},
    {"/api/network/name", HTTP_POST, portal_handle_hostname_set, false},
    {"/api/network/certificate", HTTP_POST, portal_handle_certificate_domain_set, false},
    {"/csigarland-root.crt", HTTP_GET, portal_handle_root_certificate, true},
    {"/trial-log", HTTP_GET, handle_trial_log, true},
    {"/api/leds", HTTP_POST, portal_handle_set_leds, false},
    {"/api/leds/power", HTTP_GET, portal_handle_led_power_get, false},
    {"/api/leds/power", HTTP_POST, portal_handle_led_power_set, false},
    {"/api/effects", HTTP_GET, portal_handle_effects, false},
    {"/api/effects/planned", HTTP_GET, portal_handle_planned, false},
    {"/api/effects/file", HTTP_POST, portal_handle_effect_upload, false},
    {"/api/effects/file", HTTP_DELETE, portal_handle_effect_delete, false},
    {"/api/effects/preview", HTTP_POST, portal_handle_effect_preview, false},
    {"/api/effects/store", HTTP_GET, portal_handle_effect_store, false},
    {"/api/effect", HTTP_POST, portal_handle_activate, false},
    {"/api/favorites", HTTP_POST, portal_handle_favorite, false},
    {"/api/wifi", HTTP_GET, portal_handle_wifi_status, false},
    {"/api/wifi", HTTP_POST, portal_handle_wifi_set, false},
    {"/api/wifi/scan", HTTP_GET, portal_handle_wifi_scan, false},
    {"/api/wifi/scan", HTTP_POST, portal_handle_wifi_scan, false},
    {"/api/schedule", HTTP_GET, portal_handle_schedule_get, false},
    {"/api/schedule", HTTP_POST, portal_handle_schedule_set, false},
    {"/api/schedule/timeline", HTTP_GET, portal_handle_schedule_timeline, false},
    {"/api/playlist", HTTP_GET, portal_handle_playlist_get, false},
    {"/api/playlist", HTTP_POST, portal_handle_playlist_set, false},
    {"/api/playlist/play", HTTP_POST, portal_handle_playlist_play, false},
    {"/api/time", HTTP_GET, portal_handle_time_get, false},
    {"/api/time", HTTP_POST, portal_handle_time_set, false},
    {"/api/time/rtc", HTTP_POST, portal_handle_rtc_set, false},
    {"/api/display", HTTP_GET, portal_handle_display_get, false},
    {"/api/display", HTTP_POST, portal_handle_display_set, false},
    {"/api/ota", HTTP_GET, handle_ota_get, false},
    {"/api/ota", HTTP_POST, handle_ota_set, false},
    {"/api/auth/devices", HTTP_GET, auth_handle_devices, false},
    {"/api/auth/forget", HTTP_POST, auth_handle_forget, false},
    {"/api/auth/request", HTTP_POST, auth_handle_request, true},
    {"/api/auth/wait", HTTP_POST, auth_handle_wait, true},
};

// The public API. One wildcard per method stands for all of these in the server's own table, of
// which the two servers have to reserve room. See docs/stubs/PUBLIC-API.md.
constexpr Route kPublicRoutes[] = {
    {"/api/v1/about", HTTP_GET, handle_about, true},
    {"/api/v1/pair/request", HTTP_POST, auth_handle_request, true},
    {"/api/v1/pair/wait", HTTP_POST, auth_handle_wait_for_token, true},
    {"/api/v1/state", HTTP_GET, portal_handle_state, false},
    {"/api/v1/events", HTTP_GET, portal_handle_events, false},
    {"/api/v1/state", HTTP_POST, portal_handle_mode_set, false},
    {"/api/v1/power", HTTP_POST, portal_handle_power, false},
    {"/api/v1/effects", HTTP_GET, portal_handle_effects, false},
    {"/api/v1/effects/play", HTTP_POST, portal_handle_activate, false},
    {"/api/v1/effects/favorite", HTTP_POST, portal_handle_favorite, false},
    {"/api/v1/effects/file", HTTP_GET, portal_handle_effect_read, false},
    {"/api/v1/effects/file", HTTP_POST, portal_handle_effect_upload, false},
    {"/api/v1/effects/file", HTTP_DELETE, portal_handle_effect_delete, false},
    {"/api/v1/effects/preview", HTTP_POST, portal_handle_effect_preview, false},
    {"/api/v1/effects/store", HTTP_GET, portal_handle_effect_store, false},
    {"/api/v1/schedule", HTTP_GET, portal_handle_schedule_get, false},
    {"/api/v1/schedule", HTTP_POST, portal_handle_schedule_set, false},
    {"/api/v1/schedule/timeline", HTTP_GET, portal_handle_schedule_timeline, false},
    {"/api/v1/playlist", HTTP_GET, portal_handle_playlist_get, false},
    {"/api/v1/playlist", HTTP_POST, portal_handle_playlist_set, false},
    {"/api/v1/playlist/play", HTTP_POST, portal_handle_playlist_play, false},
    {"/api/v1/time", HTTP_GET, portal_handle_time_get, false},
    {"/api/v1/time", HTTP_POST, portal_handle_time_set, false},
    {"/api/v1/time/rtc", HTTP_POST, portal_handle_rtc_set, false},
    {"/api/v1/display", HTTP_GET, portal_handle_display_get, false},
    {"/api/v1/display", HTTP_POST, portal_handle_display_set, false},
    {"/api/v1/leds", HTTP_POST, portal_handle_set_leds, false},
    {"/api/v1/leds/power", HTTP_GET, portal_handle_led_power_get, false},
    {"/api/v1/leds/power", HTTP_POST, portal_handle_led_power_set, false},
    {"/api/v1/wifi", HTTP_GET, portal_handle_wifi_status, false},
    {"/api/v1/wifi", HTTP_POST, portal_handle_wifi_set, false},
    {"/api/v1/wifi/scan", HTTP_GET, portal_handle_wifi_scan, false},
    {"/api/v1/wifi/scan", HTTP_POST, portal_handle_wifi_scan, false},
    {"/api/v1/network/name", HTTP_POST, portal_handle_hostname_set, false},
    {"/api/v1/network/certificate", HTTP_POST, portal_handle_certificate_domain_set, false},
    {"/api/v1/firmware", HTTP_GET, handle_ota_get, false},
    {"/api/v1/firmware", HTTP_POST, handle_ota_set, false},
    {"/api/v1/info", HTTP_GET, portal_handle_info, false},
    {"/api/v1/clients", HTTP_GET, auth_handle_devices, false},
    {"/api/v1/clients/forget", HTTP_POST, auth_handle_forget, false},
    {"/api/v1/log", HTTP_GET, public_log_handle, false},
};

// Those no route takes among them, so that a client hears so from the public API's own errors.
constexpr httpd_method_t kPublicMethods[] = {HTTP_GET, HTTP_POST, HTTP_DELETE, HTTP_PUT, HTTP_PATCH};

// The files, the portal's routes, and a wildcard for each method.
static_assert(std::size(kPortalFiles) + std::size(kRoutes) + std::size(kPublicMethods) <= kMaxRoutes, "a route past the limit stops the server");

// Plain HTTP serves the setup network alone: its phone windows refuse our certificate. Anyone
// else is sent to HTTPS before anything they sent is read.
bool must_use_https(httpd_req_t* req) {
    return s_secure_running && req->handle == s_plain_server && !auth_on_setup_network(req);
}

esp_err_t redirect_to_https(httpd_req_t* req) {
    // Nothing it sent is read here, whatever the answer: a body is the client's to send again, to HTTPS.
    portal_mark_body_unread(req);
    char host[80] = "";
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) {
        return portal_end_of_request(req, portal_send_err(req, HTTPD_400_BAD_REQUEST, "no host"));
    }
    const size_t name_length = auth_host_name_length(host);
    if (name_length == 0) {
        return portal_end_of_request(req, portal_send_err(req, HTTPD_400_BAD_REQUEST, "bad host"));
    }
    host[name_length] = '\0';
    // Static: the header keeps the pointer until the reply is out, and the plain server is one task.
    static char s_location[sizeof(host) + 8 + CONFIG_HTTPD_MAX_URI_LEN];
    std::snprintf(s_location, sizeof(s_location), "https://%s%s", host, req->uri);
    // Temporary, and kept by no cache: should HTTPS fail at some later start, a browser has to ask
    // this server again, which then stays to take an update, rather than go on to HTTPS by itself.
    httpd_resp_set_status(req, "307 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", s_location);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return portal_end_of_request(req, httpd_resp_send(req, nullptr, 0));
}

esp_err_t redirect_within_network(httpd_req_t* req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", auth_on_setup_network(req) ? "http://" CSIGARLAND_PORTAL_IP "/network" : "/");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, nullptr, 0);
}

// GET and DELETE take no body here, and one sent with them is refused unread before anything else: the
// server would read the rest itself, in its own task, for as long as the sender takes to send it.
bool refused_for_its_body(httpd_req_t* req, esp_err_t& sent) {
    if (req->content_len == 0 || (req->method != HTTP_GET && req->method != HTTP_DELETE)) {
        return false;
    }
    portal_mark_body_unread(req);
    sent = portal_end_of_request(req, portal_send_err(req, HTTPD_400_BAD_REQUEST, "this request takes no body"));
    return true;
}

// On the setup network any address someone types leads here, and a page under that name could not
// use the API. Sending it to the portal's own address is also how a phone finds the portal.
esp_err_t run_portal_file(httpd_req_t* req) {
    const Handling handling(req);
    esp_err_t refused = ESP_OK;
    if (refused_for_its_body(req, refused)) {
        return refused;
    }
    if (must_use_https(req)) {
        return redirect_to_https(req);
    }
    if (auth_on_setup_network(req) && auth_setup_host_is_foreign(req)) {
        return redirect_within_network(req);
    }
    return handle_portal_file(req);
}

esp_err_t run_one_route(httpd_req_t* req, const Route& route) {
    esp_err_t refused = ESP_OK;
    if (refused_for_its_body(req, refused)) {
        return refused;
    }
    // Unread until a reader took all of it: a handler that answers early, or reads none, as a scan does, has
    // the connection closed, rather than the server read the rest for as long as it comes.
    portal_mark_body_unread(req);
    if (route.handler != handle_trial_log && must_use_https(req)) {
        return redirect_to_https(req);
    }
    // Open ones share nothing and can be sent to by anyone, who must not be able to hold up the
    // rest, an update's report among them, by sending a body slowly.
    const AuthAnswer answer = route.open ? (auth_open_route_allows(req) ? AuthAnswer::Allowed : AuthAnswer::NotPaired)
                                         : auth_check(req);
    const bool allowed = answer == AuthAnswer::Allowed;
    if (allowed && route.open) {
        return portal_end_of_request(req, route.handler(req));
    }
    if (!allowed) {
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        // Refused before a byte of its body is read, which the server then never reads.
        portal_mark_body_unread(req);
        if (answer == AuthAnswer::Unreadable) {
            httpd_resp_set_hdr(req, "Retry-After", "5");
            return portal_end_of_request(
                req, portal_send_status(req, "503 Service Unavailable", "the paired browsers cannot be read now, so try again"));
        }
        return portal_end_of_request(req, portal_send_status(req, "401 Unauthorized", "this browser is not paired"));
    }
    // The handlers share static buffers, as they were written for one server task.
    xSemaphoreTake(s_handler_lock, portMAX_DELAY);
    req->user_ctx = s_state;
    const esp_err_t result = route.handler(req);
    xSemaphoreGive(s_handler_lock);
    return portal_end_of_request(req, result);
}

esp_err_t run_route(httpd_req_t* req) {
    const Handling handling(req);
    return run_one_route(req, *static_cast<const Route*>(req->user_ctx));
}

esp_err_t run_public_route(httpd_req_t* req) {
    const Handling handling(req);
    const size_t length = api_path_length(req->uri);
    bool known_path = false;
    for (const Route& route : kPublicRoutes) {
        if (std::strlen(route.uri) != length || std::strncmp(route.uri, req->uri, length) != 0) {
            continue;
        }
        known_path = true;
        if (route.method == req->method) {
            return run_one_route(req, route);
        }
    }
    if (must_use_https(req)) {
        return redirect_to_https(req);
    }
    // No route reads its body, which the server then never reads.
    portal_mark_body_unread(req);
    return portal_end_of_request(req, known_path ? portal_send_status(req, "405 Method Not Allowed", "this route does not take that method")
                                                 : portal_send_err(req, HTTPD_404_NOT_FOUND, "no such route"));
}

// Phones test for a captive network by requesting known addresses. Answering those
// with a redirect is what makes them open the portal by themselves.
esp_err_t handle_not_found(httpd_req_t* req, httpd_err_code_t) {
    const Handling handling(req);
    // Nothing here reads a body, which the server then never reads either.
    portal_mark_body_unread(req);
    if (must_use_https(req)) {
        return redirect_to_https(req);
    }
    return portal_end_of_request(req, redirect_within_network(req));
}

esp_err_t register_routes(httpd_handle_t server) {
    for (const PortalFile& file : kPortalFiles) {
        const httpd_uri_t route = {.uri = file.uri,
                                   .method = HTTP_GET,
                                   .handler = run_portal_file,
                                   .user_ctx = const_cast<PortalFile*>(&file)};
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &route), TAG, "route %s", route.uri);
    }
    for (const Route& entry : kRoutes) {
        const httpd_uri_t route = {.uri = entry.uri,
                                   .method = entry.method,
                                   .handler = run_route,
                                   .user_ctx = const_cast<Route*>(&entry)};
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &route), TAG, "route %s", route.uri);
    }
    for (const httpd_method_t method : kPublicMethods) {
        const httpd_uri_t route = {.uri = "/api/v1/*", .method = method, .handler = run_public_route, .user_ctx = nullptr};
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &route), TAG, "route %s", route.uri);
    }
    return httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, handle_not_found);
}

// Plain HTTP first. With it up, a firmware that cannot do HTTPS can still report why and take the
// next update, so that an owner with no USB is never left without a way in.
esp_err_t start_plain_server() {
    httpd_config_t plain = HTTPD_DEFAULT_CONFIG();
    plain.stack_size = kServerStackBytes;
    plain.max_uri_handlers = kMaxRoutes;
    plain.uri_match_fn = httpd_uri_match_wildcard;
    plain.max_open_sockets = kPlainServerSockets;
    plain.ctrl_port = kPlainServerControlPort;
    plain.task_priority = kServerPriority;
    plain.lru_purge_enable = true;
    plain.open_fn = portal_watch_open;
    plain.close_fn = portal_watch_close;
    ESP_RETURN_ON_ERROR(httpd_start(&s_plain_server, &plain), TAG, "HTTP server");
    const esp_err_t routed = register_routes(s_plain_server);
    if (routed != ESP_OK) {
        httpd_stop(s_plain_server);
        s_plain_server = nullptr;
    }
    return routed;
}

esp_err_t start_secure_server(State& state) {
    const TlsIdentity* const identity = portal_tls_identity(network_hostname(), state);
    ESP_RETURN_ON_FALSE(identity != nullptr, ESP_FAIL, TAG, "no certificate to serve with");
    s_identity = identity;
    httpd_ssl_config_t secure = HTTPD_SSL_CONFIG_DEFAULT();
    secure.servercert = reinterpret_cast<const uint8_t*>(identity->certificate);
    secure.servercert_len = std::strlen(identity->certificate) + 1;
    secure.prvtkey_pem = reinterpret_cast<const uint8_t*>(identity->key);
    secure.prvtkey_len = std::strlen(identity->key) + 1;
    secure.httpd.stack_size = kSecureServerStackBytes;
    secure.httpd.max_uri_handlers = kMaxRoutes;
    secure.httpd.uri_match_fn = httpd_uri_match_wildcard;
    secure.httpd.max_open_sockets = kSecureServerSockets;
    secure.httpd.ctrl_port = kSecureServerControlPort;
    secure.httpd.task_priority = kServerPriority;
    secure.httpd.lru_purge_enable = true;
    // Kept by the HTTPS server and called once a session's handshake is done.
    secure.httpd.open_fn = portal_watch_open;
    secure.httpd.close_fn = portal_watch_close;
    httpd_handle_t server = nullptr;
    ESP_RETURN_ON_ERROR(httpd_ssl_start(&server, &secure), TAG, "HTTPS server");
    const esp_err_t routed = register_routes(server);
    if (routed != ESP_OK) {
        httpd_ssl_stop(server);
    } else {
        portal_watch_handshakes(server);
    }
    return routed;
}

// On trial: until the network is up, and a moment longer, so that what has been logged can be
// read before anything risky runs, should it not survive.
void let_the_log_out() {
    for (uint32_t waited_ms = 0; waited_ms < kTrialNetworkWaitMs && !network_status().connected; waited_ms += kTrialStepMs) {
        vTaskDelay(pdMS_TO_TICKS(kTrialStepMs));
    }
    ESP_LOGI(TAG, "plain HTTP is up, HTTPS next");
    vTaskDelay(pdMS_TO_TICKS(kTrialLogWindowMs));
}

esp_err_t start_all(State& state) {
    s_state = &state;
    s_handler_lock = xSemaphoreCreateMutexStatic(&s_handler_lock_storage);
    if (!portal_watch_start()) {
        ESP_LOGE(TAG, "no task to watch the sessions: a request that does not come holds a server till its own timeouts");
    }
    ESP_RETURN_ON_ERROR(network_start(state), TAG, "network");
    // Worked out once now, as two servers could both be first to ask, and get different boot ids.
    ota_boot_id();
    ota_running_image();
    auth_load_devices();
    const esp_err_t plain = start_plain_server();
    if (plain != ESP_OK) {
        ESP_LOGE(TAG, "no plain HTTP, so no redirect or captive portal: %s", esp_err_to_name(plain));
    } else if (ota_on_trial()) {
        let_the_log_out();
    }
    const esp_err_t secure = start_secure_server(state);
    if (secure == ESP_OK) {
        s_secure_running = true;
        auth_set_secure_cookies(true);
    } else {
        ESP_LOGE(TAG, "no HTTPS, the portal stays on plain HTTP: %s", esp_err_to_name(secure));
    }
    // Either one can take the next update, which is what matters.
    ESP_RETURN_ON_FALSE(plain == ESP_OK || secure == ESP_OK, ESP_FAIL, TAG, "no server at all");
    ESP_LOGI(TAG, "portal ready");
    return ESP_OK;
}

}  // namespace

// Plain text for the portal's own pages, and {"error": ...} on the public API.
esp_err_t portal_send_status(httpd_req_t* req, const char* status, const char* message) {
    httpd_resp_set_status(req, status);
    if (!api_is_public(req->uri)) {
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, message);
    }
    const JsonPtr root(cJSON_CreateObject());
    cJSON* const error = root != nullptr ? cJSON_AddObjectToObject(root.get(), "error") : nullptr;
    if (error != nullptr) {
        cJSON_AddStringToObject(error, "code", api_error_code(std::atoi(status)));
        cJSON_AddStringToObject(error, "message", message);
    }
    const std::unique_ptr<char, JsonTextDeleter> text(error != nullptr ? cJSON_PrintUnformatted(root.get()) : nullptr);
    httpd_resp_set_type(req, "application/json");
    if (text == nullptr) {
        // Asks for no memory, as there is none, and not through send_json, which fails to here.
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, R"({"error":{"code":"internal","message":"out of memory"}})");
    }
    return httpd_resp_sendstr(req, text.get());
}

esp_err_t portal_send_err(httpd_req_t* req, httpd_err_code_t error, const char* message) {
    if (!api_is_public(req->uri)) {
        return httpd_resp_send_err(req, error, message);
    }
    switch (error) {
        case HTTPD_400_BAD_REQUEST:
            return portal_send_status(req, "400 Bad Request", message);
        case HTTPD_404_NOT_FOUND:
            return portal_send_status(req, "404 Not Found", message);
        case HTTPD_408_REQ_TIMEOUT:
            return portal_send_status(req, "408 Request Timeout", message);
        default:
            return portal_send_status(req, "500 Internal Server Error", message);
    }
}

namespace {

// One request of each server at a time is marked, by its handle: the plain server and the secure one each
// run their requests one after another, each in its own task.
struct UnreadMark {
    std::atomic<httpd_handle_t> server{nullptr};
    std::atomic<httpd_req_t*> request{nullptr};
};
std::array<UnreadMark, 2> s_unread{};

}  // namespace

void portal_mark_body_unread(httpd_req_t* req) {
    if (req->content_len == 0) {
        return;
    }
    for (UnreadMark& mark : s_unread) {
        httpd_handle_t expected = nullptr;
        if (mark.server.load() == req->handle || mark.server.compare_exchange_strong(expected, req->handle)) {
            mark.request.store(req);
            return;
        }
    }
}

void portal_mark_body_read(httpd_req_t* req) {
    for (UnreadMark& mark : s_unread) {
        if (mark.server.load() == req->handle) {
            httpd_req_t* expected = req;
            mark.request.compare_exchange_strong(expected, nullptr);
        }
    }
}

esp_err_t portal_end_of_request(httpd_req_t* req, esp_err_t result) {
    for (UnreadMark& mark : s_unread) {
        if (mark.server.load() == req->handle) {
            if (mark.request.exchange(nullptr) == req) {
                return ESP_FAIL;
            }
        }
    }
    return result;
}

// A body sent a byte now and then never runs into the server's timeout: the whole of it has this.
constexpr int64_t kBodyDeadlineUs = 10 * 1000 * 1000;

bool portal_read_body(httpd_req_t* req, char* body, size_t limit) {
    if (req->content_len == 0) {
        portal_send_err(req, HTTPD_400_BAD_REQUEST, "bad body size");
        return false;
    }
    if (req->content_len > limit) {
        portal_send_status(req, "413 Content Too Large", "bad body size");
        portal_mark_body_unread(req);
        return false;
    }
    body[req->content_len] = '\0';
    size_t received = 0;
    int timeouts = 0;
    const int64_t started = esp_timer_get_time();
    // The watch holds a read that blocks to it too, as one waiting on a TLS record can.
    portal_watch_deadline(req, started + kBodyDeadlineUs);
    while (received < req->content_len) {
        if (esp_timer_get_time() - started > kBodyDeadlineUs) {
            portal_send_err(req, HTTPD_408_REQ_TIMEOUT, "body not received in time");
            portal_mark_body_unread(req);
            return false;
        }
        const int chunk = httpd_req_recv(req, body + received, req->content_len - received);
        // The server answers one request at a time, so a client that announces a body and
        // never sends it must not hold the portal for good.
        if (chunk == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= kReceiveTimeoutsAllowed) {
            continue;
        }
        if (chunk <= 0) {
            portal_send_err(req, HTTPD_400_BAD_REQUEST, "body not received");
            portal_mark_body_unread(req);
            return false;
        }
        received += static_cast<size_t>(chunk);
    }
    portal_watch_deadline(req, 0);
    portal_mark_body_read(req);
    return true;
}

bool portal_read_text(httpd_req_t* req, char* body, size_t limit) {
    if (!portal_read_body(req, body, limit)) {
        return false;
    }
    // Read as a C string from here on, what follows a NUL would never be looked at.
    if (!body_is_text(body, req->content_len)) {
        portal_send_err(req, HTTPD_400_BAD_REQUEST, "the body holds a NUL byte");
        return false;
    }
    return true;
}

JsonPtr portal_read_json(httpd_req_t* req, char* body, size_t limit) {
    if (!portal_read_body(req, body, limit)) {
        return nullptr;
    }
    JsonPtr root(json_parse_body(body, req->content_len));
    if (root == nullptr) {
        portal_send_err(req, HTTPD_400_BAD_REQUEST, "not valid JSON");
    }
    return root;
}

JsonPtr portal_read_json(httpd_req_t* req) {
    char body[kMaxRequestBytes + 1];
    return portal_read_json(req, body, kMaxRequestBytes);
}

bool portal_https_running() {
    return s_secure_running;
}

bool portal_request_is_secure(httpd_req_t* req) {
    return s_plain_server == nullptr || req->handle != s_plain_server;
}

bool start_portal(State& state) {
    // Its failure leaves pages asking now and then, as they did before, and nothing else.
    portal_events_start(state);
    const esp_err_t err = start_all(state);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "portal failed: %s", esp_err_to_name(err));
    }
    return err == ESP_OK;
}

}  // namespace garland
