#ifndef RAUC_BLE_OTA_HANDLER_H
#define RAUC_BLE_OTA_HANDLER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* OTA transfer state (opaque to callers). */
struct ota_ctx;

/* Allocate a new OTA context.
 * `dest_dir` is the directory where the received bundle will be stored
 * (e.g. "/tmp").  The filename comes from OtaBegin.  Returns NULL on
 * allocation failure. */
struct ota_ctx *ota_ctx_new(const char *dest_dir);

/* Free an OTA context and close any open file. */
void ota_ctx_free(struct ota_ctx *ctx);

/* ---- handlers called from handle_message ---- */

/* Process OtaBegin.  Opens the destination file and records transfer
 * parameters.  Writes result into *ok / *error (error is a static string,
 * caller must NOT free it). */
void ota_handle_begin(struct ota_ctx *ctx,
                      const char *filename,
                      uint64_t total_size,
                      uint32_t chunk_size,
                      int *ok, const char **error);

/* Process OtaChunk.  Writes chunk data to the file.
 * Returns the ack seq and ok flag. */
void ota_handle_chunk(struct ota_ctx *ctx,
                      uint32_t seq,
                      const uint8_t *data, size_t data_len,
                      uint32_t *ack_seq, int *ok);

/* Process OtaEnd.  Closes the file, optionally verifies CRC-64.
 * Writes result into *ok / *error.  On success the completed file
 * path is available via ota_ctx_path(). */
void ota_handle_end(struct ota_ctx *ctx,
                    uint64_t crc64,
                    int *ok, const char **error);

/* Return the full path of the received file (valid after a successful
 * OtaEnd).  Returns NULL if no transfer has completed. */
const char *ota_ctx_path(const struct ota_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
