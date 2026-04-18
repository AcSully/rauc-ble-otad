#ifndef RAUC_BLE_OTA_HANDLER_H
#define RAUC_BLE_OTA_HANDLER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ota_ctx;

struct ota_ctx *ota_ctx_new(const char *dest_dir);
void ota_ctx_free(struct ota_ctx *ctx);

void ota_handle_begin(struct ota_ctx *ctx,
                      const char *filename,
                      uint64_t total_size,
                      uint32_t chunk_size,
                      int *ok, const char **error);

void ota_handle_chunk(struct ota_ctx *ctx,
                      uint32_t seq,
                      const uint8_t *data, size_t data_len,
                      uint32_t *ack_seq, int *ok);

void ota_handle_end(struct ota_ctx *ctx,
                    uint64_t crc64,
                    int *ok, const char **error);

const char *ota_ctx_path(const struct ota_ctx *ctx);
const char *ota_ctx_filename(const struct ota_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
