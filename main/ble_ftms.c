// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Richard Parsons

#include "ble_ftms.h"
#include "config.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"

static const char *TAG = "ble_ftms";

/* --------------------------------------------------------------------------
 * Constants — Fitness Machine Service (UUID 0x1826)
 * -------------------------------------------------------------------------- */

/*
 * Indoor Bike Data flags field (first 2 bytes of notification, 0x2AD2).
 * Bits that are SET indicate the corresponding field is ABSENT (inverted
 * logic for the first two flags, normal for the rest — see FTMS spec §4.9).
 *
 * Bit 0:  More Data (0 = Instantaneous Speed present)
 * Bit 1:  Average Speed present
 * Bit 2:  Instantaneous Cadence present
 * Bit 3:  Average Cadence present
 * Bit 4:  Total Distance present
 * Bit 5:  Resistance Level present
 * Bit 6:  Instantaneous Power present
 * Bit 7:  Average Power present
 * Bit 8:  Expended Energy present
 * Bit 9:  Heart Rate present
 * Bit 10: Metabolic Equivalent present
 * Bit 11: Elapsed Time present
 * Bit 12: Remaining Time present
 *
 * We transmit: Instantaneous Speed, Instantaneous Cadence, Instantaneous Power.
 */
#define FTMS_INDOOR_BIKE_FLAGS   0x0044U  /* bits 2 and 6 set = cadence + power present,
                                             bit 0 clear = speed present */

/* Fitness Machine Feature flags (0x2ACC).
 *   Bit 3  — Cadence Supported
 *   Bit 14 — Power Measurement Supported
 */
#define FTMS_FEATURE_FLAGS       0x00004008U
#define FTMS_TARGET_SETTING_FLAGS 0x00000008U  /* Bit 3 — Target Power Supported */

/* Fitness Machine Control Point opcodes (0x2AD9). */
#define FTMS_OP_REQUEST_CONTROL   0x00U
#define FTMS_OP_SET_TARGET_POWER  0x05U
#define FTMS_OP_RESPONSE_CODE     0x80U

/* Control Point response values. */
#define FTMS_RESP_SUCCESS         0x01U
#define FTMS_RESP_OP_NOT_SUPP     0x02U
#define FTMS_RESP_INVALID_PARAM   0x03U

/* --------------------------------------------------------------------------
 * Module state
 * -------------------------------------------------------------------------- */

static uint16_t s_indoor_bike_handle;   /* val_handle for Indoor Bike Data (notify) */
static uint16_t s_ctrl_pt_handle;       /* val_handle for Control Point (indicate)  */

static volatile int16_t s_target_power_w  = 0;
static volatile bool    s_target_updated  = false;

/* --------------------------------------------------------------------------
 * GATT access callback
 * -------------------------------------------------------------------------- */

static int ftms_gatt_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t uuid = (uint16_t)(uintptr_t)arg;

    switch (uuid) {

        case 0x2ACC: { /* Fitness Machine Feature — read */
            uint32_t feat    = FTMS_FEATURE_FLAGS;
            uint32_t targets = FTMS_TARGET_SETTING_FLAGS;
            int rc = os_mbuf_append(ctxt->om, &feat,    sizeof(feat));
            rc    |= os_mbuf_append(ctxt->om, &targets, sizeof(targets));
            return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }

        case 0x2AD9: { /* Fitness Machine Control Point — write */
            if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
                return BLE_ATT_ERR_UNLIKELY;
            }

            uint8_t opcode = 0;
            os_mbuf_copydata(ctxt->om, 0, sizeof(opcode), &opcode);

            uint8_t resp[3];
            resp[0] = FTMS_OP_RESPONSE_CODE;
            resp[1] = opcode;

            switch (opcode) {
                case FTMS_OP_REQUEST_CONTROL:
                    resp[2] = FTMS_RESP_SUCCESS;
                    ESP_LOGI(TAG, "Client requested control");
                    break;

                case FTMS_OP_SET_TARGET_POWER: {
                    int16_t watts = 0;
                    if (OS_MBUF_PKTLEN(ctxt->om) < 3) {
                        resp[2] = FTMS_RESP_INVALID_PARAM;
                        break;
                    }
                    os_mbuf_copydata(ctxt->om, 1, sizeof(watts), &watts);
                    s_target_power_w = watts;
                    s_target_updated = true;
                    resp[2] = FTMS_RESP_SUCCESS;
                    ESP_LOGI(TAG, "Target power set to %" PRId16 " W", watts);
                    break;
                }

                default:
                    resp[2] = FTMS_RESP_OP_NOT_SUPP;
                    ESP_LOGW(TAG, "Unsupported FTMS opcode: 0x%02x", opcode);
                    break;
            }

            struct os_mbuf *om = ble_hs_mbuf_from_flat(resp, sizeof(resp));
            if (om != NULL) {
                ble_gatts_indicate_custom(conn_handle, s_ctrl_pt_handle, om);
            }
            return 0;
        }

        default:
            return BLE_ATT_ERR_UNLIKELY;
    }
}

