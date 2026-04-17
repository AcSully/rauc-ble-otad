/*
 * gatt_server.c -- BlueZ GATT peripheral for OTA via GDBus
 *
 * Registers a custom GATT service with two characteristics (RX write,
 * TX notify) and a BLE advertisement so that a phone can connect and
 * push OTA data.  Incoming PDUs are fed through the ble_reasm / app_dispatch
 * pipeline; responses are packed via ble_pack and sent as notifications.
 */

#include "gatt_server.h"
#include "ble_reasm.h"
#include "ble_pack.h"
#include "app_dispatch.h"
#include "ota_handler.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* UUIDs                                                               */
/* ------------------------------------------------------------------ */
#define OTA_SERVICE_UUID  "12345678-1234-5678-1234-56789abcdef0"
#define OTA_RX_CHAR_UUID  "12345678-1234-5678-1234-56789abcdef1"
#define OTA_TX_CHAR_UUID  "12345678-1234-5678-1234-56789abcdef2"

/* D-Bus object paths */
#define APP_PATH          "/com/ota"
#define SVC_PATH          "/com/ota/service0"
#define CHAR_RX_PATH      "/com/ota/service0/char_rx"
#define CHAR_TX_PATH      "/com/ota/service0/char_tx"
#define ADV_PATH          "/com/ota/adv"

/* BLE ATT default payload (20 bytes = MTU 23 - 3 ATT header) */
#define DEFAULT_ATT_PAYLOAD  20u

/* Max reassembly buffer — each BLE message is one app-layer chunk
 * (OtaChunk ~4 KB payload + protobuf overhead + type header).
 * Files are split at the application layer, not the BLE layer. */
#define REASM_BUF_SIZE  (8u * 1024u)

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */
static GDBusConnection *g_conn;
static guint g_app_reg;
static guint g_svc_reg;
static guint g_char_rx_reg;
static guint g_char_tx_reg;
static guint g_adv_reg;

static struct ble_reasm_ctx g_reasm;
static uint8_t g_reasm_buf[REASM_BUF_SIZE];

/* OTA receive context */
static struct ota_ctx *g_ota;

/* TX notification state */
static gboolean g_tx_notifying;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static uint64_t now_ms(void)
{
    return (uint64_t)(g_get_monotonic_time() / 1000);
}

static GVariant *make_byte_array(const uint8_t *data, size_t len)
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("ay"));
    for (size_t i = 0; i < len; i++)
        g_variant_builder_add(&builder, "y", data[i]);
    return g_variant_builder_end(&builder);
}

/* Send one notification PDU on the TX characteristic via
 * PropertiesChanged on the Value property — this is how BlueZ
 * picks up GATT notifications from external applications. */
static void tx_notify(const uint8_t *pdu, size_t len)
{
    if (!g_tx_notifying || !g_conn) return;

    GVariantBuilder changed;
    g_variant_builder_init(&changed, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&changed, "{sv}", "Value",
                          make_byte_array(pdu, len));

    GVariantBuilder invalidated;
    g_variant_builder_init(&invalidated, G_VARIANT_TYPE("as"));

    GError *err = NULL;
    g_dbus_connection_emit_signal(
        g_conn, NULL,
        CHAR_TX_PATH,
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        g_variant_new("(sa{sv}as)",
                      "org.bluez.GattCharacteristic1",
                      &changed,
                      &invalidated),
        &err);

    if (err) {
        g_printerr("tx_notify: %s\n", err->message);
        g_error_free(err);
    } else {
        g_print("tx_notify: sent %zu bytes\n", len);
    }
}

