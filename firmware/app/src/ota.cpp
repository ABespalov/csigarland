/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Installs signed firmware updates and keeps a bad one from sticking
 * Contributors: None
 */

#include "ota.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>

#include "effect_pack_store.h"
#include "esp_app_desc.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/oid.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "ota_logic.h"
#include "ota_root_certificate.h"
#include "psa/crypto.h"

namespace garland {
namespace {

const char* const TAG = "ota";

constexpr size_t kChunkBytes = 1460;
constexpr int kReceiveTimeoutsAllowed = 3;
constexpr uint32_t kRestartDelayMs = 1500;
constexpr int kConfirmAttempts = 5;
constexpr uint32_t kConfirmRetryMs = 5000;
constexpr uint32_t kTaskStackBytes = 3072;
constexpr size_t kDigestBytes = kOtaDigestBytes;
constexpr int kLightingWaitMs = 1500;  // Longer than a probe, the longest it is ever busy.
constexpr int kLightingWaitStepMs = 5;

std::atomic<bool> s_restarting{false};

bool read_exact(httpd_req_t* req, uint8_t* out, size_t length) {
    size_t received = 0;
    int timeouts = 0;
    while (received < length) {
        const int chunk = httpd_req_recv(req, reinterpret_cast<char*>(out) + received, length - received);
        if (chunk == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= kReceiveTimeoutsAllowed) {
            continue;
        }
        if (chunk <= 0) {
            return false;
        }
        timeouts = 0;
        received += static_cast<size_t>(chunk);
    }
    return true;
}

// The file's certificate, parsed and freed with this object.
class SigningCertificate {
public:
    SigningCertificate() { mbedtls_x509_crt_init(&certificate_); }
    ~SigningCertificate() { mbedtls_x509_crt_free(&certificate_); }
    SigningCertificate(const SigningCertificate&) = delete;
    SigningCertificate& operator=(const SigningCertificate&) = delete;

    // Issued by the root this firmware holds, and meant for signing code. Validity dates are
    // not checked: a board may have no clock yet, and ESP-IDF's mbedTLS is built without them.
    bool load_and_check(const uint8_t* der, size_t length) {
        mbedtls_x509_crt root;
        mbedtls_x509_crt_init(&root);
        uint32_t flags = 0;
        const bool good =
            mbedtls_x509_crt_parse_der(&root, kOtaRootCertificate, sizeof(kOtaRootCertificate)) == 0 &&
            mbedtls_x509_crt_parse_der(&certificate_, der, length) == 0 &&
            mbedtls_x509_crt_verify(&certificate_, &root, nullptr, nullptr, &flags, nullptr, nullptr) == 0 &&
            mbedtls_x509_crt_check_extended_key_usage(&certificate_, MBEDTLS_OID_CODE_SIGNING,
                                                      MBEDTLS_OID_SIZE(MBEDTLS_OID_CODE_SIGNING)) == 0;
        mbedtls_x509_crt_free(&root);
        if (!good) {
            ESP_LOGW(TAG, "certificate refused, flags %lx", static_cast<unsigned long>(flags));
        }
        return good;
    }

    psa_status_t check_signature(const std::array<uint8_t, kDigestBytes>& digest, const OtaHeader& header) {
        psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
        psa_key_id_t key = 0;
        if (mbedtls_pk_get_psa_attributes(&certificate_.pk, PSA_KEY_USAGE_VERIFY_HASH, &attributes) != 0) {
            return PSA_ERROR_GENERIC_ERROR;
        }
        // The default policy is deterministic ECDSA, which refuses a check asked for as plain
        // ECDSA. The signature has the same form either way, so the policy names what is used.
        constexpr psa_algorithm_t algorithm = PSA_ALG_ECDSA(PSA_ALG_SHA_256);
        psa_set_key_algorithm(&attributes, algorithm);
        const int imported = mbedtls_pk_import_into_psa(&certificate_.pk, &attributes, &key);
        psa_reset_key_attributes(&attributes);
        if (imported != 0) {
            return PSA_ERROR_GENERIC_ERROR;
        }
        const psa_status_t status = psa_verify_hash(key, algorithm, digest.data(), digest.size(),
                                                    header.signature.data(), header.signature.size());
        psa_destroy_key(key);
        return status;
    }

private:
    mbedtls_x509_crt certificate_;
};

// How far the whole file has come, across its parts.
struct Progress {
    State& state;
    uint64_t total = 0;
    uint64_t done = 0;

