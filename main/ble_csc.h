// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Richard Parsons

#pragma once

#include "hall_sensor.h"  /* csc_measurement_t */
#include <stdbool.h>

/**
 * @file ble_csc.h
 * @brief BLE Cycling Speed and Cadence (CSC) GATT peripheral.
 *
 * Implements the Bluetooth CSC Profile (CSP_SPEC v1.0) on top of the
 * ESP-IDF NimBLE host stack.  The following GATT characteristics are
 * exposed under the CSC Service (0x1816):
 *
 *   0x2A5B  CSC Measurement     — Notify
 *   0x2A5C  CSC Feature         — Read
 *   0x2A5D  Sensor Location     — Read
 *   0x2A55  SC Control Point    — Write / Indicate
 */

/**
 * Initialise the NimBLE stack, register the CSC GATT service, and begin
 * advertising.  Must be called after nvs_flash_init().
 */
void ble_csc_init(void);

/**
 * Send a CSC Measurement notification to the connected central.
 * Silently does nothing when no client is connected or has not subscribed.
 */
void ble_csc_send_measurement(const csc_measurement_t *meas);

/** Returns true while a BLE central is connected. */
bool ble_csc_is_connected(void);