/* Pack and send a response message as notifications on TX char. */
static void send_response(uint16_t type, const void *msg)
{
    uint8_t enc_buf[4096];
    size_t enc_len = 0;

    if (ble_app_encode(type, msg, enc_buf, sizeof(enc_buf), &enc_len) != 0) {
        g_printerr("send_response: encode failed\n");
        return;
    }

    struct ble_pack_iter it;
    if (ble_pack_iter_init(&it, enc_buf, (uint32_t)enc_len, DEFAULT_ATT_PAYLOAD) != 0) {
        g_printerr("send_response: pack init failed\n");
        return;
    }

    uint8_t frame[DEFAULT_ATT_PAYLOAD];
    size_t frame_len = 0;
    while (ble_pack_iter_next(&it, frame, sizeof(frame), &frame_len) > 0) {
        tx_notify(frame, frame_len);
    }
}

/* Handle a fully reassembled message. */
static void handle_message(const uint8_t *msg, uint32_t len)
{
    uint16_t type = 0;
    void *decoded = NULL;

    if (ble_app_decode(msg, len, &type, &decoded) != 0) {
        g_printerr("handle_message: decode failed\n");
        return;
    }

    g_print("handle_message: type=0x%04x len=%u\n", type, len);

    switch (type) {
    case BLE_APP_PING:
        send_response(BLE_APP_PONG, decoded);
        break;

    case BLE_APP_OTA_BEGIN: {
        int ok = 0;
        const char *error = "";
        ota_handle_begin(g_ota,
                         ble_app_ota_begin_filename(decoded),
                         ble_app_ota_begin_total_size(decoded),
                         ble_app_ota_begin_chunk_size(decoded),
                         &ok, &error);
        void *ack = ble_app_ota_begin_ack_new(ok, error);
        send_response(BLE_APP_OTA_BEGIN_ACK, ack);
        ble_app_free(BLE_APP_OTA_BEGIN_ACK, ack);
        break;
    }

    case BLE_APP_OTA_CHUNK: {
        size_t data_len = 0;
        const uint8_t *data = ble_app_ota_chunk_data(decoded, &data_len);
        uint32_t ack_seq = 0;
        int ok = 0;
        ota_handle_chunk(g_ota,
                         ble_app_ota_chunk_seq(decoded),
                         data, data_len,
                         &ack_seq, &ok);
        void *ack = ble_app_ota_chunk_ack_new(ack_seq, ok);
        send_response(BLE_APP_OTA_CHUNK_ACK, ack);
        ble_app_free(BLE_APP_OTA_CHUNK_ACK, ack);
        break;
    }

    case BLE_APP_OTA_END: {
        int ok = 0;
        const char *error = "";
        ota_handle_end(g_ota,
                       ble_app_ota_end_crc64(decoded),
                       &ok, &error);
        void *ack = ble_app_ota_end_ack_new(ok, error);
        send_response(BLE_APP_OTA_END_ACK, ack);
        ble_app_free(BLE_APP_OTA_END_ACK, ack);

        if (ok) {
            const char *path = ota_ctx_path(g_ota);
            g_print("OTA complete: %s\n", path ? path : "(null)");
            /* TODO: invoke RAUC D-Bus Install(path) here */
        }
        break;
    }

    default:
        g_print("handle_message: unhandled type 0x%04x\n", type);
        break;
    }

    ble_app_free(type, decoded);
}

/* ------------------------------------------------------------------ */
/* GDBus introspection XML                                             */
/* ------------------------------------------------------------------ */

/* org.bluez.GattApplication1 -- just needs GetManagedObjects via
 * ObjectManager interface, which GDBusObjectManagerServer handles.
 * We take the simpler approach: export a raw ObjectManager method. */

static const gchar app_introspect_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.DBus.ObjectManager'>"
    "    <method name='GetManagedObjects'>"
    "      <arg name='objects' type='a{oa{sa{sv}}}' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static const gchar svc_introspect_xml[] =
    "<node>"
    "  <interface name='org.bluez.GattService1'>"
    "    <property name='UUID' type='s' access='read'/>"
    "    <property name='Primary' type='b' access='read'/>"
    "  </interface>"
    "</node>";

