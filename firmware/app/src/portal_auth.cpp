/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Who may use the portal: browsers paired with the panel's buttons
 * Contributors: None
 *
 * Two locks. s_lock guards the request and the list in memory and is only ever held to copy or
 * change them, as button and timer callbacks take it. s_save_lock is held across a change of the
 * list and its flash write, as one transaction, and they never take it. Always in that order.
 */

#include "portal_auth.h"

#include <cJSON.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "auth_logic.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "json_limits.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "network.h"
#include "portal_common.h"
#include "psa/crypto.h"
#include "scheduler.h"
#include "settings.h"

namespace garland {
namespace {

const char* const TAG = "auth";

constexpr char kCookieName[] = "csig";
// Over plain HTTP. A browser will not let plain HTTP replace a Secure cookie of the same name,
// which it may hold from when HTTPS worked, and would say nothing about it.
constexpr char kPlainCookieName[] = "csigh";
constexpr char kPlainCookieCleared[] = "csigh=; Max-Age=0; Path=/";
constexpr uint32_t kForgottenNoticeUs = 3000000;
constexpr uint32_t kForgetTaskStackBytes = 4096;
constexpr size_t kMaxBodyBytes = 192;

State* s_state = nullptr;
SemaphoreHandle_t s_lock = nullptr;
StaticSemaphore_t s_lock_storage;
SemaphoreHandle_t s_save_lock = nullptr;
StaticSemaphore_t s_save_lock_storage;
AuthDevices s_devices{};
AuthDevices s_to_save{};
// The list in flash is there and could not be read: the one in memory knows none of it. Under s_save_lock
// once the servers are up.
std::atomic<bool> s_unread{false};
constexpr int kReadAttempts = 3;
constexpr uint32_t kReadRetryMs = 50;
Pairing s_pairing;
esp_timer_handle_t s_clear_timer = nullptr;
constexpr size_t kMaxOriginBytes = 96;  // A portal's own is far shorter.
char s_pending_label[kAuthLabelBytes] = "";
bool s_secure_cookies = false;
// Under s_lock: TLS can recover its domain read after plain HTTP has started.
AuthDomain s_domain;

class Guard {
public:
    explicit Guard(SemaphoreHandle_t lock = s_lock) : lock_(lock) { xSemaphoreTake(lock_, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(lock_); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;

private:
    SemaphoreHandle_t lock_;
};

uint32_t now_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

int64_t now_epoch() {
    return time_is_synced() ? static_cast<int64_t>(std::time(nullptr)) : 0;
}

bool hash_of(const uint8_t* token, std::array<uint8_t, kAuthHashBytes>& out) {
    size_t length = 0;
    return psa_hash_compute(PSA_ALG_SHA_256, token, kAuthTokenBytes, out.data(), out.size(), &length) == PSA_SUCCESS &&
           length == out.size();
}

// The list in flash, read again where it could not be, and taken in place of the one in memory, which knew
// none of it. Until it is, nothing is written over it: an empty list saved there would forget every browser
// paired. True once it is read, or was. The caller holds s_save_lock.
bool list_is_read() {
    if (!s_unread.load()) {
        return true;
    }
    if (settings_load_auth(s_to_save.data(), s_to_save.size()) == StoredRead::Unreadable) {
        // Not logged: a request comes here each time, and the start said so once.
        return false;
    }
    {
        Guard guard;
        s_devices = s_to_save;
    }
    s_unread.store(false);
    ESP_LOGI(TAG, "the paired browsers were read late");
    return true;
}

// Writes the list as it is in memory now. The caller holds s_save_lock.
esp_err_t save_current() {
    if (!list_is_read()) {
        return ESP_ERR_INVALID_STATE;
    }
    {
        Guard guard;
        s_to_save = s_devices;
    }
    const esp_err_t err = settings_save_auth(s_to_save.data(), s_to_save.size());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving failed: %s", esp_err_to_name(err));
    }
    return err;
}

// Whether a request that came with the cookie came from this portal's own pages: auth_cookie_origin_ok.
bool cookie_origin_ok(httpd_req_t* req) {
    char host[kMaxOriginBytes];
    char origin[kMaxOriginBytes];
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) {
        return false;
    }
    const bool secure = portal_request_is_secure(req);
    const size_t origin_bytes = httpd_req_get_hdr_value_len(req, "Origin");
    if (origin_bytes == 0) {
        return auth_cookie_origin_ok(nullptr, host, secure);
    }
    return origin_bytes < sizeof(origin) && httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) == ESP_OK &&
           auth_cookie_origin_ok(origin, host, secure);
}

// The paired client a request comes from, and the hash of its token into hash_out if given, or -1. A
// program gives its token as a bearer, alone; a browser its HTTPS cookie, and while auth_plain_cookie_counts
// the one plain HTTP gave it, should HTTPS have failed for a while, each tried in turn: an old HTTPS cookie
// of a pairing forgotten or out of its days, which the browser still sends, does not hide the pairing it
// made on plain HTTP since.
// unread_out, if given, says whether a token was looked for in a list not read yet: taken with the look, so
// that a list read by another request since cannot turn this one's refusal into "not paired".
int find_caller(httpd_req_t* req, std::array<uint8_t, kAuthHashBytes>* hash_out = nullptr, bool* unread_out = nullptr) {
    if (unread_out != nullptr) {
        *unread_out = false;
    }
    // A browser paired before comes back once the list can be read, with no restart.
    if (s_unread.load()) {
        Guard saving(s_save_lock);
        list_is_read();
    }
    // "Bearer " and the token.
    char header[kAuthTokenBytes * 2 + 1 + 8] = "";
    const char* bearer = nullptr;
    if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) == ESP_OK) {
        bearer = auth_bearer_token(header);
    }
    if (bearer == nullptr && !cookie_origin_ok(req)) {
        return -1;
    }
    const char* const cookies[] = {kCookieName, kPlainCookieName};
    const bool plain_counts = auth_plain_cookie_counts(s_secure_cookies, s_secure_cookies && auth_on_setup_network(req));
    const size_t tries = bearer != nullptr || !plain_counts ? 1 : std::size(cookies);
    const int64_t now = now_epoch();
    bool settled = false;
    int found = -1;
    for (size_t i = 0; i < tries && found < 0; ++i) {
        char text[kAuthTokenBytes * 2 + 1] = "";
        size_t size = sizeof(text);
        if (bearer != nullptr) {
            const size_t length = std::strlen(bearer);
            if (length >= sizeof(text)) {
                return -1;
            }
            std::memcpy(text, bearer, length + 1);
        } else if (httpd_req_get_cookie_val(req, cookies[i], text, &size) != ESP_OK) {
            continue;
        }
        uint8_t token[kAuthTokenBytes];
        std::array<uint8_t, kAuthHashBytes> hash{};
        if (!auth_from_hex(text, token, sizeof(token)) || !hash_of(token, hash)) {
            continue;
        }
        {
            Guard guard;
            settled = auth_settle_clock(s_devices, now) || settled;
            found = auth_find(s_devices, hash, now);
            // Under the guard the list is taken in under, and cleared after it: a list still unread here is
            // the one just looked in.
            if (found < 0 && unread_out != nullptr && s_unread.load()) {
                *unread_out = true;
            }
        }
        if (hash_out != nullptr) {
            *hash_out = hash;
        }
    }
    if (settled) {
        // Not worth failing a request over: the days start again at the next try if this is lost.
        Guard saving(s_save_lock);
        save_current();
    }
    return found;
}

