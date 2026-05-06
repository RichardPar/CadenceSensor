// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Richard Parsons

#include "ble_csc.h"
#include "hall_sensor.h"
#include "config.h"

#include <assert.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_csc";

/* --------------------------------------------------------------------------
 * Constants
 * -------------------------------------------------------------------------- */

/**
 * CSC Feature flags (characteristic 0x2A5C):
 *   Bit 0 — Wheel Revolution Data supported
 *   Bit 1 — Crank Revolution Data supported
 */
#define CSC_FEATURE_FLAGS        0x0003U

/* CSC Measurement flags (characteristic 0x2A5B). */
#define CSC_MEAS_FLAG_WHEEL_REV  0x01U
#define CSC_MEAS_FLAG_CRANK_REV  0x02U

/* SC Control Point opcodes and response values. */
#define SC_CTRL_OP_RESET_WHEEL   0x01U  /* Reset Cumulative Value */
#define SC_CTRL_OP_RESPONSE      0x10U  /* Response Code */
#define SC_CTRL_RESP_SUCCESS     0x01U
#define SC_CTRL_RESP_OP_UNSUP    0x02U  /* Op Code Not Supported */

/* --------------------------------------------------------------------------
 * Module state
 * -------------------------------------------------------------------------- */

static uint8_t  s_own_addr_type;
static uint16_t s_conn_handle    = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_meas_handle;      /* val_handle for CSC Measurement (notify) */
static uint16_t s_ctrl_pt_handle;   /* val_handle for SC Control Point (indicate) */

/* --------------------------------------------------------------------------
 * Forward declarations
 * -------------------------------------------------------------------------- */

static void ble_csc_advertise(void);
static int  ble_csc_gap_event(struct ble_gap_event *event, void *arg);
static int  ble_csc_gatt_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg);

/* --------------------------------------------------------------------------
 * GATT service definition
 * -------------------------------------------------------------------------- */

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        /* Cycling Speed and Cadence Service — UUID 0x1816 */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1816),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* CSC Measurement (0x2A5B) — notify */
                .uuid       = BLE_UUID16_DECLARE(0x2A5B),
                .access_cb  = ble_csc_gatt_cb,
                .arg        = (void *)(uintptr_t)0x2A5B,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_meas_handle,
            },
            {
                /* CSC Feature (0x2A5C) — read */
                .uuid      = BLE_UUID16_DECLARE(0x2A5C),
                .access_cb = ble_csc_gatt_cb,
                .arg       = (void *)(uintptr_t)0x2A5C,
                .flags     = BLE_GATT_CHR_F_READ,
            },
            {
                /* Sensor Location (0x2A5D) — read */
                .uuid      = BLE_UUID16_DECLARE(0x2A5D),
                .access_cb = ble_csc_gatt_cb,
                .arg       = (void *)(uintptr_t)0x2A5D,
                .flags     = BLE_GATT_CHR_F_READ,
            },
            {
                /* SC Control Point (0x2A55) — write + indicate
                 * Required by CSC Profile when Wheel Revolution Data is
                 * supported (CSP_SPEC §3.1 Table 3.1). */
                .uuid       = BLE_UUID16_DECLARE(0x2A55),
                .access_cb  = ble_csc_gatt_cb,
                .arg        = (void *)(uintptr_t)0x2A55,
                .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_INDICATE,
                .val_handle = &s_ctrl_pt_handle,
            },
            { 0 }, /* end of characteristics */
        },
    },
    { 0 }, /* end of services */
};

/* --------------------------------------------------------------------------
 * GATT access callback
 * -------------------------------------------------------------------------- */

static int ble_csc_gatt_cb(uint16_t conn_h, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t uuid = (uint16_t)(uintptr_t)arg;
    int rc;

    switch (uuid) {

        case 0x2A5C: { /* CSC Feature — read */
            uint16_t feat = CSC_FEATURE_FLAGS;
            rc = os_mbuf_append(ctxt->om, &feat, sizeof(feat));
            return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }

        case 0x2A5D: { /* Sensor Location — read */
            uint8_t loc = SENSOR_LOCATION;
            rc = os_mbuf_append(ctxt->om, &loc, sizeof(loc));
            return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }

        case 0x2A55: { /* SC Control Point — write */
            if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
                return BLE_ATT_ERR_UNLIKELY;
            }

            uint8_t opcode = 0;
            os_mbuf_copydata(ctxt->om, 0, sizeof(opcode), &opcode);

            uint8_t resp[3];
            resp[0] = SC_CTRL_OP_RESPONSE;
            resp[1] = opcode;

            if (opcode == SC_CTRL_OP_RESET_WHEEL) {
                hall_sensor_reset_wheel_revs();
                resp[2] = SC_CTRL_RESP_SUCCESS;
                ESP_LOGI(TAG, "Wheel revolution counter reset by client");
            } else {
                resp[2] = SC_CTRL_RESP_OP_UNSUP;
                ESP_LOGW(TAG, "Unsupported SC Control Point opcode: 0x%02x", opcode);
            }

            /* Send indication back to the client.  If the client has not
             * subscribed to indications, NimBLE returns an error we can ignore. */
            struct os_mbuf *om = ble_hs_mbuf_from_flat(resp, sizeof(resp));
            if (om != NULL) {
                ble_gatts_indicate_custom(conn_h, s_ctrl_pt_handle, om);
            }
            return 0;
        }

        default:
            return BLE_ATT_ERR_UNLIKELY;
    }
}

