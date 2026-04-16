#include "ble_reasm.h"
#include "ble_pack.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_single_deliver(void)
{
    uint8_t buf[256];
    struct ble_reasm_ctx ctx;
    assert(ble_reasm_init(&ctx, buf, sizeof(buf), 1000) == 0);

    uint8_t pdu[] = { BLE_CTRL_SINGLE, 0, 0, 0, 3, 'a', 'b', 'c' };
    const uint8_t *msg = NULL;
    uint32_t len = 0;
    int r = ble_reasm_feed(&ctx, pdu, sizeof(pdu), 0, &msg, &len);
    assert(r == BLE_REASM_DELIVER);
    assert(len == 3 && memcmp(msg, "abc", 3) == 0);
    printf("  test_single_deliver OK\n");
}

static void test_single_length_mismatch(void)
{
    uint8_t buf[256];
    struct ble_reasm_ctx ctx;
    assert(ble_reasm_init(&ctx, buf, sizeof(buf), 0) == 0);

    /* TotalLen=5 but only 3 payload bytes with CTRL_SINGLE */
    uint8_t pdu[] = { BLE_CTRL_SINGLE, 0, 0, 0, 5, 'a', 'b', 'c' };
    const uint8_t *msg = NULL;
    uint32_t len = 0;
    int r = ble_reasm_feed(&ctx, pdu, sizeof(pdu), 0, &msg, &len);
    assert(r == BLE_REASM_DROP);
    assert(ctx.state == BLE_REASM_IDLE);
    printf("  test_single_length_mismatch OK\n");
}

static void test_idle_receives_continuation(void)
{
    uint8_t buf[256];
    struct ble_reasm_ctx ctx;
    ble_reasm_init(&ctx, buf, sizeof(buf), 0);

    uint8_t mid[] = { BLE_CTRL_MIDDLE, 'x', 'y' };
    const uint8_t *m = NULL;
    uint32_t l = 0;
    assert(ble_reasm_feed(&ctx, mid, sizeof(mid), 0, &m, &l) == BLE_REASM_DROP);

    uint8_t end[] = { BLE_CTRL_END, 'x' };
    assert(ble_reasm_feed(&ctx, end, sizeof(end), 0, &m, &l) == BLE_REASM_DROP);
    printf("  test_idle_receives_continuation OK\n");
}

static void test_illegal_ctrl(void)
{
    uint8_t buf[256];
    struct ble_reasm_ctx ctx;
    ble_reasm_init(&ctx, buf, sizeof(buf), 0);

    uint8_t pdu[] = { 0x41, 'a', 'b' };
    const uint8_t *m = NULL;
    uint32_t l = 0;
    assert(ble_reasm_feed(&ctx, pdu, sizeof(pdu), 0, &m, &l) == BLE_REASM_DROP);
    printf("  test_illegal_ctrl OK\n");
}

static void test_total_zero_and_too_big(void)
{
    uint8_t buf[16];
    struct ble_reasm_ctx ctx;
    ble_reasm_init(&ctx, buf, sizeof(buf), 0);

    uint8_t zero[] = { BLE_CTRL_FIRST, 0, 0, 0, 0 };
    const uint8_t *m = NULL;
    uint32_t l = 0;
    assert(ble_reasm_feed(&ctx, zero, sizeof(zero), 0, &m, &l) == BLE_REASM_DROP);

    /* total=100 > buf_cap=16 */
    uint8_t big[] = { BLE_CTRL_FIRST, 0, 0, 0, 100, 'x' };
    assert(ble_reasm_feed(&ctx, big, sizeof(big), 0, &m, &l) == BLE_REASM_DROP);
    printf("  test_total_zero_and_too_big OK\n");
}

static void test_timeout(void)
{
    uint8_t buf[256];
    struct ble_reasm_ctx ctx;
    ble_reasm_init(&ctx, buf, sizeof(buf), 100);

    /* Start a multi-frame session, total=10 */
    uint8_t first[] = { BLE_CTRL_FIRST, 0, 0, 0, 10, 'a', 'b', 'c' };
    const uint8_t *m = NULL;
    uint32_t l = 0;
    assert(ble_reasm_feed(&ctx, first, sizeof(first), 0, &m, &l) == BLE_REASM_NEED_MORE);
    assert(ctx.state == BLE_REASM_REASSEMBLING);

    /* Continuation after timeout is evaluated as IDLE + continuation -> DROP */
    uint8_t mid[] = { BLE_CTRL_MIDDLE, 'd', 'e' };
    assert(ble_reasm_feed(&ctx, mid, sizeof(mid), 1000, &m, &l) == BLE_REASM_DROP);
    assert(ctx.state == BLE_REASM_IDLE);
    printf("  test_timeout OK\n");
}

static void test_end_length_mismatch(void)
{
    uint8_t buf[256];
    struct ble_reasm_ctx ctx;
    ble_reasm_init(&ctx, buf, sizeof(buf), 0);

    uint8_t first[] = { BLE_CTRL_FIRST, 0, 0, 0, 10, 'a', 'b', 'c' };  /* need 10 */
    const uint8_t *m = NULL;
    uint32_t l = 0;
    assert(ble_reasm_feed(&ctx, first, sizeof(first), 0, &m, &l) == BLE_REASM_NEED_MORE);

    /* End with only 3 more bytes -> total=6 != 10 */
    uint8_t end[] = { BLE_CTRL_END, 'd', 'e', 'f' };
    assert(ble_reasm_feed(&ctx, end, sizeof(end), 0, &m, &l) == BLE_REASM_DROP);
    assert(ctx.state == BLE_REASM_IDLE);
    printf("  test_end_length_mismatch OK\n");
}

int main(void)
{
    test_single_deliver();
    test_single_length_mismatch();
    test_idle_receives_continuation();
    test_illegal_ctrl();
    test_total_zero_and_too_big();
    test_timeout();
    test_end_length_mismatch();
    printf("test_ble_reasm: all passed\n");
    return 0;
}