bool comes_from_this_portal(httpd_req_t* req) {
    char origin[kMaxOriginBytes];
    char host[kMaxOriginBytes];
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK ||
        !auth_host_is_own(host, CSIGARLAND_PORTAL_IP, network_hostname())) {
        return false;
    }
    const size_t origin_bytes = httpd_req_get_hdr_value_len(req, "Origin");
    if (origin_bytes == 0) {
        return true;
    }
    return origin_bytes < sizeof(origin) && httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) == ESP_OK &&
           auth_origin_is_own(origin, host, portal_request_is_secure(req));
}

void show_nothing(void*) {
    Guard guard;
    s_pairing.expire(now_ms());
    if (!s_pairing.waiting(now_ms())) {
        s_state->set_pair_display(kPairDisplayNone);
    }
}

void arm_clear_timer(uint64_t after_us) {
    if (s_clear_timer != nullptr) {
        esp_timer_stop(s_clear_timer);
        esp_timer_start_once(s_clear_timer, after_us);
    }
}

// As every route reads a body: held to its size, and to the time the whole of it may take to come.
bool read_json(httpd_req_t* req, JsonPtr& out) {
    char body[kMaxBodyBytes + 1];
    out = portal_read_json(req, body, kMaxBodyBytes);
    return out != nullptr;
}

