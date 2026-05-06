// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Richard Parsons

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @file ble_ftms.h
 * @brief Fitness Machine Service (FTMS) BLE server — UUID 0x1826.
 *
 * Implements the Indoor Bike data path of the Bluetooth FTMS specification,
 * allowing apps such as Zwift and MyWhoosh to send ERG (target power) and
 * simulation (gradient, wind) resistance targets to the trainer.
 *
 * This module is independent of ble_csc.  Both services can run concurrently
 * on the same NimBLE stack once ble_ftms_register_services() is called before
 * ble_gatts_start().
 *
 * Usage (once hardware is ready):
 *   1. Call ble_ftms_register_services() during NimBLE init (before stack start).
 *   2. Call ble_ftms_notify_indoor_bike() at your notification interval to
 *      push speed/cadence/power data to the connected client.
 *   3. Poll ble_ftms_get_target_power() from your control loop to read the
 *      last power target written by the app.
 */

/**
 * Indoor Bike data to include in each FTMS notification.
 * Fields set to zero are omitted automatically if the corresponding flag
 * is cleared in FTMS_INDOOR_BIKE_FLAGS inside ble_ftms.c.
 */
typedef struct {
    uint16_t instantaneous_speed_kmh_x100;  /**< Speed in km/h × 100 (e.g. 2840 = 28.40 km/h) */
    uint16_t instantaneous_cadence_x2;      /**< Cadence in rpm × 2 (e.g. 180 = 90 rpm) */
    int16_t  instantaneous_power_w;         /**< Instantaneous power in watts */
} ftms_indoor_bike_t;

/**
 * Register the FTMS GATT service with the NimBLE stack.
 * Must be called after ble_svc_gap_init() / ble_svc_gatt_init() and before
 * ble_gatts_start() (i.e. during the same init sequence as ble_csc).
 */
void ble_ftms_register_services(void);

/**
 * Send an Indoor Bike Data notification to the connected client.
 * Has no effect if no client is connected or notifications are not enabled.
 *
 * @param conn_handle  BLE connection handle (from the GAP event callback).
 * @param data         Pointer to the current indoor bike data.
 */
void ble_ftms_notify_indoor_bike(uint16_t conn_handle,
                                 const ftms_indoor_bike_t *data);

/**
 * Return the most recent target power (watts) written by the client via the
 * Fitness Machine Control Point.  Returns 0 if no target has been set.
 */
int16_t ble_ftms_get_target_power(void);

/**
 * Return true if the client has sent a new target power since the last call
 * to this function (clears the flag on read).
 */
bool ble_ftms_target_power_updated(void);
