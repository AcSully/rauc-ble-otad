#include "app_dispatch.h"
#include "app.pb.h"

#include <google/protobuf/message_lite.h>
#include <cstdio>

namespace {

google::protobuf::MessageLite *make_message(uint16_t type)
{
    switch (type) {
    case BLE_APP_PING:              return new ble::app::Ping();
    case BLE_APP_PONG:              return new ble::app::Pong();
    case BLE_APP_OTA_BEGIN:         return new ble::app::OtaBegin();
    case BLE_APP_OTA_BEGIN_ACK:     return new ble::app::OtaBeginAck();
    case BLE_APP_OTA_CHUNK:         return new ble::app::OtaChunk();
    case BLE_APP_OTA_CHUNK_ACK:     return new ble::app::OtaChunkAck();
    case BLE_APP_OTA_END:           return new ble::app::OtaEnd();
    case BLE_APP_OTA_END_ACK:       return new ble::app::OtaEndAck();
    case BLE_APP_GET_VERSION:       return new ble::app::GetVersion();
    case BLE_APP_VERSION_REPLY:     return new ble::app::VersionReply();
    case BLE_APP_MISSING_CHUNKS:    return new ble::app::MissingChunks();
    case BLE_APP_OTA_INSTALL:       return new ble::app::OtaInstall();
    case BLE_APP_OTA_INSTALL_REPLY: return new ble::app::OtaInstallReply();
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

extern "C" size_t ble_app_encoded_size(uint16_t /*type*/, const void *msg)
{
    if (!msg) return 0;
    const auto *m = static_cast<const google::protobuf::MessageLite *>(msg);
    return BLE_APP_TYPE_HDR_LEN + m->ByteSizeLong();
}

/* ---- getters ---- */

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

extern "C" const uint8_t *ble_app_ota_chunk_data(const void *msg, size_t *out_len)
{
    const auto &d = static_cast<const ble::app::OtaChunk *>(msg)->data();
    if (out_len) *out_len = d.size();
    return reinterpret_cast<const uint8_t *>(d.data());
}

extern "C" uint64_t ble_app_ota_end_crc64(const void *msg)
{
    return static_cast<const ble::app::OtaEnd *>(msg)->crc64();
}

extern "C" const uint8_t *ble_app_ota_install_sha256(const void *msg, size_t *out_len)
{
    const auto &d = static_cast<const ble::app::OtaInstall *>(msg)->sha256();
    if (out_len) *out_len = d.size();
    return reinterpret_cast<const uint8_t *>(d.data());
}

/* ---- constructors ---- */

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

extern "C" void *ble_app_version_reply_new(const char *version)
{
    auto *m = new ble::app::VersionReply();
    if (version) m->set_version(version);
    return m;
}

extern "C" void *ble_app_missing_chunks_new(int ok, const char *error,
                                            const uint8_t *content, size_t content_len)
{
    auto *m = new ble::app::MissingChunks();
    m->set_ok(ok != 0);
    if (error && error[0]) m->set_error(error);
    if (content && content_len > 0) {
        m->set_content(reinterpret_cast<const char *>(content), content_len);
    }
    return m;
}

extern "C" void *ble_app_ota_install_reply_new(int ok, const char *error)
{
    auto *m = new ble::app::OtaInstallReply();
    m->set_ok(ok != 0);
    if (error && error[0]) m->set_error(error);
    return m;
}

/* ---- human-readable describe ---- */

namespace {

const char *type_name(uint16_t type)
{
    switch (type) {
    case BLE_APP_PING:              return "PING";
    case BLE_APP_PONG:              return "PONG";
    case BLE_APP_OTA_BEGIN:         return "OTA_BEGIN";
    case BLE_APP_OTA_BEGIN_ACK:     return "OTA_BEGIN_ACK";
    case BLE_APP_OTA_CHUNK:         return "OTA_CHUNK";
    case BLE_APP_OTA_CHUNK_ACK:     return "OTA_CHUNK_ACK";
    case BLE_APP_OTA_END:           return "OTA_END";
    case BLE_APP_OTA_END_ACK:       return "OTA_END_ACK";
    case BLE_APP_GET_VERSION:       return "GET_VERSION";
    case BLE_APP_VERSION_REPLY:     return "VERSION_REPLY";
    case BLE_APP_MISSING_CHUNKS:    return "MISSING_CHUNKS";
    case BLE_APP_OTA_INSTALL:       return "OTA_INSTALL";
    case BLE_APP_OTA_INSTALL_REPLY: return "OTA_INSTALL_REPLY";
    default:                        return "UNKNOWN";
    }
}

int clamp_written(int n, size_t cap)
{
    if (n < 0) return 0;
    if ((size_t)n >= cap) return (int)(cap - 1);
    return n;
}

} // namespace

extern "C" int ble_app_describe(uint16_t type, const void *msg,
                                char *buf, size_t cap)
{
    if (!buf || cap == 0) return 0;
    buf[0] = '\0';
    const char *name = type_name(type);
    int n = 0;

    switch (type) {
    case BLE_APP_PING: {
        const auto *m = static_cast<const ble::app::Ping *>(msg);
        n = std::snprintf(buf, cap, "%s nonce=%llu", name,
                          (unsigned long long)m->nonce());
        break;
    }
    case BLE_APP_PONG: {
        const auto *m = static_cast<const ble::app::Pong *>(msg);
        n = std::snprintf(buf, cap, "%s nonce=%llu", name,
                          (unsigned long long)m->nonce());
        break;
    }
    case BLE_APP_OTA_BEGIN: {
        const auto *m = static_cast<const ble::app::OtaBegin *>(msg);
        n = std::snprintf(buf, cap,
                          "%s filename=\"%s\" total_size=%llu chunk_size=%u",
                          name, m->filename().c_str(),
                          (unsigned long long)m->total_size(),
                          m->chunk_size());
        break;
    }
    case BLE_APP_OTA_BEGIN_ACK: {
        const auto *m = static_cast<const ble::app::OtaBeginAck *>(msg);
        n = std::snprintf(buf, cap, "%s ok=%s error=\"%s\"",
                          name, m->ok() ? "true" : "false",
                          m->error().c_str());
        break;
    }
    case BLE_APP_OTA_CHUNK: {
        const auto *m = static_cast<const ble::app::OtaChunk *>(msg);
        n = std::snprintf(buf, cap, "%s seq=%u data_len=%zu",
                          name, m->seq(), m->data().size());
        break;
    }
    case BLE_APP_OTA_CHUNK_ACK: {
        const auto *m = static_cast<const ble::app::OtaChunkAck *>(msg);
        n = std::snprintf(buf, cap, "%s seq=%u ok=%s",
                          name, m->seq(), m->ok() ? "true" : "false");
        break;
    }
    case BLE_APP_OTA_END: {
        const auto *m = static_cast<const ble::app::OtaEnd *>(msg);
        n = std::snprintf(buf, cap, "%s crc64=%llu", name,
                          (unsigned long long)m->crc64());
        break;
    }
    case BLE_APP_OTA_END_ACK: {
        const auto *m = static_cast<const ble::app::OtaEndAck *>(msg);
        n = std::snprintf(buf, cap, "%s ok=%s error=\"%s\"",
                          name, m->ok() ? "true" : "false",
                          m->error().c_str());
        break;
    }
    case BLE_APP_GET_VERSION:
        n = std::snprintf(buf, cap, "%s", name);
        break;
    case BLE_APP_VERSION_REPLY: {
        const auto *m = static_cast<const ble::app::VersionReply *>(msg);
        n = std::snprintf(buf, cap, "%s version=\"%s\"",
                          name, m->version().c_str());
        break;
    }
    case BLE_APP_MISSING_CHUNKS: {
        const auto *m = static_cast<const ble::app::MissingChunks *>(msg);
        n = std::snprintf(buf, cap,
                          "%s ok=%s error=\"%s\" content_len=%zu",
                          name, m->ok() ? "true" : "false",
                          m->error().c_str(), m->content().size());
        break;
    }
    case BLE_APP_OTA_INSTALL: {
        const auto *m = static_cast<const ble::app::OtaInstall *>(msg);
        const auto &d = m->sha256();
        char hex[65] = {0};
        size_t limit = d.size() > 32 ? 32 : d.size();
        for (size_t i = 0; i < limit; i++)
            std::snprintf(hex + i * 2, 3, "%02x", (uint8_t)d[i]);
        n = std::snprintf(buf, cap, "%s sha256=%s", name, hex);
        break;
    }
    case BLE_APP_OTA_INSTALL_REPLY: {
        const auto *m = static_cast<const ble::app::OtaInstallReply *>(msg);
        n = std::snprintf(buf, cap, "%s ok=%s error=\"%s\"",
                          name, m->ok() ? "true" : "false",
                          m->error().c_str());
        break;
    }
    default:
        n = std::snprintf(buf, cap, "UNKNOWN(0x%04x)", type);
        break;
    }

    return clamp_written(n, cap);
}