/* --------------------------------------------------------------------------
 * GAP event handler
 * -------------------------------------------------------------------------- */

static int ble_csc_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ESP_LOGI(TAG, "Connected; handle=%d",
                         event->connect.conn_handle);
                s_conn_handle = event->connect.conn_handle;
            } else {
                ESP_LOGI(TAG, "Connection failed; status=%d, restarting adv",
                         event->connect.status);
                ble_csc_advertise();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Disconnected; reason=0x%02x — restarting adv",
                     event->disconnect.reason);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ble_csc_advertise();
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGI(TAG, "Advertising timed out — restarting");
            ble_csc_advertise();
            break;

        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG, "Subscription change; handle=%d notify=%d indicate=%d",
                     event->subscribe.attr_handle,
                     event->subscribe.cur_notify,
                     event->subscribe.cur_indicate);
            break;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU updated; conn=%d mtu=%d",
                     event->mtu.conn_handle, event->mtu.value);
            break;

        default:
            break;
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * Advertising
 * -------------------------------------------------------------------------- */

static void ble_csc_advertise(void)
{
    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields  fields     = {0};
    int rc;

    /* Flags: General Discoverable, BR/EDR not supported. */
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* Advertise the CSC service UUID so scanners can filter by service. */
    static const ble_uuid16_t csc_uuid = BLE_UUID16_INIT(0x1816);
    fields.uuids16             = &csc_uuid;
    fields.num_uuids16         = 1;
    fields.uuids16_is_complete = 1;

    /* Complete local name. */
    fields.name             = (const uint8_t *)DEVICE_NAME;
    fields.name_len         = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed; rc=%d", rc);
        return;
    }

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;   /* undirected connectable */
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;   /* general discoverable  */

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                            &adv_params, ble_csc_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed; rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "Advertising as \"%s\"", DEVICE_NAME);
    }
}

/* --------------------------------------------------------------------------
 * NimBLE host callbacks
 * -------------------------------------------------------------------------- */

static void ble_on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed; rc=%d", rc);
        return;
    }

    uint8_t addr[6] = {0};
    ble_hs_id_copy_addr(s_own_addr_type, addr, NULL);
    ESP_LOGI(TAG, "BLE MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

    ble_csc_advertise();
}

static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE host stack reset; reason=%d", reason);
}

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();               /* blocks until nimble_port_stop() */
    nimble_port_freertos_deinit();
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

void ble_csc_init(void)
{
    int rc;

    nimble_port_init();

    ble_hs_cfg.sync_cb  = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    /* Initialise the mandatory GAP and GATT services. */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    /* Register the CSC GATT service. */
    rc = ble_gatts_count_cfg(s_gatt_svcs);
    assert(rc == 0);

    rc = ble_gatts_add_svcs(s_gatt_svcs);
    assert(rc == 0);

    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    assert(rc == 0);

    /* Start the NimBLE host task — advertising begins inside ble_on_sync(). */
    nimble_port_freertos_init(ble_host_task);
}

void ble_csc_send_measurement(const csc_measurement_t *meas)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }

    /*
     * CSC Measurement packet layout (Flags = 0x03 — both wheel and crank):
     *
     *  Offset  Size  Field
     *  ------  ----  -----
     *     0      1   Flags (0x03)
     *     1      4   Cumulative Wheel Revolutions (uint32 LE)
     *     5      2   Last Wheel Event Time        (uint16 LE, 1/1024 s)
     *     7      2   Cumulative Crank Revolutions (uint16 LE)
     *     9      2   Last Crank Event Time        (uint16 LE, 1/1024 s)
     *
     * Total: 11 bytes.
     */
    uint8_t buf[11];
    buf[0]  = CSC_MEAS_FLAG_WHEEL_REV | CSC_MEAS_FLAG_CRANK_REV;
    buf[1]  = (uint8_t)(meas->cumulative_wheel_revs >>  0);
    buf[2]  = (uint8_t)(meas->cumulative_wheel_revs >>  8);
    buf[3]  = (uint8_t)(meas->cumulative_wheel_revs >> 16);
    buf[4]  = (uint8_t)(meas->cumulative_wheel_revs >> 24);
    buf[5]  = (uint8_t)(meas->last_wheel_event_time >>  0);
    buf[6]  = (uint8_t)(meas->last_wheel_event_time >>  8);
    buf[7]  = (uint8_t)(meas->cumulative_crank_revs >>  0);
    buf[8]  = (uint8_t)(meas->cumulative_crank_revs >>  8);
    buf[9]  = (uint8_t)(meas->last_crank_event_time >>  0);
    buf[10] = (uint8_t)(meas->last_crank_event_time >>  8);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, sizeof(buf));
    if (om == NULL) {
        ESP_LOGE(TAG, "Failed to allocate notification mbuf");
        return;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_meas_handle, om);
    if (rc != 0 && rc != BLE_HS_ENOTCONN) {
        /* BLE_HS_ENOTCONN is expected if the client hasn't subscribed yet. */
        ESP_LOGD(TAG, "Notify returned rc=%d", rc);
    }
}

bool ble_csc_is_connected(void)
{
    return s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}
