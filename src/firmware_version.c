/*
 * firmware_version.c -- parse VERSION_ID from /data/os-release.
 *
 * Format is the standard os-release key=value file, e.g.
 *   VERSION_ID="0.0.1"
 * Surrounding double-quotes are stripped.
 */

#include "firmware_version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OS_RELEASE_PATH "/data/os-release"
#define KEY             "VERSION_ID"

static char *dup_unknown(void) { return strdup("unknown"); }

static char *strip_quotes(char *s)
{
    size_t n = strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        s[n - 1] = '\0';
        s++;
    }
    return s;
}

char *firmware_version_read(void)
{
    FILE *fp = fopen(OS_RELEASE_PATH, "r");
    if (!fp) return dup_unknown();

    char *result = NULL;
    char line[256];
    const size_t key_len = strlen(KEY);

    while (fgets(line, sizeof(line), fp)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';

        if (strncmp(line, KEY, key_len) != 0) continue;
        if (line[key_len] != '=')             continue;

        char *val = strip_quotes(line + key_len + 1);
        result = strdup(val);
        break;
    }

    fclose(fp);
    return result ? result : dup_unknown();
}
