#include "app_dispatch.h"
#include "app.pb.h"

#include <google/protobuf/message_lite.h>

namespace {

google::protobuf::MessageLite *make_message(uint16_t type)
{
    switch (type) {
    case BLE_APP_PING:          return new ble::app::Ping();
    case BLE_APP_PONG:          return new ble::app::Pong();
    case BLE_APP_OTA_BEGIN:     return new ble::app::OtaBegin();
    case BLE_APP_OTA_BEGIN_ACK: return new ble::app::OtaBeginAck();
    case BLE_APP_OTA_CHUNK:     return new ble::app::OtaChunk();
    case BLE_APP_OTA_CHUNK_ACK: return new ble::app::OtaChunkAck();
    case BLE_APP_OTA_END:       return new ble::app::OtaEnd();
    case BLE_APP_OTA_END_ACK:   return new ble::app::OtaEndAck();
    default: return nullptr;
    }
}

} // namespace

extern "C" int ble_app_encode(uint16_t type, const void *msg,
                              uint8_t *out, size_t out_cap, size_t *out_len)
{
    if (!msg || !out || !out_len || out_cap < BLE_APP_TYPE_HDR_LEN) return -1;
    const auto *m = static_cast<const google::protobuf::MessageLite *>(msg);
    size_t body = m->ByteSizeLong();
    if (body + BLE_APP_TYPE_HDR_LEN > out_cap) return -1;
    out[0] = static_cast<uint8_t>(type >> 8);
    out[1] = static_cast<uint8_t>(type & 0xFF);
    if (!m->SerializeToArray(out + BLE_APP_TYPE_HDR_LEN, static_cast<int>(body)))
        return -1;
    *out_len = BLE_APP_TYPE_HDR_LEN + body;
    return 0;
}

extern "C" int ble_app_decode(const uint8_t *buf, size_t len,
                              uint16_t *out_type, void **out_msg)
{
    if (!buf || !out_type || !out_msg || len < BLE_APP_TYPE_HDR_LEN) return -1;
    uint16_t type = (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
    google::protobuf::MessageLite *m = make_message(type);
    if (!m) return -1;
    if (!m->ParseFromArray(buf + BLE_APP_TYPE_HDR_LEN,
                           static_cast<int>(len - BLE_APP_TYPE_HDR_LEN))) {
        delete m;
        return -1;
    }
    *out_type = type;
    *out_msg = m;
    return 0;
}

extern "C" void ble_app_free(uint16_t /*type*/, void *msg)
{
    delete static_cast<google::protobuf::MessageLite *>(msg);
}

/* ------------------------------------------------------------------ */
/* C-accessible field getters for OTA messages                         */
/* ------------------------------------------------------------------ */

extern "C" const char *ble_app_ota_begin_filename(const void *msg)
{
    return static_cast<const ble::app::OtaBegin *>(msg)->filename().c_str();
}

extern "C" uint64_t ble_app_ota_begin_total_size(const void *msg)
{
    return static_cast<const ble::app::OtaBegin *>(msg)->total_size();
}

extern "C" uint32_t ble_app_ota_begin_chunk_size(const void *msg)
{
    return static_cast<const ble::app::OtaBegin *>(msg)->chunk_size();
}

extern "C" uint32_t ble_app_ota_chunk_seq(const void *msg)
{
    return static_cast<const ble::app::OtaChunk *>(msg)->seq();
}

extern "C" const uint8_t *ble_app_ota_chunk_data(const void *msg,
                                                  size_t *out_len)
{
    const auto &d = static_cast<const ble::app::OtaChunk *>(msg)->data();
    if (out_len) *out_len = d.size();
    return reinterpret_cast<const uint8_t *>(d.data());
}

extern "C" uint64_t ble_app_ota_end_crc64(const void *msg)
{
    return static_cast<const ble::app::OtaEnd *>(msg)->crc64();
}

/* ------------------------------------------------------------------ */
/* C-accessible message constructors for Ack responses                 */
/* ------------------------------------------------------------------ */

extern "C" void *ble_app_ota_begin_ack_new(int ok, const char *error)
{
    auto *m = new ble::app::OtaBeginAck();
    m->set_ok(ok != 0);
    if (error && error[0]) m->set_error(error);
    return m;
}

extern "C" void *ble_app_ota_chunk_ack_new(uint32_t seq, int ok)
{
    auto *m = new ble::app::OtaChunkAck();
    m->set_seq(seq);
    m->set_ok(ok != 0);
    return m;
}

extern "C" void *ble_app_ota_end_ack_new(int ok, const char *error)
{
    auto *m = new ble::app::OtaEndAck();
    m->set_ok(ok != 0);
    if (error && error[0]) m->set_error(error);
    return m;
}
