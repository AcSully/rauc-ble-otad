#ifndef RAUC_BLE_GATT_SERVER_H
#define RAUC_BLE_GATT_SERVER_H

#include <gio/gio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start the GATT peripheral: register the OTA service + advertising with
 * BlueZ over D-Bus.  The caller must already have a running GMainLoop.
 * adapter_path is e.g. "/org/bluez/hci0" (NULL = autodetect first adapter).
 * Returns 0 on success. */
int gatt_server_start(GDBusConnection *conn, const char *adapter_path);

void gatt_server_stop(void);

#ifdef __cplusplus
}
#endif

#endif
