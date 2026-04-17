#ifndef RAUC_BLE_APP_DISPATCH_H
#define RAUC_BLE_APP_DISPATCH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ble_app_type {
    BLE_APP_PING          = 0x0001,
    BLE_APP_PONG          = 0x0002,
    BLE_APP_OTA_BEGIN     = 0x0010,
    BLE_APP_OTA_BEGIN_ACK = 0x0011,
    BLE_APP_OTA_CHUNK     = 0x0012,
    BLE_APP_OTA_CHUNK_ACK = 0x0013,
    BLE_APP_OTA_END       = 0x0014,
    BLE_APP_OTA_END_ACK   = 0x0015,
};

#define BLE_APP_TYPE_HDR_LEN 2u

/* Serialize an application message into out:
 *   TYPE_HI | TYPE_LO | protobuf_data
 * msg must point to the matching C++ protobuf object for `type`.
 * Returns 0 on success. */
int ble_app_encode(uint16_t type, const void *msg,
                   uint8_t *out, size_t out_cap, size_t *out_len);

/* Parse a delivered payload into (type, msg). On success *out_msg is a
 * newly allocated protobuf object owned by the caller; free with ble_app_free. */
int ble_app_decode(const uint8_t *buf, size_t len,
                   uint16_t *out_type, void **out_msg);

void ble_app_free(uint16_t type, void *msg);

/* ---- C-accessible field getters for OTA messages ---- */

/* OtaBegin fields */
const char *ble_app_ota_begin_filename(const void *msg);
uint64_t    ble_app_ota_begin_total_size(const void *msg);
uint32_t    ble_app_ota_begin_chunk_size(const void *msg);

/* OtaChunk fields */
uint32_t    ble_app_ota_chunk_seq(const void *msg);
const uint8_t *ble_app_ota_chunk_data(const void *msg, size_t *out_len);

/* OtaEnd fields */
uint64_t    ble_app_ota_end_crc64(const void *msg);

/* ---- C-accessible message constructors for Ack responses ---- */

void *ble_app_ota_begin_ack_new(int ok, const char *error);
void *ble_app_ota_chunk_ack_new(uint32_t seq, int ok);
void *ble_app_ota_end_ack_new(int ok, const char *error);

#ifdef __cplusplus
}
#endif

#endif