    void add(size_t bytes) {
        done += bytes;
        state.set_update_percent(ota_percent(done, total));
    }
};

// Passes one part of the file to write, and holds it to the digest the signed header gives.
// write returns whether it took the bytes.
template <typename Write>
const char* receive_part(httpd_req_t* req, uint32_t bytes, const std::array<uint8_t, kDigestBytes>& expected,
                         Progress& progress, Write&& write) {
    // Static: too large for the server's stack, and the server handles one request at a time.
    static std::array<uint8_t, kChunkBytes> s_chunk;
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    if (psa_hash_setup(&hash, PSA_ALG_SHA_256) != PSA_SUCCESS) {
        return "could not start checking the file";
    }
    size_t written = 0;
    int timeouts = 0;
    while (written < bytes) {
        const size_t wanted = std::min<size_t>(s_chunk.size(), bytes - written);
        const int chunk = httpd_req_recv(req, reinterpret_cast<char*>(s_chunk.data()), wanted);
        if (chunk == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= kReceiveTimeoutsAllowed) {
            continue;
        }
        if (chunk <= 0) {
            psa_hash_abort(&hash);
            return "the connection was lost";
        }
        timeouts = 0;
        const auto length = static_cast<size_t>(chunk);
        if (!write(s_chunk.data(), length) || psa_hash_update(&hash, s_chunk.data(), length) != PSA_SUCCESS) {
            psa_hash_abort(&hash);
            return "could not write the update";
        }
        written += length;
        progress.add(length);
    }
    std::array<uint8_t, kDigestBytes> digest{};
    size_t digest_length = 0;
    if (psa_hash_finish(&hash, digest.data(), digest.size(), &digest_length) != PSA_SUCCESS ||
        digest_length != digest.size()) {
        return "could not check the file";
    }
    return digest == expected ? nullptr : "this file was changed after signing";
}

const char* receive_pack(httpd_req_t* req, const OtaHeader& header, Progress& progress) {
    const char* problem = pack_store_begin(header.pack_bytes);
    if (problem != nullptr) {
        return problem;
    }
    problem = receive_part(req, header.pack_bytes, header.pack_digest, progress, [](const uint8_t* data, size_t length) {
        return pack_store_write(data, length) == nullptr;
    });
    return problem != nullptr ? problem : pack_store_check(header.pack_digest.data(), header.image_bytes == 0);
}

const char* receive_image(httpd_req_t* req, const esp_partition_t* slot, const OtaHeader& header, Progress& progress) {
    esp_ota_handle_t handle = 0;
    const esp_err_t begun = esp_ota_begin(slot, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (begun != ESP_OK) {
        ESP_LOGE(TAG, "begin failed: %s", esp_err_to_name(begun));
        return "could not start the update";
    }
    const char* const problem =
        receive_part(req, header.image_bytes, header.image_digest, progress, [&](const uint8_t* data, size_t length) {
            const esp_err_t err = esp_ota_write(handle, data, length);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "write failed: %s", esp_err_to_name(err));
            }
            return err == ESP_OK;
        });
    if (problem != nullptr) {
        esp_ota_abort(handle);
        return problem;
    }
    // Checks the image's own structure and hash, and that it is for this chip.
    const esp_err_t ended = esp_ota_end(handle);
    if (ended != ESP_OK) {
        ESP_LOGE(TAG, "image refused: %s", esp_err_to_name(ended));
        return "the image is damaged or not for this board";
    }
    return nullptr;
}

// Before any part is written, so a stranger's file costs no flash wear.
const char* check_signed_header(httpd_req_t* req, const OtaHeader& header, const uint8_t* raw_header) {
    static std::array<uint8_t, kOtaMaxCertificateBytes> s_certificate;
    if (psa_crypto_init() != PSA_SUCCESS || !read_exact(req, s_certificate.data(), header.certificate_bytes)) {
        return "could not read the certificate";
    }
    SigningCertificate signer;
    if (!signer.load_and_check(s_certificate.data(), header.certificate_bytes)) {
        return "this file was not signed by a certificate this garland trusts";
    }
    std::array<uint8_t, kDigestBytes> digest{};
    size_t digest_length = 0;
    if (psa_hash_compute(PSA_ALG_SHA_256, raw_header, kOtaSignedBytes, digest.data(), digest.size(), &digest_length) !=
        PSA_SUCCESS) {
        return "could not check the file";
    }
    const psa_status_t status = signer.check_signature(digest, header);
    if (status == PSA_ERROR_INVALID_SIGNATURE) {
        ESP_LOGW(TAG, "update refused: bad signature");
        return "the signature is not valid: this file was changed after signing";
    }
    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "signature check failed: %ld", static_cast<long>(status));
        return "could not check the signature";
    }
    return nullptr;
}

