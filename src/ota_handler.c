/*
 * ota_handler.c -- OTA file-receive state machine
 *
 * Manages the lifecycle of a single OTA transfer: OtaBegin opens a
 * file, OtaChunk appends data, OtaEnd closes and optionally verifies
 * a CRC-64.  Only one transfer may be active at a time.
 */

#include "ota_handler.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum ota_state {
    OTA_IDLE,
    OTA_RECEIVING,
    OTA_DONE,
};

struct ota_ctx {
    char        *dest_dir;
    char        *file_path;
    FILE        *fp;
    enum ota_state state;
    uint64_t    total_size;
    uint32_t    chunk_size;
    uint32_t    next_seq;
    uint64_t    bytes_written;
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Sanitise a filename coming from the phone: reject path separators
 * and empty strings so we never write outside dest_dir. */
static int filename_ok(const char *name)
{
    if (!name || name[0] == '\0') return 0;
    if (strchr(name, '/'))  return 0;
    if (strchr(name, '\\')) return 0;
    if (name[0] == '.')     return 0;  /* no hidden / dot-dot */
    return 1;
}

static void reset_transfer(struct ota_ctx *ctx)
{
    if (ctx->fp) {
        fclose(ctx->fp);
        ctx->fp = NULL;
    }
    free(ctx->file_path);
    ctx->file_path     = NULL;
    ctx->state         = OTA_IDLE;
    ctx->total_size    = 0;
    ctx->chunk_size    = 0;
    ctx->next_seq      = 0;
    ctx->bytes_written = 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

struct ota_ctx *ota_ctx_new(const char *dest_dir)
{
    if (!dest_dir) return NULL;

    struct ota_ctx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->dest_dir = strdup(dest_dir);
    if (!ctx->dest_dir) { free(ctx); return NULL; }

    ctx->state = OTA_IDLE;
    return ctx;
}

void ota_ctx_free(struct ota_ctx *ctx)
{
    if (!ctx) return;
    reset_transfer(ctx);
    free(ctx->dest_dir);
    free(ctx);
}

/* ---- OtaBegin ---------------------------------------------------- */

void ota_handle_begin(struct ota_ctx *ctx,
                      const char *filename,
                      uint64_t total_size,
                      uint32_t chunk_size,
                      int *ok, const char **error)
{
    *ok    = 0;
    *error = "";

    if (!ctx) { *error = "no ota context"; return; }

    /* If a previous transfer was in progress, abort it. */
    if (ctx->state == OTA_RECEIVING) {
        fprintf(stderr, "ota: aborting previous transfer\n");
        reset_transfer(ctx);
    }

    if (!filename_ok(filename)) {
        *error = "invalid filename";
        return;
    }
    if (total_size == 0) {
        *error = "total_size is zero";
        return;
    }
    if (chunk_size == 0) {
        *error = "chunk_size is zero";
        return;
    }

    /* Build destination path: dest_dir/filename */
    size_t dir_len  = strlen(ctx->dest_dir);
    size_t name_len = strlen(filename);
    /* +2 for '/' and '\0' */
    char *path = malloc(dir_len + 1 + name_len + 1);
    if (!path) { *error = "out of memory"; return; }
    memcpy(path, ctx->dest_dir, dir_len);
    path[dir_len] = '/';
    memcpy(path + dir_len + 1, filename, name_len + 1);

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "ota: cannot open %s: %s\n", path, strerror(errno));
        free(path);
        *error = "cannot open file";
        return;
    }

    ctx->file_path     = path;
    ctx->fp            = fp;
    ctx->total_size    = total_size;
    ctx->chunk_size    = chunk_size;
    ctx->next_seq      = 0;
    ctx->bytes_written = 0;
    ctx->state         = OTA_RECEIVING;

    fprintf(stderr, "ota: begin %s (%lu bytes, chunk %u)\n",
            path, (unsigned long)total_size, chunk_size);

    *ok = 1;
}

/* ---- OtaChunk ---------------------------------------------------- */

void ota_handle_chunk(struct ota_ctx *ctx,
                      uint32_t seq,
                      const uint8_t *data, size_t data_len,
                      uint32_t *ack_seq, int *ok)
{
    *ack_seq = seq;
    *ok      = 0;

    if (!ctx || ctx->state != OTA_RECEIVING) return;

    if (seq != ctx->next_seq) {
        fprintf(stderr, "ota: seq mismatch: expected %u got %u\n",
                ctx->next_seq, seq);
        return;
    }

    if (data_len == 0 || data_len > ctx->chunk_size) {
        fprintf(stderr, "ota: bad chunk len %zu (chunk_size %u)\n",
                data_len, ctx->chunk_size);
        return;
    }

    if (ctx->bytes_written + data_len > ctx->total_size) {
        fprintf(stderr, "ota: data exceeds total_size\n");
        return;
    }

    size_t written = fwrite(data, 1, data_len, ctx->fp);
    if (written != data_len) {
        fprintf(stderr, "ota: fwrite error: %s\n", strerror(errno));
        return;
    }

    ctx->bytes_written += data_len;
    ctx->next_seq++;
    *ok = 1;
}

/* ---- OtaEnd ------------------------------------------------------ */

void ota_handle_end(struct ota_ctx *ctx,
                    uint64_t crc64,
                    int *ok, const char **error)
{
    *ok    = 0;
    *error = "";

    (void)crc64;  /* TODO: CRC-64 verification */

    if (!ctx || ctx->state != OTA_RECEIVING) {
        *error = "no active transfer";
        return;
    }

    /* Flush and close the file. */
    if (fclose(ctx->fp) != 0) {
        fprintf(stderr, "ota: fclose error: %s\n", strerror(errno));
        ctx->fp = NULL;
        *error  = "file close error";
        ctx->state = OTA_IDLE;
        return;
    }
    ctx->fp = NULL;

    if (ctx->bytes_written != ctx->total_size) {
        fprintf(stderr, "ota: size mismatch: got %lu expected %lu\n",
                (unsigned long)ctx->bytes_written,
                (unsigned long)ctx->total_size);
        *error = "size mismatch";
        ctx->state = OTA_IDLE;
        return;
    }

    ctx->state = OTA_DONE;
    fprintf(stderr, "ota: transfer complete: %s (%lu bytes)\n",
            ctx->file_path, (unsigned long)ctx->bytes_written);

    *ok = 1;
}

const char *ota_ctx_path(const struct ota_ctx *ctx)
{
    if (!ctx || ctx->state != OTA_DONE) return NULL;
    return ctx->file_path;
}