esp_err_t send_state(httpd_req_t* req, const char* state) {
    char body[32];
    std::snprintf(body, sizeof(body), "{\"state\":\"%s\"}", state);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}

enum class MadeDevice : uint8_t { Made, Revoked, Failed };

// A token counts from the moment it is safely in flash, so the browser never holds one that a
// restart would forget. The approval it comes from must have outlived any forgetting: one that
// follows this waits for s_save_lock and takes this browser with the rest.
MadeDevice make_device(uint32_t approved_generation, const char (&label)[kAuthLabelBytes], char (&token_text)[kAuthTokenBytes * 2 + 1]) {
    uint8_t token[kAuthTokenBytes];
    esp_fill_random(token, sizeof(token));
    std::array<uint8_t, kAuthHashBytes> hash{};
    if (!hash_of(token, hash)) {
        return MadeDevice::Failed;
    }
    const int64_t now = now_epoch();
    Guard saving(s_save_lock);
    // A browser added to a list not read would be saved over the rest of it.
    if (!list_is_read()) {
        return MadeDevice::Failed;
    }
    size_t slot = 0;
    {
        Guard guard;
        if (s_pairing.generation() != approved_generation) {
            return MadeDevice::Revoked;
        }
        s_to_save = s_devices;
        slot = auth_slot_for_new(s_to_save, now);
        AuthDevice& device = s_to_save[slot];
        device = AuthDevice{};
        device.in_use = true;
        device.hash = hash;
        device.paired_epoch = now;
        device.expires_epoch = now != 0 ? now + kAuthValiditySeconds : 0;
        std::memcpy(device.label, label, sizeof(device.label));
    }
    const esp_err_t err = settings_save_auth(s_to_save.data(), s_to_save.size());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving failed: %s", esp_err_to_name(err));
        return MadeDevice::Failed;
    }
    {
        Guard guard;
        s_devices[slot] = s_to_save[slot];
    }
    auth_to_hex(token, sizeof(token), token_text);
    return MadeDevice::Made;
}

// Access ends in memory whatever the flash says, and the owner hears if a restart could bring
// it back.
void forget_task(void*) {
    esp_err_t err = ESP_OK;
    {
        Guard saving(s_save_lock);
        {
            Guard guard;
            s_devices = AuthDevices{};
            s_pairing.revoke();
        }
        // The owner asked for none, which needs nothing of the list that could not be read.
        s_unread.store(false);
        err = save_current();
    }
    s_state->set_pair_display(err == ESP_OK ? kPairDisplayForgotten : kPairDisplayForgetFailed);
    arm_clear_timer(kForgottenNoticeUs);
    ESP_LOGW(TAG, "every browser forgotten%s", err == ESP_OK ? "" : ", until the next start");
    vTaskDelete(nullptr);
}

}  // namespace

void auth_init(State& state) {
    s_state = &state;
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    s_save_lock = xSemaphoreCreateMutexStatic(&s_save_lock_storage);
    const esp_timer_create_args_t timer = {.callback = show_nothing, .arg = nullptr, .dispatch_method = ESP_TIMER_TASK, .name = "auth", .skip_unhandled_events = false};
    if (esp_timer_create(&timer, &s_clear_timer) != ESP_OK) {
        s_clear_timer = nullptr;
    }
}

void auth_set_secure_cookies(bool secure) {
    s_secure_cookies = secure;
}

void auth_load_devices() {
    psa_crypto_init();
    StoredRead read = StoredRead::Unreadable;
    for (int attempt = 0; attempt < kReadAttempts && read == StoredRead::Unreadable; ++attempt) {
        if (attempt > 0) {
            vTaskDelay(pdMS_TO_TICKS(kReadRetryMs));
        }
        read = settings_load_auth(s_to_save.data(), s_to_save.size());
    }
    if (read == StoredRead::Unreadable) {
        ESP_LOGE(TAG, "the paired browsers cannot be read: none is let in, and none saved over them, until they are");
    }
    s_unread.store(read == StoredRead::Unreadable);
    {
        Guard guard;
        s_devices = s_to_save;
    }
    // Plain HTTP starts before TLS, whose own retries may recover the domain later.
    char loaded_domain[kTlsDomainBytes] = "";
    StoredRead domain = StoredRead::Unreadable;
    for (int attempt = 0; attempt < kReadAttempts && domain == StoredRead::Unreadable; ++attempt) {
        domain = settings_load_tls_domain(loaded_domain, sizeof(loaded_domain));
    }
    auth_take_domain(domain, loaded_domain);
}

