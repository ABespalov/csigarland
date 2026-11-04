/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The portal's root, key and certificate, made on the board and kept in encrypted NVS
 * Contributors: None
 *
 * The keys are born here and never leave, so no firmware image or update file carries them, and
 * no two boards share them. Made whenever they are missing, and again only if they turn out
 * unusable. Everything else is a certificate on those same keys: the root's when the domain
 * changes or its dates run out, the portal's when its names change or its dates run out.
 */

#include "portal_tls.h"

#include <array>
#include <atomic>
#include <cstring>
#include <ctime>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/oid.h"
#include "mbedtls/pem.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#include "scheduler.h"
#include "settings.h"
#include "source_date.h"

namespace garland {
namespace {

const char* const TAG = "tls";

constexpr char kRootSubject[] = "CN=csiGarland root,O=csiGarland";
constexpr char kPortalSubject[] = "CN=csiGarland portal,O=csiGarland";
constexpr char kNameConstraintsOid[] = "\x55\x1D\x1E";  // 2.5.29.30
constexpr size_t kMaxNames = 3;
constexpr uint32_t kMakerStackBytes = 8192;
constexpr int kReadAttempts = 3;
constexpr uint32_t kReadRetryMs = 200;

TlsIdentity s_identity{};
TlsStarts s_starts{};
char s_domain[kTlsDomainBytes] = "";
char s_wanted_names[kTlsNamesBytes] = "";
int64_t s_now_epoch = 0;
TaskHandle_t s_waiting_task = nullptr;
bool s_made = false;

// Says which step failed and how, as nobody may be there to debug it.
bool passed(int result, const char* step) {
    if (result != 0) {
        ESP_LOGE(TAG, "%s failed: %d (-0x%04x)", step, result, static_cast<unsigned>(-result));
    }
    return result == 0;
}

bool make_key(char (&pem)[kTlsKeyBytes]) {
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_EXPORT | PSA_KEY_USAGE_SIGN_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_key_id_t key = 0;
    if (!passed(psa_generate_key(&attributes, &key), "generating a key")) {
        return false;
    }
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    const bool good = passed(mbedtls_pk_copy_from_psa(key, &pk), "copying the key") &&
                      passed(mbedtls_pk_write_key_pem(&pk, reinterpret_cast<unsigned char*>(pem), sizeof(pem)), "writing the key");
    mbedtls_pk_free(&pk);
    psa_destroy_key(key);
    return good;
}

bool load_key(const char* pem, mbedtls_pk_context& pk) {
    return passed(mbedtls_pk_parse_key(&pk, reinterpret_cast<const unsigned char*>(pem), std::strlen(pem) + 1, nullptr, 0), "reading a key");
}

// Whether what the server is given would serve: the portal's certificate reads, and its key reads and is
// the one the certificate names. The server takes them as they are, and reads them only at a handshake,
// so one that cannot serve would leave HTTPS up in name, the home network sent to it, and every handshake
// failing. Out of memory says nothing of them either way: they are kept, and not served this start.
enum class Serves : uint8_t { Yes, No, Unknown };
Serves identity_serves(const TlsIdentity& identity) {
    psa_crypto_init();
    mbedtls_x509_crt certificate;
    mbedtls_x509_crt_init(&certificate);
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    const int read_certificate = mbedtls_x509_crt_parse(&certificate, reinterpret_cast<const unsigned char*>(identity.certificate),
                                                        std::strlen(identity.certificate) + 1);
    const int read_key = read_certificate == 0 ? mbedtls_pk_parse_key(&key, reinterpret_cast<const unsigned char*>(identity.key),
                                                                      std::strlen(identity.key) + 1, nullptr, 0)
                                               : 0;
    const int paired = read_certificate == 0 && read_key == 0 ? mbedtls_pk_check_pair(&certificate.pk, &key) : 0;
    mbedtls_pk_free(&key);
    mbedtls_x509_crt_free(&certificate);
    const auto short_of_memory = [](int result) {
        return result == MBEDTLS_ERR_X509_ALLOC_FAILED || result == MBEDTLS_ERR_PK_ALLOC_FAILED || result == MBEDTLS_ERR_PEM_ALLOC_FAILED ||
               result == MBEDTLS_ERR_ASN1_ALLOC_FAILED || result == PSA_ERROR_INSUFFICIENT_MEMORY;
    };
    if (short_of_memory(read_certificate) || short_of_memory(read_key) || short_of_memory(paired)) {
        return Serves::Unknown;
    }
    if (read_certificate != 0 || read_key != 0 || paired != 0) {
        ESP_LOGE(TAG, "the saved certificate cannot serve: -0x%04x -0x%04x -0x%04x", static_cast<unsigned>(-read_certificate),
                 static_cast<unsigned>(-read_key), static_cast<unsigned>(-paired));
        return Serves::No;
    }
    return Serves::Yes;
}

// What every certificate has. The caller adds what makes it a root or a portal's.
bool start_certificate(mbedtls_x509write_cert& writer, const char* subject, const char* issuer, int64_t until) {
    std::array<uint8_t, 16> serial{};
    esp_fill_random(serial.data(), serial.size());
    serial[0] = (serial[0] & 0x7F) | 0x40;  // Positive, with no leading zero.
    char not_before[kTlsTimeBytes];
    char not_after[kTlsTimeBytes];
    tls_format_time(s_now_epoch - kTlsClientClockGraceSeconds, not_before);
    tls_format_time(until, not_after);
    mbedtls_x509write_crt_set_version(&writer, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&writer, MBEDTLS_MD_SHA256);
    return passed(mbedtls_x509write_crt_set_subject_name(&writer, subject), "subject") &&
           passed(mbedtls_x509write_crt_set_issuer_name(&writer, issuer), "issuer") &&
           passed(mbedtls_x509write_crt_set_serial_raw(&writer, serial.data(), serial.size()), "serial") &&
           passed(mbedtls_x509write_crt_set_validity(&writer, not_before, not_after), "dates");
}

// From the PEM's bytes rather than by parsing the certificate: mbedTLS refuses a certificate with
// critical Name Constraints, which it cannot enforce, and it never has to verify this one.
bool write_fingerprint() {
    mbedtls_pem_context pem;
    mbedtls_pem_init(&pem);
    std::array<uint8_t, 32> digest{};
    size_t length = 0;
    size_t used = 0;
    size_t der_bytes = 0;
    const bool good = passed(mbedtls_pem_read_buffer(&pem, "-----BEGIN CERTIFICATE-----", "-----END CERTIFICATE-----",
                                                     reinterpret_cast<const unsigned char*>(s_identity.root_certificate),
                                                     nullptr, 0, &used), "reading the root back") &&
                      [&] {
                          const unsigned char* const der = mbedtls_pem_get_buffer(&pem, &der_bytes);
                          return passed(psa_hash_compute(PSA_ALG_SHA_256, der, der_bytes, digest.data(), digest.size(), &length), "its digest");
                      }();
    mbedtls_pem_free(&pem);
    if (!good) {
        return false;
    }
    for (size_t i = 0; i < digest.size(); ++i) {
        std::snprintf(s_identity.root_fingerprint + i * 2, 3, "%02X", digest[i]);
    }
    return true;
}

bool make_root() {
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    mbedtls_x509write_cert writer;
    mbedtls_x509write_crt_init(&writer);
    std::array<uint8_t, kTlsConstraintsBytes> constraints{};
    const size_t constraints_bytes = tls_name_constraints_der(s_domain, CSIGARLAND_PORTAL_IP, constraints.data(), constraints.size());
    const int64_t until = s_now_epoch + kTlsRootValiditySeconds;
    bool good = constraints_bytes != 0 && load_key(s_identity.root_key, pk);
    if (good) {
        mbedtls_x509write_crt_set_subject_key(&writer, &pk);
        mbedtls_x509write_crt_set_issuer_key(&writer, &pk);
        good = start_certificate(writer, kRootSubject, kRootSubject, until) &&
               passed(mbedtls_x509write_crt_set_basic_constraints(&writer, 1, 0), "root constraints") &&
               passed(mbedtls_x509write_crt_set_key_usage(&writer, MBEDTLS_X509_KU_KEY_CERT_SIGN | MBEDTLS_X509_KU_CRL_SIGN), "root usage") &&
               passed(mbedtls_x509write_crt_set_subject_key_identifier(&writer), "root key id") &&
               passed(mbedtls_x509write_crt_set_extension(&writer, kNameConstraintsOid, sizeof(kNameConstraintsOid) - 1, 1,
                                                          constraints.data(), constraints_bytes), "name constraints") &&
               passed(mbedtls_x509write_crt_pem(&writer, reinterpret_cast<unsigned char*>(s_identity.root_certificate),
                                                sizeof(s_identity.root_certificate)), "signing the root");
    }
    mbedtls_x509write_crt_free(&writer);
    mbedtls_pk_free(&pk);
    if (!good) {
        return false;
    }
    std::snprintf(s_identity.root_domain, sizeof(s_identity.root_domain), "%s", s_domain);
    s_identity.root_not_after = until;
    s_starts.root_not_before = s_now_epoch - kTlsClientClockGraceSeconds;
    return write_fingerprint();
}

// Up to kMaxNames, pointing into the text, which it cuts at the commas.
void fill_names(char* text, std::array<mbedtls_x509_san_list, kMaxNames>& list, std::array<std::array<uint8_t, 4>, kMaxNames>& addresses) {
    size_t count = 0;
    for (char* part = text; part != nullptr && count < kMaxNames;) {
        char* const comma = std::strchr(part, ',');
        if (comma != nullptr) {
            *comma = '\0';
        }
        mbedtls_x509_san_list& entry = list[count];
        entry = mbedtls_x509_san_list{};
        uint8_t address[4];
        if (tls_parse_ipv4(part, address)) {
            std::memcpy(addresses[count].data(), address, sizeof(address));
            entry.node.type = MBEDTLS_X509_SAN_IP_ADDRESS;
            entry.node.san.unstructured_name.p = addresses[count].data();
            entry.node.san.unstructured_name.len = sizeof(address);
        } else {
            entry.node.type = MBEDTLS_X509_SAN_DNS_NAME;
            entry.node.san.unstructured_name.p = reinterpret_cast<unsigned char*>(part);
            entry.node.san.unstructured_name.len = std::strlen(part);
        }
        if (count > 0) {
            list[count - 1].next = &entry;
        }
        ++count;
        part = comma != nullptr ? comma + 1 : nullptr;
    }
}

bool make_portal_certificate() {
    mbedtls_pk_context portal_key;
    mbedtls_pk_init(&portal_key);
    mbedtls_pk_context root_key;
    mbedtls_pk_init(&root_key);
    mbedtls_x509write_cert writer;
    mbedtls_x509write_crt_init(&writer);

    static char s_names[kTlsNamesBytes];
    std::snprintf(s_names, sizeof(s_names), "%s", s_wanted_names);
    static std::array<mbedtls_x509_san_list, kMaxNames> s_list;
    static std::array<std::array<uint8_t, 4>, kMaxNames> s_addresses;
    fill_names(s_names, s_list, s_addresses);
    mbedtls_asn1_sequence server_auth{};
    server_auth.buf.tag = MBEDTLS_ASN1_OID;
    server_auth.buf.p = reinterpret_cast<unsigned char*>(const_cast<char*>(MBEDTLS_OID_SERVER_AUTH));
    server_auth.buf.len = MBEDTLS_OID_SIZE(MBEDTLS_OID_SERVER_AUTH);

    // Less the day's grace at its start: Apple's limit counts from there, and so would refuse 826.
    const int64_t until = s_now_epoch + kTlsValiditySeconds - kTlsClientClockGraceSeconds;
    bool good = load_key(s_identity.key, portal_key) && load_key(s_identity.root_key, root_key);
    if (good) {
        mbedtls_x509write_crt_set_subject_key(&writer, &portal_key);
        mbedtls_x509write_crt_set_issuer_key(&writer, &root_key);
        good = start_certificate(writer, kPortalSubject, kRootSubject, until) &&
               passed(mbedtls_x509write_crt_set_basic_constraints(&writer, 0, -1), "portal constraints") &&
               passed(mbedtls_x509write_crt_set_key_usage(&writer, MBEDTLS_X509_KU_DIGITAL_SIGNATURE), "portal usage") &&
               passed(mbedtls_x509write_crt_set_ext_key_usage(&writer, &server_auth), "portal extended usage") &&
               passed(mbedtls_x509write_crt_set_authority_key_identifier(&writer), "authority key id") &&
               passed(mbedtls_x509write_crt_set_subject_alternative_name(&writer, s_list.data()), "portal names") &&
               passed(mbedtls_x509write_crt_pem(&writer, reinterpret_cast<unsigned char*>(s_identity.certificate),
                                                sizeof(s_identity.certificate)), "signing the portal certificate");
    }
    mbedtls_x509write_crt_free(&writer);
    mbedtls_pk_free(&root_key);
    mbedtls_pk_free(&portal_key);
    if (!good) {
        return false;
    }
    std::snprintf(s_identity.names, sizeof(s_identity.names), "%s", s_wanted_names);
    s_identity.not_after = until;
    s_starts.not_before = s_now_epoch - kTlsClientClockGraceSeconds;
    return true;
}

// Certificates come from keys that exist; keys are made only where there are none.
bool make_what_is_missing() {
    if (s_identity.root_key[0] == '\0') {
        ESP_LOGW(TAG, "this board makes its own root key");
        s_identity.root_certificate[0] = '\0';
        if (!make_key(s_identity.root_key)) {
            return false;
        }
    }
    if (s_identity.key[0] == '\0') {
        ESP_LOGW(TAG, "this board makes its own portal key");
        s_identity.certificate[0] = '\0';
        if (!make_key(s_identity.key)) {
            return false;
        }
    }
    if (tls_root_is_stale(s_identity, s_domain, s_now_epoch)) {
        ESP_LOGW(TAG, "a new root certificate, which browsers must be given again");
        s_identity.certificate[0] = '\0';
        if (!make_root()) {
            return false;
        }
    }
    return !tls_certificate_is_stale(s_identity, s_wanted_names, s_now_epoch) || make_portal_certificate();
}

// A saved key that mbedTLS takes, or none: what failed was then the making, not the key. Running out
// of memory while reading it says nothing of the key, and never counts against it. Nor does the bare
// MBEDTLS_ERR_PK_KEY_INVALID_FORMAT, which mbedTLS returns whatever stopped the public point's import,
// out of key slots or memory among it: a key whose bytes are bad comes back with what in them is, an
// ASN.1 or PEM code beside it. Read three times, as what runs short may be free a moment later.
KeyCheck check_key(const char* pem) {
    if (pem[0] == '\0') {
        return KeyCheck::Sound;
    }
    int result = 0;
    for (int attempt = 0; attempt < 3; ++attempt) {
        mbedtls_pk_context pk;
        mbedtls_pk_init(&pk);
        result = mbedtls_pk_parse_key(&pk, reinterpret_cast<const unsigned char*>(pem), std::strlen(pem) + 1, nullptr, 0);
        mbedtls_pk_free(&pk);
        if (result == 0) {
            return KeyCheck::Sound;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    const bool resources = result == MBEDTLS_ERR_PK_ALLOC_FAILED || result == MBEDTLS_ERR_PEM_ALLOC_FAILED ||
                           result == MBEDTLS_ERR_ASN1_ALLOC_FAILED || result == PSA_ERROR_INSUFFICIENT_MEMORY ||
                           result == PSA_ERROR_INSUFFICIENT_STORAGE || result == MBEDTLS_ERR_PK_KEY_INVALID_FORMAT ||
                           result == MBEDTLS_ERR_PK_INVALID_PUBKEY;
    ESP_LOGE(TAG, "reading a key: -0x%04x", static_cast<unsigned>(-result));
    return resources ? KeyCheck::Unknown : KeyCheck::Broken;
}

void maker_task(void*) {
    psa_crypto_init();
    bool good = make_what_is_missing();
    if (!good) {
        switch (tls_recovery(check_key(s_identity.root_key), check_key(s_identity.key))) {
            case TlsRecovery::Retry:
                // Sound keys are never given up for a failure that may pass: once more, and if that
                // fails too, nothing is saved, and HTTP stays for an update.
                ESP_LOGE(TAG, "could not make certificates with the saved keys, trying once more");
                good = make_what_is_missing();
                break;
            case TlsRecovery::NewPortalKey:
                // The root stays, and with it every browser's trust: only the portal's key is made again,
                // and its certificate under the same root.
                ESP_LOGE(TAG, "the saved portal key cannot be read, so this board makes a new one under its root");
                s_identity.key[0] = '\0';
                s_identity.certificate[0] = '\0';
                good = make_what_is_missing();
                break;
            case TlsRecovery::NewKeys:
                // A key that cannot be read could never serve.
                ESP_LOGE(TAG, "the saved root key cannot be read, so this board makes new keys");
                s_identity = TlsIdentity{};
                good = make_what_is_missing();
                break;
            case TlsRecovery::Keep:
                // Nothing is known against the keys: they stay saved, and a later start tries again.
                ESP_LOGE(TAG, "out of memory reading the saved keys: they are kept, and HTTP stays for an update");
                break;
        }
    }
    if (good) {
        const esp_err_t err = settings_save_tls(s_identity);
        if (err != ESP_OK) {
            // It still serves: the next start simply makes another, and browsers ask once more.
            ESP_LOGE(TAG, "saving failed: %s", esp_err_to_name(err));
        } else {
            s_starts.ruled_out = 0;
            settings_save_tls_starts(s_starts);
        }
        ESP_LOGW(TAG, "certificate for %s, root SHA-256 %s", s_identity.names, s_identity.root_fingerprint);
    } else {
        ESP_LOGE(TAG, "could not make the certificates");
    }
    ESP_LOGI(TAG, "%u bytes of stack to spare, %u of heap free", static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
             static_cast<unsigned>(esp_get_free_heap_size()));
    s_made = good;
    xTaskNotifyGive(s_waiting_task);
    vTaskDelete(nullptr);
}

// The moment by which a certificate made now counts: never before the sources' date, so one made
// just now can never be thought to start too late, and restart for that again and again.
// Set once what the portal's HTTPS is made from has all been read and this start's making of certificates
// is over, made or failed, so that a renewal is weighed on what came of it: a time sync while the maker
// still runs would weigh the certificate it is about to replace, and restart for it.
std::atomic<bool> s_configured{false};
// When this start's making of certificates failed, in microseconds of uptime, or -1: a renewal that the
// time sync asks for would restart into the same failure, and every restart cuts short the time plain
// HTTP has to take an update in. One is let through only every kRenewAfterFailureUs.
std::atomic<int64_t> s_failed_at_us{-1};
constexpr int64_t kRenewAfterFailureUs = 6LL * 3600 * 1000 * 1000;

int64_t soonest_start(int64_t now_epoch) {
    const int64_t built = tls_epoch_from_build_date(CSIGARLAND_SOURCE_DATE);
    return now_epoch > built ? now_epoch : built;
}

}  // namespace

const TlsIdentity* portal_tls_identity_loaded() {
    return s_identity.certificate[0] != '\0' ? &s_identity : nullptr;
}

bool portal_tls_renewal_due(int64_t now_epoch) {
    // Only once the keys and the domain were both read: names worked out from no domain would find any
    // certificate stale, and restart for it at every time sync, while HTTP is all there is to update by.
    const int64_t failed_at = s_failed_at_us.load();
    if (failed_at >= 0 && esp_timer_get_time() - failed_at < kRenewAfterFailureUs) {
        return false;
    }
    return s_configured.load() && portal_tls_identity_loaded() != nullptr && rtc_epoch_is_valid(now_epoch) &&
           (tls_root_is_stale(s_identity, s_domain, now_epoch) || tls_certificate_is_stale(s_identity, s_wanted_names, now_epoch) ||
            tls_starts_after(s_starts, soonest_start(now_epoch)));
}

bool portal_tls_prepare_renewal(int64_t now_epoch) {
    if (tls_starts_after(s_starts, soonest_start(now_epoch))) {
        s_starts.ruled_out = 1;
        if (settings_save_tls_starts(s_starts) != ESP_OK) {
            return false;
        }
    }
    return settings_save_last_epoch(now_epoch) == ESP_OK;
}

const TlsIdentity* portal_tls_identity(const char* hostname, State& state) {
    s_configured.store(false);
    StoredRead read = settings_load_tls(s_identity);
    for (int attempt = 1; attempt < kReadAttempts && read == StoredRead::Unreadable; ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(kReadRetryMs));
        read = settings_load_tls(s_identity);
    }
    StoredRead domain = settings_load_tls_domain(s_domain, sizeof(s_domain));
    for (int attempt = 1; attempt < kReadAttempts && domain == StoredRead::Unreadable; ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(kReadRetryMs));
        domain = settings_load_tls_domain(s_domain, sizeof(s_domain));
    }
    if (!tls_may_make_identity(read, domain)) {
        // What is saved stays as it is, and the portal goes on over HTTP until a start reads it.
        ESP_LOGE(TAG, "the saved keys or domain could not be read: no HTTPS this start, and they are kept");
        return nullptr;
    }
    settings_load_tls_starts(s_starts);
    tls_wanted_names(hostname, s_domain, CSIGARLAND_PORTAL_IP, s_wanted_names);