// A frame in flight finishes first. After a while the update goes on regardless: a lighting task
// that does not answer must not keep a closed garland from the firmware that would fix it. It risks
// little: RMT's interrupt runs through flash writes, so a frame in flight still comes out whole.
void wait_for_lighting(const State& state) {
    for (int waited_ms = 0; !state.lighting_paused(); waited_ms += kLightingWaitStepMs) {
        if (waited_ms >= kLightingWaitMs) {
            ESP_LOGW(TAG, "the lighting task did not pause");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(kLightingWaitStepMs));
    }
}

const char* install(httpd_req_t* req, State& state, OtaInstalled& installed) {
    static std::array<uint8_t, kOtaHeaderBytes> s_header;
    if (req->content_len < s_header.size() || !read_exact(req, s_header.data(), s_header.size())) {
        return "not a csiGarland update file";
    }
    const esp_partition_t* slot = esp_ota_get_next_update_partition(nullptr);
    if (slot == nullptr) {
        return "no update slot: this board needs one flash over USB first";
    }
    OtaHeader header;
    const char* problem =
        ota_parse_header(s_header.data(), req->content_len, slot->size, pack_store_capacity(), header);
    if (problem == nullptr) {
        problem = check_signed_header(req, header, s_header.data());
    }
    if (problem != nullptr) {
        return problem;
    }
    // A restart now would count against the new firmware, and any update ends in one.
    if (ota_on_trial()) {
        return "the running firmware is still on trial: try again in a minute";
    }

    state.set_update_percent(0);
    wait_for_lighting(state);
    Progress progress{state, uint64_t{header.pack_bytes} + header.image_bytes};
    if (header.pack_bytes > 0) {
        problem = receive_pack(req, header, progress);
    }
    if (problem == nullptr && header.image_bytes > 0) {
        problem = receive_image(req, slot, header, progress);
    }
    if (problem != nullptr) {
        return problem;
    }
    state.set_update_percent(kUpdateApplying);
    // Only now, with every part in place and checked. Should the power go between these two,
    // the old firmware runs on with the new pack and refuses whichever effects it cannot play.
    // Should the switch fail instead, the pack is taken back, as nothing was installed.
    if (header.pack_bytes > 0) {
        problem = pack_store_commit();
        if (problem != nullptr) {
            return problem;
        }
    }
    if (header.image_bytes > 0) {
        const esp_err_t boot = esp_ota_set_boot_partition(slot);
        if (boot != ESP_OK) {
            ESP_LOGE(TAG, "boot switch failed: %s", esp_err_to_name(boot));
            if (header.pack_bytes > 0 && pack_store_revert() != nullptr) {
                return "could not switch to the update: the firmware stays, but its effects change at the next start";
            }
            return "could not switch to the update";
        }
    }
    installed.firmware = header.image_bytes > 0;
    installed.effects = header.pack_bytes > 0;
    state.set_update_percent(kUpdateRestarting);
    ESP_LOGI(TAG, "update installed: pack %lu bytes, image %lu bytes", static_cast<unsigned long>(header.pack_bytes),
             static_cast<unsigned long>(header.image_bytes));
    return nullptr;
}

void restart_task(void*) {
    vTaskDelay(pdMS_TO_TICKS(kRestartDelayMs));
    esp_restart();
}

// From the start of a trial until the flash has been seen to hold its end, whatever it says meanwhile.
std::atomic<bool> s_trial_open{false};
// When it began, in ms since the start. Stored before the trial is opened, so it is there when read.
std::atomic<uint32_t> s_trial_start_ms{0};

uint32_t uptime_ms() {
    return static_cast<uint32_t>(pdTICKS_TO_MS(xTaskGetTickCount()));
}

OtaRecord running_record() {
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    const esp_err_t err = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state);
    if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_NOT_SUPPORTED) {
        return OtaRecord::Absent;
    }
    if (err != ESP_OK) {
        return OtaRecord::Unreadable;
    }
    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        return OtaRecord::Pending;
    }
    return state == ESP_OTA_IMG_VALID ? OtaRecord::Valid : OtaRecord::Other;
}

bool confirm_held() {
    const esp_partition_t* const boot = esp_ota_get_boot_partition();
    const bool boots_this_image = boot != nullptr && boot->address == esp_ota_get_running_partition()->address;
    return ota_confirm_held(running_record(), boots_this_image);
}

