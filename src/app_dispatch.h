#ifndef RAUC_BLE_APP_DISPATCH_H
#define RAUC_BLE_APP_DISPATCH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ble_app_type {
    BLE_APP_PING = 0x0001,
    BLE_APP_PONG = 0x0002,
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

#ifdef __cplusplus
}
#endif

#endif