static const gchar char_rx_introspect_xml[] =
    "<node>"
    "  <interface name='org.bluez.GattCharacteristic1'>"
    "    <method name='WriteValue'>"
    "      <arg name='value' type='ay' direction='in'/>"
    "      <arg name='options' type='a{sv}' direction='in'/>"
    "    </method>"
    "    <property name='UUID' type='s' access='read'/>"
    "    <property name='Service' type='o' access='read'/>"
    "    <property name='Flags' type='as' access='read'/>"
    "  </interface>"
    "</node>";

static const gchar char_tx_introspect_xml[] =
    "<node>"
    "  <interface name='org.bluez.GattCharacteristic1'>"
    "    <method name='StartNotify'/>"
    "    <method name='StopNotify'/>"
    "    <property name='UUID' type='s' access='read'/>"
    "    <property name='Service' type='o' access='read'/>"
    "    <property name='Flags' type='as' access='read'/>"
    "    <property name='Value' type='ay' access='read'/>"
    "    <property name='Notifying' type='b' access='read'/>"
    "  </interface>"
    "</node>";

static const gchar adv_introspect_xml[] =
    "<node>"
    "  <interface name='org.bluez.LEAdvertisement1'>"
    "    <method name='Release'/>"
    "    <property name='Type' type='s' access='read'/>"
    "    <property name='ServiceUUIDs' type='as' access='read'/>"
    "    <property name='LocalName' type='s' access='read'/>"
    "  </interface>"
    "</node>";

/* ------------------------------------------------------------------ */
/* Application (ObjectManager): GetManagedObjects                      */
/* ------------------------------------------------------------------ */

static void add_svc_properties(GVariantBuilder *ifaces)
{
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&props, "{sv}", "UUID",
                          g_variant_new_string(OTA_SERVICE_UUID));
    g_variant_builder_add(&props, "{sv}", "Primary",
                          g_variant_new_boolean(TRUE));
    g_variant_builder_add(ifaces, "{sa{sv}}",
                          "org.bluez.GattService1", &props);
}

static void add_char_properties(GVariantBuilder *ifaces,
                                const char *uuid, const char *svc,
                                const char **flags, int nflags)
{
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&props, "{sv}", "UUID",
                          g_variant_new_string(uuid));
    g_variant_builder_add(&props, "{sv}", "Service",
                          g_variant_new_object_path(svc));

    GVariantBuilder flag_array;
    g_variant_builder_init(&flag_array, G_VARIANT_TYPE("as"));
    for (int i = 0; i < nflags; i++)
        g_variant_builder_add(&flag_array, "s", flags[i]);
    g_variant_builder_add(&props, "{sv}", "Flags",
                          g_variant_builder_end(&flag_array));

    g_variant_builder_add(ifaces, "{sa{sv}}",
                          "org.bluez.GattCharacteristic1", &props);
}

