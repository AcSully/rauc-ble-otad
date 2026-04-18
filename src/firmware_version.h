#ifndef RAUC_BLE_FIRMWARE_VERSION_H
#define RAUC_BLE_FIRMWARE_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

/* Read VERSION_ID from /data/os-release.
 * Returns a malloc'd string owned by the caller (free with free()).
 * Returns "unknown" (also malloc'd) on any error. */
char *firmware_version_read(void);

#ifdef __cplusplus
}
#endif

#endif