    const int64_t built = tls_epoch_from_build_date(CSIGARLAND_SOURCE_DATE);
    const auto now = static_cast<int64_t>(std::time(nullptr));
    // The latest moment it can vouch for: the real time, or failing that the last it saw for this.
    const int64_t known = time_is_synced() && rtc_epoch_is_valid(now) ? now : settings_load_last_epoch();
    s_now_epoch = known > built ? known : built;
    tls_drop_ruled_out(s_identity, s_starts, s_now_epoch);
    // A certificate that cannot serve is made again, under the keys kept, or the keys are judged as a
    // failed making judges them: HTTPS goes up only on one that serves, and plain HTTP stays otherwise.
    const Serves serves = s_identity.certificate[0] != '\0' ? identity_serves(s_identity) : Serves::No;
    if (serves == Serves::Unknown) {
        // Not known to serve, and not known not to: kept as saved, and plain HTTP stays this start, as an
        // HTTPS that failed every handshake would leave the home network nothing to update by.
        ESP_LOGE(TAG, "out of memory checking the saved certificate: it is kept, and HTTP stays for an update");
        s_failed_at_us.store(esp_timer_get_time());
        s_configured.store(true);
        return nullptr;
    }
    if (serves == Serves::No) {
        s_identity.certificate[0] = '\0';
    }
    if (s_identity.root_key[0] != '\0' && s_identity.key[0] != '\0' && s_identity.certificate[0] != '\0' &&
        !tls_root_is_stale(s_identity, s_domain, s_now_epoch) && !tls_certificate_is_stale(s_identity, s_wanted_names, s_now_epoch)) {
        s_configured.store(true);
        return &s_identity;
    }
    s_waiting_task = xTaskGetCurrentTaskHandle();
    state.set_making_key(true);
    // Below the screen's task, so that it can show this.
    const BaseType_t created = xTaskCreate(maker_task, "tls_maker", kMakerStackBytes, nullptr, tskIDLE_PRIORITY + 2, nullptr);
    if (created == pdPASS) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    state.set_making_key(false);
    if (created != pdPASS || !s_made) {
        s_failed_at_us.store(esp_timer_get_time());
        s_configured.store(true);
        return nullptr;
    }
    s_configured.store(true);
    return &s_identity;
}

}  // namespace garland
