#include "app_dispatch.h"
#include "app.pb.h"
#include "ble_pack.h"
#include "ble_reasm.h"

#include <cassert>
#include <cstdio>
#include <cstring>

static void test_encode_decode_ping()
{
    ble::app::Ping p;
    p.set_nonce(0x1122334455667788ULL);

    uint8_t buf[128];
    size_t len = 0;
    assert(ble_app_encode(BLE_APP_PING, &p, buf, sizeof(buf), &len) == 0);
    assert(len >= BLE_APP_TYPE_HDR_LEN);
    assert(buf[0] == 0x00 && buf[1] == 0x01);

    uint16_t type = 0;
    void *msg = nullptr;
    assert(ble_app_decode(buf, len, &type, &msg) == 0);
    assert(type == BLE_APP_PING);
    auto *pp = static_cast<ble::app::Ping *>(msg);
    assert(pp->nonce() == 0x1122334455667788ULL);
    ble_app_free(type, msg);
    std::printf("  test_encode_decode_ping OK\n");
}

static void test_unknown_type()
{
    uint8_t buf[] = { 0xFF, 0xFF, 0x01, 0x02 };
    uint16_t type = 0;
    void *msg = nullptr;
    assert(ble_app_decode(buf, sizeof(buf), &type, &msg) == -1);
    std::printf("  test_unknown_type OK\n");
}

static void test_full_pipeline()
{
    /* Build a Pong, encode -> pack -> reasm -> decode, verify nonce. */
    ble::app::Pong src;
    src.set_nonce(0xDEADBEEFCAFEBABEULL);

    uint8_t app_buf[256];
    size_t app_len = 0;
    assert(ble_app_encode(BLE_APP_PONG, &src, app_buf, sizeof(app_buf), &app_len) == 0);

    struct ble_pack_iter it;
    assert(ble_pack_iter_init(&it, app_buf, (uint32_t)app_len, 20) == 0);

    uint8_t reasm_buf[512];
    struct ble_reasm_ctx ctx;
    ble_reasm_init(&ctx, reasm_buf, sizeof(reasm_buf), 0);

    uint8_t frame[64];
    size_t flen = 0;
    const uint8_t *out_msg = nullptr;
    uint32_t out_len = 0;
    int delivered = 0;
    while (true) {
        int n = ble_pack_iter_next(&it, frame, sizeof(frame), &flen);
        if (n == 0) break;
        int r = ble_reasm_feed(&ctx, frame, flen, 0, &out_msg, &out_len);
        if (r == BLE_REASM_DELIVER) { delivered = 1; break; }
        assert(r == BLE_REASM_NEED_MORE);
    }
    assert(delivered && out_len == app_len);
    assert(std::memcmp(out_msg, app_buf, app_len) == 0);

    uint16_t type = 0;
    void *msg = nullptr;
    assert(ble_app_decode(out_msg, out_len, &type, &msg) == 0);
    assert(type == BLE_APP_PONG);
    auto *pp = static_cast<ble::app::Pong *>(msg);
    assert(pp->nonce() == 0xDEADBEEFCAFEBABEULL);
    ble_app_free(type, msg);
    std::printf("  test_full_pipeline OK\n");
}

int main()
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    test_encode_decode_ping();
    test_unknown_type();
    test_full_pipeline();
    google::protobuf::ShutdownProtobufLibrary();
    std::printf("test_app_dispatch: all passed\n");
    return 0;
}
