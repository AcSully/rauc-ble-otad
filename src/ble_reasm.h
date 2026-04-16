#ifndef RAUC_BLE_REASM_H
#define RAUC_BLE_REASM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ble_reasm_state {
    BLE_REASM_IDLE = 0,
    BLE_REASM_REASSEMBLING = 1,
};

enum ble_reasm_result {
    BLE_REASM_NEED_MORE = 0,  /* Frame accepted, waiting for more. */
    BLE_REASM_DELIVER   = 1,  /* A full message is ready (out_msg/out_len set). */
    BLE_REASM_DROP      = -1, /* Illegal frame / timeout / overflow; context reset. */
};

struct ble_reasm_ctx {
    enum ble_reasm_state state;
    uint8_t  *buf;
    size_t    buf_cap;
    uint32_t  total_len;
    uint32_t  buf_used;
    uint32_t  timeout_ms;
    uint64_t  last_rx_ms;
};

/* Initialize receiver context with caller-provided buffer and timeout.
 * timeout_ms == 0 disables timeout. */
int ble_reasm_init(struct ble_reasm_ctx *ctx,
                   uint8_t *buf, size_t buf_cap,
                   uint32_t timeout_ms);

/* Feed one BLE PDU. On DELIVER, *out_msg points into ctx->buf and *out_len
 * is the full message length; the buffer remains valid until the next feed. */
int ble_reasm_feed(struct ble_reasm_ctx *ctx,
                   const uint8_t *pdu, size_t pdu_len,
                   uint64_t now_ms,
                   const uint8_t **out_msg, uint32_t *out_len);

void ble_reasm_reset(struct ble_reasm_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