static void handle_get_managed_objects(GDBusConnection *conn,
                                       const gchar *sender,
                                       const gchar *object_path,
                                       const gchar *interface_name,
                                       const gchar *method_name,
                                       GVariant *parameters,
                                       GDBusMethodInvocation *invocation,
                                       gpointer user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)method_name;
    (void)parameters; (void)user_data;

    GVariantBuilder objects;
    g_variant_builder_init(&objects, G_VARIANT_TYPE("a{oa{sa{sv}}}"));

    /* Service */
    {
        GVariantBuilder ifaces;
        g_variant_builder_init(&ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
        add_svc_properties(&ifaces);
        g_variant_builder_add(&objects, "{oa{sa{sv}}}", SVC_PATH, &ifaces);
    }

    /* RX characteristic */
    {
        GVariantBuilder ifaces;
        g_variant_builder_init(&ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
        const char *flags[] = {"write-without-response"};
        add_char_properties(&ifaces, OTA_RX_CHAR_UUID, SVC_PATH,
                            flags, 1);
        g_variant_builder_add(&objects, "{oa{sa{sv}}}", CHAR_RX_PATH, &ifaces);
    }

    /* TX characteristic */
    {
        GVariantBuilder ifaces;
        g_variant_builder_init(&ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
        const char *flags[] = {"notify"};
        add_char_properties(&ifaces, OTA_TX_CHAR_UUID, SVC_PATH,
                            flags, 1);
        g_variant_builder_add(&objects, "{oa{sa{sv}}}", CHAR_TX_PATH, &ifaces);
    }

    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(a{oa{sa{sv}}})", &objects));
}

/* ------------------------------------------------------------------ */
/* RX characteristic: WriteValue handler                               */
/* ------------------------------------------------------------------ */

static void handle_rx_write(GDBusConnection *conn,
                            const gchar *sender,
                            const gchar *object_path,
                            const gchar *interface_name,
                            const gchar *method_name,
                            GVariant *parameters,
                            GDBusMethodInvocation *invocation,
                            gpointer user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)method_name; (void)user_data;

    GVariant *value_var = NULL;
    GVariant *options_var = NULL;
    g_variant_get(parameters, "(@ay@a{sv})", &value_var, &options_var);

    gsize pdu_len = 0;
    const guint8 *pdu = g_variant_get_fixed_array(value_var, &pdu_len,
                                                   sizeof(guint8));

    const uint8_t *out_msg = NULL;
    uint32_t out_len = 0;
    int rc = ble_reasm_feed(&g_reasm, pdu, pdu_len, now_ms(),
                            &out_msg, &out_len);

    if (rc == BLE_REASM_DELIVER) {
        g_print("gatt_rx: reassembled %u bytes\n", out_len);
        handle_message(out_msg, out_len);
    } else if (rc == BLE_REASM_DROP) {
        g_printerr("gatt_rx: dropped frame (%zu bytes)\n", pdu_len);
    }

    g_variant_unref(value_var);
    g_variant_unref(options_var);
    g_dbus_method_invocation_return_value(invocation, NULL);
}

/* ------------------------------------------------------------------ */
/* TX characteristic: StartNotify / StopNotify                         */
/* ------------------------------------------------------------------ */

static void handle_tx_method(GDBusConnection *conn,
                             const gchar *sender,
                             const gchar *object_path,
                             const gchar *interface_name,
                             const gchar *method_name,
                             GVariant *parameters,
                             GDBusMethodInvocation *invocation,
                             gpointer user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)parameters; (void)user_data;

    if (g_strcmp0(method_name, "StartNotify") == 0) {
        g_tx_notifying = TRUE;
        g_print("TX: notifications enabled\n");
    } else if (g_strcmp0(method_name, "StopNotify") == 0) {
        g_tx_notifying = FALSE;
        g_print("TX: notifications disabled\n");
    }

    g_dbus_method_invocation_return_value(invocation, NULL);
}

/* ------------------------------------------------------------------ */
/* Property getters (shared by all objects)                            */
/* ------------------------------------------------------------------ */

static GVariant *handle_svc_get_property(GDBusConnection *conn,
                                         const gchar *sender,
                                         const gchar *object_path,
                                         const gchar *interface_name,
                                         const gchar *property_name,
                                         GError **error,
                                         gpointer user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)user_data;

    if (g_strcmp0(property_name, "UUID") == 0)
        return g_variant_new_string(OTA_SERVICE_UUID);
    if (g_strcmp0(property_name, "Primary") == 0)
        return g_variant_new_boolean(TRUE);

    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                "No such property: %s", property_name);
    return NULL;
}

static GVariant *handle_char_rx_get_property(GDBusConnection *conn,
                                             const gchar *sender,
                                             const gchar *object_path,
                                             const gchar *interface_name,
                                             const gchar *property_name,
                                             GError **error,
                                             gpointer user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)user_data;

    if (g_strcmp0(property_name, "UUID") == 0)
        return g_variant_new_string(OTA_RX_CHAR_UUID);
    if (g_strcmp0(property_name, "Service") == 0)
        return g_variant_new_object_path(SVC_PATH);
    if (g_strcmp0(property_name, "Flags") == 0) {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("as"));
        g_variant_builder_add(&b, "s", "write-without-response");
        return g_variant_builder_end(&b);
    }

    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                "No such property: %s", property_name);
    return NULL;
}