/* --------------------------------------------------------------------------
 * GATT service definition
 * -------------------------------------------------------------------------- */

static const struct ble_gatt_svc_def s_ftms_svcs[] = {
    {
        /* Fitness Machine Service — UUID 0x1826 */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1826),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* Fitness Machine Feature (0x2ACC) — read */
                .uuid      = BLE_UUID16_DECLARE(0x2ACC),
                .access_cb = ftms_gatt_cb,
                .arg       = (void *)(uintptr_t)0x2ACC,
                .flags     = BLE_GATT_CHR_F_READ,
            },
            {
                /* Indoor Bike Data (0x2AD2) — notify */
                .uuid       = BLE_UUID16_DECLARE(0x2AD2),
                .access_cb  = ftms_gatt_cb,
                .arg        = (void *)(uintptr_t)0x2AD2,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_indoor_bike_handle,
            },
            {
                /* Fitness Machine Control Point (0x2AD9) — write + indicate */
                .uuid       = BLE_UUID16_DECLARE(0x2AD9),
                .access_cb  = ftms_gatt_cb,
                .arg        = (void *)(uintptr_t)0x2AD9,
                .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_INDICATE,
                .val_handle = &s_ctrl_pt_handle,
            },
            { 0 }, /* end of characteristics */
        },
    },
    { 0 }, /* end of services */
};

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

void ble_ftms_register_services(void)
{
    int rc = ble_gatts_count_cfg(s_ftms_svcs);
    assert(rc == 0);

    rc = ble_gatts_add_svcs(s_ftms_svcs);
    assert(rc == 0);

    ESP_LOGI(TAG, "FTMS service registered");
}

void ble_ftms_notify_indoor_bike(uint16_t conn_handle,
                                 const ftms_indoor_bike_t *data)
{
    /*
     * Indoor Bike Data packet layout (flags = FTMS_INDOOR_BIKE_FLAGS = 0x0044):
     *
     *  Offset  Size  Field
     *  ------  ----  -----
     *     0      2   Flags (0x0044)
     *     2      2   Instantaneous Speed (km/h × 100, uint16 LE)
     *     4      2   Instantaneous Cadence (rpm × 2, uint16 LE)
     *     6      2   Instantaneous Power (watts, int16 LE)
     *
     * Total: 8 bytes.
     */
    uint8_t buf[8];
    uint16_t flags = FTMS_INDOOR_BIKE_FLAGS;
    buf[0] = (uint8_t)(flags >> 0);
    buf[1] = (uint8_t)(flags >> 8);
    buf[2] = (uint8_t)(data->instantaneous_speed_kmh_x100 >> 0);
    buf[3] = (uint8_t)(data->instantaneous_speed_kmh_x100 >> 8);
    buf[4] = (uint8_t)(data->instantaneous_cadence_x2 >> 0);
    buf[5] = (uint8_t)(data->instantaneous_cadence_x2 >> 8);
    buf[6] = (uint8_t)(data->instantaneous_power_w >> 0);
    buf[7] = (uint8_t)(data->instantaneous_power_w >> 8);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, sizeof(buf));
    if (om == NULL) {
        ESP_LOGE(TAG, "Failed to allocate FTMS notification mbuf");
        return;
    }

    int rc = ble_gatts_notify_custom(conn_handle, s_indoor_bike_handle, om);
    if (rc != 0 && rc != BLE_HS_ENOTCONN) {
        ESP_LOGD(TAG, "FTMS notify returned rc=%d", rc);
    }
}

int16_t ble_ftms_get_target_power(void)
{
    return s_target_power_w;
}

bool ble_ftms_target_power_updated(void)
{
    if (s_target_updated) {
        s_target_updated = false;
        return true;
    }
    return false;
}