void auth_take_domain(StoredRead read, const char* domain) {
    Guard guard;
    s_domain.take(read, domain);
}

bool auth_request_host_is_garland(httpd_req_t* req) {
    char host[80] = "";
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) {
        return false;
    }
    char station[16] = "";
    const uint32_t ip = s_state->snapshot().station_ip;
    if (ip != 0) {
        std::snprintf(station, sizeof(station), "%u.%u.%u.%u", static_cast<unsigned>(ip & 0xFF),
                      static_cast<unsigned>((ip >> 8) & 0xFF), static_cast<unsigned>((ip >> 16) & 0xFF),
                      static_cast<unsigned>(ip >> 24));
    }
    const char* const hostname = network_hostname();
    Guard guard;
    return auth_host_is_garland(host, CSIGARLAND_PORTAL_IP, hostname, s_domain.name, station);
}

void auth_clear_plain_cookie(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Set-Cookie", kPlainCookieCleared);
}

bool auth_request_carries_plain_cookie(httpd_req_t* req) {
    const size_t bytes = httpd_req_get_hdr_value_len(req, "Cookie");
    if (bytes == 0) {
        return false;
    }
    // Longer than this is not read whole: clearing is the safe side, as the cookie is not accepted anyway.
    if (bytes >= 512) {
        return true;
    }
    char cookies[512];
    return httpd_req_get_hdr_value_str(req, "Cookie", cookies, sizeof(cookies)) != ESP_OK || std::strstr(cookies, "csigh=") != nullptr;
}

bool auth_on_setup_network(httpd_req_t* req) {
    const int socket = httpd_req_to_sockfd(req);
    sockaddr_in local = {};
    sockaddr_in peer = {};
    socklen_t local_length = sizeof(local);
    socklen_t peer_length = sizeof(peer);
    if (getsockname(socket, reinterpret_cast<sockaddr*>(&local), &local_length) != 0 || local.sin_family != AF_INET ||
        getpeername(socket, reinterpret_cast<sockaddr*>(&peer), &peer_length) != 0 || peer.sin_family != AF_INET) {
        return false;
    }
    const NetworkStatus network = network_status();
    const uint32_t portal = inet_addr(CSIGARLAND_PORTAL_IP);
    return auth_setup_network_applies(network.access_point_on, network.ip, portal) &&
           auth_is_setup_client(local.sin_addr.s_addr, peer.sin_addr.s_addr, portal);
}

bool auth_setup_host_is_foreign(httpd_req_t* req) {
    char host[kMaxOriginBytes];
    return httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK ||
           !auth_host_is_own(host, CSIGARLAND_PORTAL_IP, network_hostname());
}

bool auth_open_route_allows(httpd_req_t* req) {
    return !auth_on_setup_network(req) || comes_from_this_portal(req);
}

AuthAnswer auth_check(httpd_req_t* req) {
    if (auth_on_setup_network(req)) {
        return comes_from_this_portal(req) ? AuthAnswer::Allowed : AuthAnswer::NotPaired;
    }
    bool unread = false;
    if (find_caller(req, nullptr, &unread) >= 0) {
        return AuthAnswer::Allowed;
    }
    return unread ? AuthAnswer::Unreadable : AuthAnswer::NotPaired;
}

AuthGrant auth_grant_of(httpd_req_t* req) {
    AuthGrant grant;
    if (auth_on_setup_network(req)) {
        grant.slot = comes_from_this_portal(req) ? kAuthGrantSetupNetwork : -1;
        return grant;
    }
    grant.slot = find_caller(req, &grant.hash);
    return grant;
}

