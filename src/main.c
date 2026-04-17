/*
 * main.c -- rauc-ble-otad daemon entry point
 */

#include "gatt_server.h"
#include <gio/gio.h>
#include <signal.h>
#include <stdio.h>

static GMainLoop *loop;

static void on_signal(int sig)
{
    (void)sig;
    if (loop) g_main_loop_quit(loop);
}

static void on_bus_acquired(GObject *source, GAsyncResult *res,
                            gpointer user_data)
{
    (void)source; (void)user_data;
    GError *err = NULL;
    GDBusConnection *conn = g_bus_get_finish(res, &err);
    if (err) {
        g_printerr("D-Bus connection failed: %s\n", err->message);
        g_error_free(err);
        g_main_loop_quit(loop);
        return;
    }

    if (gatt_server_start(conn, NULL) != 0) {
        g_printerr("GATT server start failed\n");
        g_main_loop_quit(loop);
    }
}

int main(void)
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    loop = g_main_loop_new(NULL, FALSE);

    g_bus_get(G_BUS_TYPE_SYSTEM, NULL, on_bus_acquired, NULL);

    g_print("rauc-ble-otad starting...\n");
    g_main_loop_run(loop);

    gatt_server_stop();
    g_main_loop_unref(loop);
    g_print("rauc-ble-otad exiting.\n");
    return 0;
}
