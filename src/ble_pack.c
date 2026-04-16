#include "ble_pack.h"
#include <string.h>

size_t ble_pack_max_first_payload(size_t att_payload)
{
    return (att_payload > BLE_FIRST_HDR_LEN) ? (att_payload - BLE_FIRST_HDR_LEN) : 0;
}

size_t ble_pack_max_cont_payload(size_t att_payload)
{
    return (att_payload > BLE_CONT_HDR_LEN) ? (att_payload - BLE_CONT_HDR_LEN) : 0;
}

int ble_pack_iter_init(struct ble_pack_iter *it,
                       const uint8_t *msg, uint32_t msg_len,
                       size_t att_payload)
{
    if (!it || !msg || msg_len == 0 || att_payload < BLE_ATT_PAYLOAD_MIN)
        return -1;
    it->msg = msg;
    it->total_len = msg_len;
    it->sent = 0;
    it->att_payload = att_payload;
    it->done = 0;
    it->single = (msg_len <= ble_pack_max_first_payload(att_payload)) ? 1 : 0;
    return 0;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

int ble_pack_iter_next(struct ble_pack_iter *it,
                       uint8_t *out, size_t out_cap, size_t *out_len)
{
    if (!it || !out || !out_len) return -1;
    if (it->done) return 0;

    uint32_t remaining = it->total_len - it->sent;
    int is_first = (it->sent == 0);

    if (is_first) {
        size_t cap = ble_pack_max_first_payload(it->att_payload);
        if (out_cap < BLE_FIRST_HDR_LEN + 1) return -1;
        uint8_t ctrl = it->single ? BLE_CTRL_SINGLE : BLE_CTRL_FIRST;
        size_t take = remaining < cap ? remaining : cap;
        if (out_cap < BLE_FIRST_HDR_LEN + take) return -1;
        out[0] = ctrl;
        put_be32(&out[1], it->total_len);
        memcpy(&out[BLE_FIRST_HDR_LEN], it->msg, take);
        it->sent += (uint32_t)take;
        *out_len = BLE_FIRST_HDR_LEN + take;
        if (it->sent == it->total_len) it->done = 1;
        return (int)*out_len;
    } else {
        size_t cap = ble_pack_max_cont_payload(it->att_payload);
        if (out_cap < BLE_CONT_HDR_LEN + 1) return -1;
        size_t take = remaining < cap ? remaining : cap;
        int last = (take == remaining);
        uint8_t ctrl = last ? BLE_CTRL_END : BLE_CTRL_MIDDLE;
        if (out_cap < BLE_CONT_HDR_LEN + take) return -1;
        out[0] = ctrl;
        memcpy(&out[BLE_CONT_HDR_LEN], it->msg + it->sent, take);
        it->sent += (uint32_t)take;
        *out_len = BLE_CONT_HDR_LEN + take;
        if (last) it->done = 1;
        return (int)*out_len;
    }
}
