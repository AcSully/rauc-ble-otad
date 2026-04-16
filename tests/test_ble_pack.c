#include "ble_pack.h"
#include "ble_reasm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void test_single_frame(void)
{
    const char *msg = "hello";
    uint8_t frame[64];
    size_t out_len = 0;
    struct ble_pack_iter it;
    assert(ble_pack_iter_init(&it, (const uint8_t *)msg, 5, 64) == 0);
    int r = ble_pack_iter_next(&it, frame, sizeof(frame), &out_len);
    assert(r > 0);
    assert(frame[0] == BLE_CTRL_SINGLE);
    assert(frame[1] == 0 && frame[2] == 0 && frame[3] == 0 && frame[4] == 5);
    assert(out_len == BLE_FIRST_HDR_LEN + 5);
    assert(memcmp(&frame[BLE_FIRST_HDR_LEN], msg, 5) == 0);
    /* next returns 0 (done) */
    assert(ble_pack_iter_next(&it, frame, sizeof(frame), &out_len) == 0);
    printf("  test_single_frame OK\n");
}

static void test_multi_frame_roundtrip(size_t att_payload)
{
    const uint32_t N = 1024;
    uint8_t *msg = (uint8_t *)malloc(N);
    for (uint32_t i = 0; i < N; i++) msg[i] = (uint8_t)(i * 31 + 7);

    struct ble_pack_iter it;
    assert(ble_pack_iter_init(&it, msg, N, att_payload) == 0);

    uint8_t reasm_buf[4096];
    struct ble_reasm_ctx ctx;
    assert(ble_reasm_init(&ctx, reasm_buf, sizeof(reasm_buf), 0) == 0);

    uint8_t frame[512];
    size_t out_len = 0;
    int first = 1;
    while (1) {
        int n = ble_pack_iter_next(&it, frame, sizeof(frame), &out_len);
        if (n == 0) break;
        assert(n > 0);
        assert(out_len <= att_payload);

        if (first) {
            assert(frame[0] == BLE_CTRL_FIRST);
            uint32_t tot = ((uint32_t)frame[1] << 24) | ((uint32_t)frame[2] << 16)
                         | ((uint32_t)frame[3] << 8)  |  (uint32_t)frame[4];
            assert(tot == N);
            first = 0;
        }
        const uint8_t *out_msg = NULL;
        uint32_t full_len = 0;
        int r = ble_reasm_feed(&ctx, frame, out_len, 0, &out_msg, &full_len);
        if (r == BLE_REASM_DELIVER) {
            assert(full_len == N);
            assert(memcmp(out_msg, msg, N) == 0);
        } else {
            assert(r == BLE_REASM_NEED_MORE);
        }
    }
    free(msg);
    printf("  test_multi_frame_roundtrip(att=%zu) OK\n", att_payload);
}

static void test_bad_init(void)
{
    struct ble_pack_iter it;
    uint8_t msg = 1;
    assert(ble_pack_iter_init(&it, &msg, 1, 10) == -1);  /* att < 20 */
    assert(ble_pack_iter_init(&it, &msg, 0, 64) == -1);  /* empty msg */
    assert(ble_pack_iter_init(&it, NULL, 1, 64) == -1);
    printf("  test_bad_init OK\n");
}

int main(void)
{
    test_single_frame();
    test_multi_frame_roundtrip(20);
    test_multi_frame_roundtrip(64);
    test_multi_frame_roundtrip(244);
    test_bad_init();
    printf("test_ble_pack: all passed\n");
    return 0;
}
