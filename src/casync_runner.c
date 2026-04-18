/*
 * casync_runner.c -- thin GLib-based wrappers around casync and rauc.
 *
 * Everything here is synchronous; the caller blocks until the spawned
 * process exits. This keeps the OTA state machine simple; the BLE link
 * goes quiet during these phases.
 */

#include "casync_runner.h"

#include <glib.h>
#include <gio/gio.h>

#include <stdio.h>
#include <string.h>

static const char *const SEEDS[] = {
    "--seed=/dev/disk/by-partlabel/fip-a",
    "--seed=/dev/disk/by-partlabel/fip-b",
    "--seed=/dev/disk/by-partlabel/rootfs-a",
    "--seed=/dev/disk/by-partlabel/rootfs-b",
};
#define N_SEEDS ((int)(sizeof(SEEDS) / sizeof(SEEDS[0])))

/* Spawn argv[] synchronously; capture stdout if out_stdout is non-NULL. */
static int run_argv(char **argv, gchar **out_stdout, const char **err)
{
    GError *gerr = NULL;
    gchar  *stdout_buf = NULL;
    gchar  *stderr_buf = NULL;
    gint    exit_status = 0;

    gboolean ok = g_spawn_sync(NULL, argv, NULL,
                               G_SPAWN_SEARCH_PATH,
                               NULL, NULL,
                               out_stdout ? &stdout_buf : NULL,
                               &stderr_buf,
                               &exit_status, &gerr);
    if (!ok) {
        fprintf(stderr, "spawn failed: %s\n", gerr ? gerr->message : "?");
        if (gerr) g_error_free(gerr);
        g_free(stdout_buf);
        g_free(stderr_buf);
        *err = "spawn failed";
        return -1;
    }

    if (!g_spawn_check_wait_status(exit_status, &gerr)) {
        fprintf(stderr, "process exited non-zero: %s\n",
                gerr ? gerr->message : "?");
        if (stderr_buf && stderr_buf[0])
            fprintf(stderr, "stderr: %s\n", stderr_buf);
        if (gerr) g_error_free(gerr);
        g_free(stdout_buf);
        g_free(stderr_buf);
        *err = "process failed";
        return -1;
    }

    g_free(stderr_buf);
    if (out_stdout) *out_stdout = stdout_buf;
    return 0;
}

int casync_list_chunks(const char *caibx_path,
                       char **out_content, size_t *out_len,
                       const char **err)
{
    *err = "";
    if (!caibx_path || !out_content || !out_len) {
        *err = "bad args"; return -1;
    }

    char *argv[3 + N_SEEDS + 2];
    int i = 0;
    argv[i++] = (char *)"casync";
    argv[i++] = (char *)"list-chunks";
    for (int s = 0; s < N_SEEDS; s++) argv[i++] = (char *)SEEDS[s];
    argv[i++] = (char *)caibx_path;
    argv[i++] = NULL;

    gchar *stdout_buf = NULL;
    if (run_argv(argv, &stdout_buf, err) != 0) return -1;

    *out_len = stdout_buf ? strlen(stdout_buf) : 0;
    *out_content = stdout_buf;
    return 0;
}

int casync_extract(const char *caibx_path,
                   const char *store_dir,
                   const char *out_path,
                   const char **err)
{
    *err = "";
    if (!caibx_path || !store_dir || !out_path) {
        *err = "bad args"; return -1;
    }

    gchar *store_arg = g_strdup_printf("--store=%s", store_dir);

    char *argv[3 + N_SEEDS + 3];
    int i = 0;
    argv[i++] = (char *)"casync";
    argv[i++] = (char *)"extract";
    argv[i++] = store_arg;
    for (int s = 0; s < N_SEEDS; s++) argv[i++] = (char *)SEEDS[s];
    argv[i++] = (char *)caibx_path;
    argv[i++] = (char *)out_path;
    argv[i++] = NULL;

    int rc = run_argv(argv, NULL, err);
    g_free(store_arg);
    return rc;
}

int sha256_file(const char *path, uint8_t digest[32], const char **err)
{
    *err = "";
    FILE *fp = fopen(path, "rb");
    if (!fp) { *err = "cannot open file for sha256"; return -1; }

    GChecksum *c = g_checksum_new(G_CHECKSUM_SHA256);
    uint8_t buf[64 * 1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        g_checksum_update(c, buf, n);
    }
    int read_err = ferror(fp);
    fclose(fp);
    if (read_err) {
        g_checksum_free(c);
        *err = "read error during sha256";
        return -1;
    }

    gsize len = 32;
    g_checksum_get_digest(c, digest, &len);
    g_checksum_free(c);
    if (len != 32) { *err = "sha256 length mismatch"; return -1; }
    return 0;
}

int rauc_install(const char *path, const char **err)
{
    *err = "";
    if (!path) { *err = "bad args"; return -1; }

    char *argv[] = {
        (char *)"rauc", (char *)"install", (char *)path, NULL,
    };
    return run_argv(argv, NULL, err);
}