bool auth_grant_holds(const AuthGrant& grant) {
    if (grant.slot == kAuthGrantSetupNetwork) {
        // Only while that network is up: once the garland is on a home network, pairing counts.
        return s_state != nullptr && s_state->snapshot().setup_network;
    }
    const int64_t now = now_epoch();
    bool settled = false;
    bool holds = false;
    {
        Guard guard;
        // A pair given without the clock starts its days here as at any request, or an open stream
        // would keep it out of time for good.
        settled = auth_settle_clock(s_devices, now);
        holds = auth_grant_holds(s_devices, grant, now);
    }
    if (settled) {
        Guard saving(s_save_lock);
        save_current();
    }
    return holds;
}

// Body: {"label": "Windows Chrome"}. The answer holds the id only this browser will know.
esp_err_t auth_handle_request(httpd_req_t* req) {
    JsonPtr root;
    if (!read_json(req, root)) {
        return ESP_OK;
    }
    std::array<uint8_t, kPairRequestBytes> id{};
    esp_fill_random(id.data(), id.size());
    const auto code = static_cast<uint16_t>(esp_random() % 10000);
    bool started = false;
    uint32_t retry_ms = 0;
    {
        Guard guard;
        started = s_pairing.start(id, code, now_ms());
        retry_ms = started ? 0 : s_pairing.retry_after_ms(now_ms());
        if (started) {
            const cJSON* label = cJSON_GetObjectItemCaseSensitive(root.get(), "label");
            auth_clean_label(cJSON_IsString(label) ? label->valuestring : "Browser", s_pending_label);
            s_state->set_pair_display(pair_display_for_code(code));
        }
    }
    if (!started) {
        // Here until the reply is out below, which is as long as the header keeps the pointer.
        char retry[12];
        std::snprintf(retry, sizeof(retry), "%u", static_cast<unsigned>(retry_ms / 1000 + 1));
        httpd_resp_set_hdr(req, "Retry-After", retry);
        return portal_send_status(req, "429 Too Many Requests", "Another request is waiting, or one was just refused.");
    }
    arm_clear_timer(static_cast<uint64_t>(kPairWaitMs) * 1000);
    ESP_LOGI(TAG, "a browser asks for access");
    char id_text[kPairRequestBytes * 2 + 1];
    auth_to_hex(id.data(), id.size(), id_text);
    char code_text[8];
    std::snprintf(code_text, sizeof(code_text), "%04u", code);
    const JsonPtr reply(cJSON_CreateObject());
    if (reply == nullptr) {
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }
    cJSON_AddStringToObject(reply.get(), "request", id_text);
    cJSON_AddStringToObject(reply.get(), "code", code_text);
    cJSON_AddNumberToObject(reply.get(), "seconds", kPairWaitMs / 1000);
    return send_json(req, reply.get());
}

namespace {

// Body: {"request": "..."}. POST, as an id does not belong in a URL or its logs. A browser gets the
// token as a cookie its pages cannot read, and a program in the answer, as it has nowhere else.
esp_err_t handle_wait(httpd_req_t* req, bool token_in_answer) {
    JsonPtr root;
    if (!read_json(req, root)) {
        return ESP_OK;
    }
    const cJSON* request = cJSON_GetObjectItemCaseSensitive(root.get(), "request");
    std::array<uint8_t, kPairRequestBytes> id{};
    if (!cJSON_IsString(request) || !auth_from_hex(request->valuestring, id.data(), id.size())) {
        return send_state(req, "gone");
    }
    PairPoll poll = PairPoll::Gone;
    uint32_t generation = 0;
    // Taken with the approval: once it has been given, the next request may overwrite the original.
    char label[kAuthLabelBytes];
    {
        Guard guard;
        poll = s_pairing.poll(id, now_ms());
        generation = s_pairing.generation();
        std::memcpy(label, s_pending_label, sizeof(label));
    }
    if (poll == PairPoll::Waiting) {
        return send_state(req, "waiting");
    }
    if (poll == PairPoll::Gone) {
        return send_state(req, "gone");
    }
    char token_text[kAuthTokenBytes * 2 + 1];
    const MadeDevice made = make_device(generation, label, token_text);
    if (made == MadeDevice::Revoked) {
        return send_state(req, "gone");
    }
    if (made == MadeDevice::Failed) {
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not save this browser: ask again");
    }
    if (token_in_answer) {
        const JsonPtr reply(cJSON_CreateObject());
        if (reply == nullptr) {
            return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
        }
        cJSON_AddStringToObject(reply.get(), "state", "approved");
        cJSON_AddStringToObject(reply.get(), "token", token_text);
        ESP_LOGI(TAG, "a program was paired");
        return send_json(req, reply.get());
    }
    // Secure where the request came over TLS, which is all but the setup network, or everything
    // when HTTPS has failed. Here until the reply is sent below, as long as the header needs it.
    const bool secure = s_secure_cookies && !auth_on_setup_network(req);
    char cookie[sizeof(kPlainCookieName) + kAuthTokenBytes * 2 + 80];
    std::snprintf(cookie, sizeof(cookie), "%s=%s; Max-Age=%lld; Path=/; HttpOnly; SameSite=Strict%s", secure ? kCookieName : kPlainCookieName,
                  token_text, static_cast<long long>(kAuthValiditySeconds), secure ? "; Secure" : "");
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    ESP_LOGI(TAG, "a browser was paired");
    return send_state(req, "approved");
}

}  // namespace

