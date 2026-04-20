#ifndef RAUC_BLE_APP_DISPATCH_H
#define RAUC_BLE_APP_DISPATCH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ble_app_type {
    BLE_APP_PING               = 0x0001,
    BLE_APP_PONG               = 0x0002,
    BLE_APP_OTA_BEGIN          = 0x0010,
    BLE_APP_OTA_BEGIN_ACK      = 0x0011,
    BLE_APP_OTA_CHUNK          = 0x0012,
    BLE_APP_OTA_CHUNK_ACK      = 0x0013,
    BLE_APP_OTA_END            = 0x0014,
    BLE_APP_OTA_END_ACK        = 0x0015,
    BLE_APP_GET_VERSION        = 0x0020,
    BLE_APP_VERSION_REPLY      = 0x0021,
    BLE_APP_MISSING_CHUNKS     = 0x0022,
    BLE_APP_OTA_INSTALL        = 0x0023,
    BLE_APP_OTA_INSTALL_REPLY  = 0x0024,
};

#define BLE_APP_TYPE_HDR_LEN 2u

int ble_app_encode(uint16_t type, const void *msg,
                   uint8_t *out, size_t out_cap, size_t *out_len);

int ble_app_decode(const uint8_t *buf, size_t len,
                   uint16_t *out_type, void **out_msg);

void ble_app_free(uint16_t type, void *msg);

/* Protobuf serialized size (app payload, including 2-byte type header). */
size_t ble_app_encoded_size(uint16_t type, const void *msg);

/* Write a one-line human-readable summary of the decoded message into buf.
 * Output format: "TYPE_NAME field=value field=value". Long fields (bytes)
 * are truncated. Returns number of chars written (excluding NUL), clamped
 * to cap-1. buf is always NUL-terminated when cap > 0. */
int ble_app_describe(uint16_t type, const void *msg, char *buf, size_t cap);

/* ---- C-accessible field getters ---- */

const char *ble_app_ota_begin_filename(const void *msg);
uint64_t    ble_app_ota_begin_total_size(const void *msg);
uint32_t    ble_app_ota_begin_chunk_size(const void *msg);

uint32_t    ble_app_ota_chunk_seq(const void *msg);
const uint8_t *ble_app_ota_chunk_data(const void *msg, size_t *out_len);

uint64_t    ble_app_ota_end_crc64(const void *msg);

const uint8_t *ble_app_ota_install_sha256(const void *msg, size_t *out_len);

/* ---- C-accessible message constructors ---- */

void *ble_app_ota_begin_ack_new(int ok, const char *error);
void *ble_app_ota_chunk_ack_new(uint32_t seq, int ok);
void *ble_app_ota_end_ack_new(int ok, const char *error);

void *ble_app_version_reply_new(const char *version);
void *ble_app_missing_chunks_new(int ok, const char *error,
                                 const uint8_t *content, size_t content_len);
void *ble_app_ota_install_reply_new(int ok, const char *error);

#ifdef __cplusplus
}
#endif

#endif
