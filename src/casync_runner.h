#ifndef RAUC_BLE_CASYNC_RUNNER_H
#define RAUC_BLE_CASYNC_RUNNER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Run casync list-chunks against caibx_path with the four hard-coded
 * partition-label seeds. On success *out_content is a g_malloc'd buffer
 * holding raw stdout (newline-separated "<ab>/<hash>.cacnk" paths) and
 * *out_len is its length. Caller frees with g_free.
 * Returns 0 on success; on error returns -1 and sets *err to a static
 * string describing the failure. */
int casync_list_chunks(const char *caibx_path,
                       char **out_content, size_t *out_len,
                       const char **err);

/* Run casync extract to rebuild out_path from caibx_path + store_dir,
 * with the same seed list. Returns 0 on success. */
int casync_extract(const char *caibx_path,
                   const char *store_dir,
                   const char *out_path,
                   const char **err);

/* Compute SHA-256 of a file; writes 32 raw bytes into digest[]. */
int sha256_file(const char *path, uint8_t digest[32], const char **err);

/* Run `rauc install <path>` synchronously. Blocks until rauc exits.
 * Returns 0 on success. */
int rauc_install(const char *path, const char **err);

#ifdef __cplusplus
}
#endif

#endif