// A firmware still on trial refuses every update, so one that cannot leave trial must not stay on it:
// a closed garland would have no way on. It restarts instead, which goes back to the previous firmware.
void trial_task(void*) {
    vTaskDelay(pdMS_TO_TICKS(kOtaTrialSeconds * 1000));
    for (int attempt = 0; attempt < kConfirmAttempts; ++attempt) {
        const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK && confirm_held()) {
            s_trial_open.store(false);
            ESP_LOGI(TAG, "update confirmed");
            vTaskDelete(nullptr);
        }
        ESP_LOGE(TAG, "could not confirm the update: %s", err == ESP_OK ? "the flash does not hold it" : esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(kConfirmRetryMs));
    }
    ESP_LOGE(TAG, "still on trial with no way to confirm: restarting, to go back");
    ota_restart_soon();
    vTaskDelete(nullptr);
}

// Marking it returns only if that failed. A restart with the trial not passed goes back all the same.
void go_back_now() {
    const esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
    ESP_LOGE(TAG, "could not mark this firmware invalid: %s, restarting", esp_err_to_name(err));
    esp_restart();
}

}  // namespace

const char* ota_receive(httpd_req_t* req, State& state, OtaInstalled& installed) {
    // A second update in that moment would start by erasing the slot the first has just written.
    if (s_restarting.load()) {
        return "the garland is restarting: send this again after that";
    }
    const char* const problem = install(req, state, installed);
    if (problem != nullptr) {
        // The screen stays on the update message only when a restart follows.
        state.set_update_percent(kNoUpdate);
    }
    return problem;
}

void ota_restart_soon() {
    s_restarting.store(true);
    if (xTaskCreate(restart_task, "ota_restart", kTaskStackBytes, nullptr, tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
        // Whatever needed the restart is done and answered, and the screen says one is coming.
        ESP_LOGE(TAG, "no task to wait in: restarting now");
        esp_restart();
    }
}

size_t ota_slot_bytes() {
    return esp_ota_get_running_partition()->size;
}

size_t ota_running_image_bytes() {
    // Reading the image's headers takes a moment, and the answer cannot change until a restart.
    static size_t s_bytes = 0;
    if (s_bytes == 0) {
        const esp_partition_t* const running = esp_ota_get_running_partition();
        const esp_partition_pos_t position = {.offset = running->address, .size = running->size};
        esp_image_metadata_t metadata = {};
        if (esp_image_get_metadata(&position, &metadata) == ESP_OK) {
            s_bytes = metadata.image_len;
        }
    }
    return s_bytes;
}

const char* ota_running_version() {
    return esp_app_get_description()->version;
}

const char* ota_running_image() {
    static char s_text[65];
    if (s_text[0] == '\0') {
        const uint8_t* const digest = esp_app_get_description()->app_elf_sha256;
        for (size_t i = 0; i < 32; ++i) {
            std::snprintf(s_text + i * 2, 3, "%02x", digest[i]);
        }
    }
    return s_text;
}

const char* ota_boot_id() {
    static char s_text[9];
    if (s_text[0] == '\0') {
        // On first use, with the radio up, as the numbers are truly random only then.
        std::snprintf(s_text, sizeof(s_text), "%08lx", static_cast<unsigned long>(esp_random()));
    }
    return s_text;
}

bool ota_on_trial() {
    return s_trial_open.load() || ota_record_means_trial(running_record());
}

uint32_t ota_trial_seconds_left() {
    if (s_trial_open.load()) {
        return ota_trial_seconds_left(s_trial_start_ms.load(), uptime_ms(), kOtaTrialSeconds);
    }
    // On trial and not yet started it: all of it is still to come.
    return ota_on_trial() ? kOtaTrialSeconds : 0;
}

void ota_start_trial(bool healthy) {
    if (!ota_on_trial()) {
        return;
    }
    if (!healthy) {
        ESP_LOGE(TAG, "new firmware did not start whole, going back");
        go_back_now();
    }
    s_trial_start_ms.store(uptime_ms());
    s_trial_open.store(true);
    ESP_LOGW(TAG, "new firmware on trial for %lu s", static_cast<unsigned long>(kOtaTrialSeconds));
    // Without the task it would stay on trial and refuse every update, so it goes back as well.
    if (xTaskCreate(trial_task, "ota_trial", kTaskStackBytes, nullptr, tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "no task to confirm with, going back");
        go_back_now();
    }
}

}  // namespace garland
