#ifndef RAUC_BLE_PACK_H
#define RAUC_BLE_PACK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_CTRL_FIRST   0x5Eu  /* '^' */
#define BLE_CTRL_MIDDLE  0x7Eu  /* '~' */
#define BLE_CTRL_END     0x3Bu  /* ';' */
#define BLE_CTRL_SINGLE  0x7Cu  /* '|' */

#define BLE_FIRST_HDR_LEN  5u   /* CTRL + TOT(4,BE) */
#define BLE_CONT_HDR_LEN   1u

#define BLE_ATT_PAYLOAD_MIN 20u

struct ble_pack_iter {
    const uint8_t *msg;
    uint32_t       total_len;
    uint32_t       sent;
    size_t         att_payload;
    int            done;
    int            single;      /* 1 when whole msg fits in one frame */
};

size_t ble_pack_max_first_payload(size_t att_payload);
size_t ble_pack_max_cont_payload(size_t att_payload);

/* Initialize iterator.
 * Returns 0 on success, -1 if att_payload < BLE_ATT_PAYLOAD_MIN or msg_len == 0. */
int ble_pack_iter_init(struct ble_pack_iter *it,
                       const uint8_t *msg, uint32_t msg_len,
                       size_t att_payload);

/* Produce next frame into out[0..out_cap).
 * On success returns >0 bytes written and sets *out_len. Returns 0 when done. Returns -1 on error. */
int ble_pack_iter_next(struct ble_pack_iter *it,
                       uint8_t *out, size_t out_cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