static GVariant *handle_char_tx_get_property(GDBusConnection *conn,
                                             const gchar *sender,
                                             const gchar *object_path,
                                             const gchar *interface_name,
                                             const gchar *property_name,
                                             GError **error,
                                             gpointer user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)user_data;

    if (g_strcmp0(property_name, "UUID") == 0)
        return g_variant_new_string(OTA_TX_CHAR_UUID);
    if (g_strcmp0(property_name, "Service") == 0)
        return g_variant_new_object_path(SVC_PATH);
    if (g_strcmp0(property_name, "Flags") == 0) {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("as"));
        g_variant_builder_add(&b, "s", "notify");
        return g_variant_builder_end(&b);
    }
    if (g_strcmp0(property_name, "Notifying") == 0)
        return g_variant_new_boolean(g_tx_notifying);
    if (g_strcmp0(property_name, "Value") == 0)
        return make_byte_array(NULL, 0);

    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                "No such property: %s", property_name);
    return NULL;
}

static GVariant *handle_adv_get_property(GDBusConnection *conn,
                                         const gchar *sender,
                                         const gchar *object_path,
                                         const gchar *interface_name,
                                         const gchar *property_name,
                                         GError **error,
                                         gpointer user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)user_data;

    if (g_strcmp0(property_name, "Type") == 0)
        return g_variant_new_string("peripheral");
    if (g_strcmp0(property_name, "LocalName") == 0)
        return g_variant_new_string("OTA-STM32MP2");
    if (g_strcmp0(property_name, "ServiceUUIDs") == 0) {
        const gchar *uuids[] = {OTA_SERVICE_UUID, NULL};
        return g_variant_new_strv(uuids, 1);
    }

    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                "No such property: %s", property_name);
    return NULL;
}

/* Advertisement Release (called by BlueZ when adv is removed) */
static void handle_adv_method(GDBusConnection *conn,
                              const gchar *sender,
                              const gchar *object_path,
                              const gchar *interface_name,
                              const gchar *method_name,
                              GVariant *parameters,
                              GDBusMethodInvocation *invocation,
                              gpointer user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)method_name;
    (void)parameters; (void)user_data;

    g_print("Advertisement released\n");
    g_dbus_method_invocation_return_value(invocation, NULL);
}

/* ------------------------------------------------------------------ */
/* Register all objects on D-Bus                                       */
/* ------------------------------------------------------------------ */

static guint register_object(GDBusConnection *conn,
                             const gchar *path,
                             const gchar *xml,
                             const GDBusInterfaceVTable *vtable)
{
    GDBusNodeInfo *node = g_dbus_node_info_new_for_xml(xml, NULL);
    if (!node) {
        g_printerr("Failed to parse introspection XML for %s\n", path);
        return 0;
    }

    GError *err = NULL;
    guint id = g_dbus_connection_register_object(
        conn, path, node->interfaces[0], vtable, NULL, NULL, &err);

    g_dbus_node_info_unref(node);

    if (err) {
        g_printerr("register_object(%s): %s\n", path, err->message);
        g_error_free(err);
        return 0;
    }
    return id;
}

/* ------------------------------------------------------------------ */
/* BlueZ calls: RegisterApplication + RegisterAdvertisement            */
/* ------------------------------------------------------------------ */

static void on_register_app_reply(GObject *source, GAsyncResult *res,
                                  gpointer user_data)
{
    (void)user_data;
    GError *err = NULL;
    GVariant *ret = g_dbus_connection_call_finish(
        G_DBUS_CONNECTION(source), res, &err);
    if (err) {
        g_printerr("RegisterApplication failed: %s\n", err->message);
        g_error_free(err);
        return;
    }
    g_variant_unref(ret);
    g_print("GATT application registered with BlueZ\n");
}