esp_err_t auth_handle_wait(httpd_req_t* req) {
    return handle_wait(req, false);
}

esp_err_t auth_handle_wait_for_token(httpd_req_t* req) {
    return handle_wait(req, true);
}

esp_err_t auth_handle_devices(httpd_req_t* req) {
    const JsonPtr root(cJSON_CreateObject());
    cJSON* list = root != nullptr ? cJSON_AddArrayToObject(root.get(), "devices") : nullptr;
    if (list == nullptr) {
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }
    const int caller = find_caller(req);
    const int64_t now = now_epoch();
    {
        Guard guard;
        for (size_t i = 0; i < s_devices.size(); ++i) {
            if (!auth_device_is_current(s_devices[i], now)) {
                continue;
            }
            cJSON* item = cJSON_CreateObject();
            if (item == nullptr) {
                break;
            }
            cJSON_AddNumberToObject(item, "id", static_cast<double>(i));
            cJSON_AddStringToObject(item, "label", s_devices[i].label);
            cJSON_AddNumberToObject(item, "paired", static_cast<double>(s_devices[i].paired_epoch));
            cJSON_AddNumberToObject(item, "expires", static_cast<double>(s_devices[i].expires_epoch));
            cJSON_AddBoolToObject(item, "this", static_cast<int>(i) == caller);
            cJSON_AddItemToArray(list, item);
        }
    }
    return send_json(req, root.get());
}

// Body: {"id": 3}. Access ends in memory either way; the answer says whether it ended for good.
esp_err_t auth_handle_forget(httpd_req_t* req) {
    JsonPtr root;
    if (!read_json(req, root)) {
        return ESP_OK;
    }
    const cJSON* id = cJSON_GetObjectItemCaseSensitive(root.get(), "id");
    if (!cJSON_IsNumber(id) || id->valueint < 0 || id->valuedouble != id->valueint ||
        static_cast<size_t>(id->valueint) >= s_devices.size()) {
        return portal_send_err(req, HTTPD_400_BAD_REQUEST, "no such browser");
    }
    esp_err_t err = ESP_OK;
    {
        Guard saving(s_save_lock);
        // Read first: the list that could not be read is the one this browser is in.
        if (!list_is_read()) {
            return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "the paired browsers cannot be read now, so try again");
        }
        {
            Guard guard;
            s_devices[static_cast<size_t>(id->valueint)] = AuthDevice{};
        }
        err = save_current();
    }
    if (err != ESP_OK) {
        return portal_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "forgotten for now, but not saved: a restart could bring it back, so try again");
    }
    return auth_handle_devices(req);
}

bool auth_request_is_waiting() {
    Guard guard;
    return s_pairing.waiting(now_ms());
}

bool auth_buttons_approve() {
    Guard guard;
    const bool approved = s_pairing.approve(now_ms());
    if (approved) {
        s_state->set_pair_display(kPairDisplayNone);
    }
    return approved;
}

void auth_button_refuse() {
    Guard guard;
    s_pairing.refuse(now_ms());
    s_state->set_pair_display(kPairDisplayNone);
    ESP_LOGW(TAG, "request refused");
}

void auth_forget_all_soon() {
    // Flash writes do not belong in the callbacks' timer task.
    if (xTaskCreate(forget_task, "auth_forget", kForgetTaskStackBytes, nullptr, tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "no task to forget browsers with");
        s_state->set_pair_display(kPairDisplayForgetFailed);
        arm_clear_timer(kForgottenNoticeUs);
    }
}

}  // namespace garland
