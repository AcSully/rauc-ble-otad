#include "ble_reasm.h"
#include "ble_pack.h"
#include <string.h>

int ble_reasm_init(struct ble_reasm_ctx *ctx,
                   uint8_t *buf, size_t buf_cap,
                   uint32_t timeout_ms)
{
    if (!ctx || !buf || buf_cap == 0) return -1;
    ctx->state = BLE_REASM_IDLE;
    ctx->buf = buf;
    ctx->buf_cap = buf_cap;
    ctx->total_len = 0;
    ctx->buf_used = 0;
    ctx->timeout_ms = timeout_ms;
    ctx->last_rx_ms = 0;
    return 0;
}

void ble_reasm_reset(struct ble_reasm_ctx *ctx)
{
    if (!ctx) return;
    ctx->state = BLE_REASM_IDLE;
    ctx->total_len = 0;
    ctx->buf_used = 0;
}

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

int ble_reasm_feed(struct ble_reasm_ctx *ctx,
                   const uint8_t *pdu, size_t pdu_len,
                   uint64_t now_ms,
                   const uint8_t **out_msg, uint32_t *out_len)
{
    if (!ctx || !pdu || pdu_len < 1) return BLE_REASM_DROP;

    /* Timeout check before processing new frame. */
    if (ctx->state == BLE_REASM_REASSEMBLING && ctx->timeout_ms > 0) {
        if (now_ms - ctx->last_rx_ms > ctx->timeout_ms) {
            ble_reasm_reset(ctx);
        }
    }

    uint8_t ctrl = pdu[0];
    switch (ctrl) {
    case BLE_CTRL_FIRST:
    case BLE_CTRL_SINGLE: {
        if (pdu_len < BLE_FIRST_HDR_LEN) {
            ble_reasm_reset(ctx);
            return BLE_REASM_DROP;
        }
        uint32_t total = read_be32(&pdu[1]);
        if (total == 0 || total > ctx->buf_cap) {
            ble_reasm_reset(ctx);
            return BLE_REASM_DROP;
        }
        uint32_t payload_len = (uint32_t)(pdu_len - BLE_FIRST_HDR_LEN);
        if (payload_len > total) {
            ble_reasm_reset(ctx);
            return BLE_REASM_DROP;
        }
        ctx->total_len = total;
        ctx->buf_used = 0;
        memcpy(ctx->buf, &pdu[BLE_FIRST_HDR_LEN], payload_len);
        ctx->buf_used = payload_len;
        ctx->last_rx_ms = now_ms;

        if (ctrl == BLE_CTRL_SINGLE) {
            if (ctx->buf_used != ctx->total_len) {
                ble_reasm_reset(ctx);
                return BLE_REASM_DROP;
            }
            if (out_msg) *out_msg = ctx->buf;
            if (out_len) *out_len = ctx->buf_used;
            ctx->state = BLE_REASM_IDLE;
            return BLE_REASM_DELIVER;
        }
        /* CTRL_FIRST */
        if (ctx->buf_used >= ctx->total_len) {
            /* A multi-frame header cannot already be complete. */
            ble_reasm_reset(ctx);
            return BLE_REASM_DROP;
        }
        ctx->state = BLE_REASM_REASSEMBLING;
        return BLE_REASM_NEED_MORE;
    }
    case BLE_CTRL_MIDDLE:
    case BLE_CTRL_END: {
        if (ctx->state != BLE_REASM_REASSEMBLING) {
            ble_reasm_reset(ctx);
            return BLE_REASM_DROP;
        }
        uint32_t payload_len = (uint32_t)(pdu_len - BLE_CONT_HDR_LEN);
        if ((uint64_t)ctx->buf_used + payload_len > ctx->total_len) {
            ble_reasm_reset(ctx);
            return BLE_REASM_DROP;
        }
        memcpy(&ctx->buf[ctx->buf_used], &pdu[BLE_CONT_HDR_LEN], payload_len);
        ctx->buf_used += payload_len;
        ctx->last_rx_ms = now_ms;

        if (ctrl == BLE_CTRL_END) {
            if (ctx->buf_used != ctx->total_len) {
                ble_reasm_reset(ctx);
                return BLE_REASM_DROP;
            }
            if (out_msg) *out_msg = ctx->buf;
            if (out_len) *out_len = ctx->buf_used;
            ctx->state = BLE_REASM_IDLE;
            return BLE_REASM_DELIVER;
        }
        return BLE_REASM_NEED_MORE;
    }
    default:
        ble_reasm_reset(ctx);
        return BLE_REASM_DROP;
    }
}