static void on_register_adv_reply(GObject *source, GAsyncResult *res,
                                  gpointer user_data)
{
    (void)user_data;
    GError *err = NULL;
    GVariant *ret = g_dbus_connection_call_finish(
        G_DBUS_CONNECTION(source), res, &err);
    if (err) {
        g_printerr("RegisterAdvertisement failed: %s\n", err->message);
        g_error_free(err);
        return;
    }
    g_variant_unref(ret);
    g_print("BLE advertisement registered\n");
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int gatt_server_start(GDBusConnection *conn, const char *adapter_path)
{
    if (!conn) return -1;
    if (!adapter_path) adapter_path = "/org/bluez/hci0";

    g_conn = conn;

    /* Init reassembler */
    ble_reasm_init(&g_reasm, g_reasm_buf, sizeof(g_reasm_buf), 5000);

    /* Init OTA handler — received files go to /tmp */
    g_ota = ota_ctx_new("/tmp");
    if (!g_ota) {
        g_printerr("gatt_server: ota_ctx_new failed\n");
        return -1;
    }

    /* -- Register D-Bus objects ------------------------------------ */

    /* Application (ObjectManager) */
    static const GDBusInterfaceVTable app_vtable = {
        .method_call = handle_get_managed_objects,
    };
    g_app_reg = register_object(conn, APP_PATH, app_introspect_xml,
                                &app_vtable);
    if (!g_app_reg) return -1;

    /* Service */
    static const GDBusInterfaceVTable svc_vtable = {
        .method_call = NULL,
        .get_property = handle_svc_get_property,
    };
    g_svc_reg = register_object(conn, SVC_PATH, svc_introspect_xml,
                                &svc_vtable);

    /* RX Characteristic */
    static const GDBusInterfaceVTable rx_vtable = {
        .method_call = handle_rx_write,
        .get_property = handle_char_rx_get_property,
    };
    g_char_rx_reg = register_object(conn, CHAR_RX_PATH,
                                    char_rx_introspect_xml, &rx_vtable);

    /* TX Characteristic */
    static const GDBusInterfaceVTable tx_vtable = {
        .method_call = handle_tx_method,
        .get_property = handle_char_tx_get_property,
    };
    g_char_tx_reg = register_object(conn, CHAR_TX_PATH,
                                    char_tx_introspect_xml, &tx_vtable);

    /* Advertisement */
    static const GDBusInterfaceVTable adv_vtable = {
        .method_call = handle_adv_method,
        .get_property = handle_adv_get_property,
    };
    g_adv_reg = register_object(conn, ADV_PATH, adv_introspect_xml,
                                &adv_vtable);

    /* -- Call BlueZ to register the GATT application --------------- */
    g_dbus_connection_call(
        conn, "org.bluez", adapter_path,
        "org.bluez.GattManager1", "RegisterApplication",
        g_variant_new("(oa{sv})", APP_PATH, NULL),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
        on_register_app_reply, NULL);

    /* -- Register advertisement ------------------------------------ */
    g_dbus_connection_call(
        conn, "org.bluez", adapter_path,
        "org.bluez.LEAdvertisingManager1", "RegisterAdvertisement",
        g_variant_new("(oa{sv})", ADV_PATH, NULL),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
        on_register_adv_reply, NULL);

    g_print("gatt_server: starting on adapter %s\n", adapter_path);
    return 0;
}

void gatt_server_stop(void)
{
    if (!g_conn) return;

    if (g_app_reg)     g_dbus_connection_unregister_object(g_conn, g_app_reg);
    if (g_svc_reg)     g_dbus_connection_unregister_object(g_conn, g_svc_reg);
    if (g_char_rx_reg) g_dbus_connection_unregister_object(g_conn, g_char_rx_reg);
    if (g_char_tx_reg) g_dbus_connection_unregister_object(g_conn, g_char_tx_reg);
    if (g_adv_reg)     g_dbus_connection_unregister_object(g_conn, g_adv_reg);

    ota_ctx_free(g_ota);
    g_ota = NULL;

    g_app_reg = g_svc_reg = g_char_rx_reg = g_char_tx_reg = g_adv_reg = 0;
    g_conn = NULL;
    g_print("gatt_server: stopped\n");
}
