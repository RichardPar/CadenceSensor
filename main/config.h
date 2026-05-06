// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Richard Parsons

#pragma once

/* --------------------------------------------------------------------------
 * Hardware — GPIO pin assignments
 *
 * Sensor: SS41F unipolar Hall-effect switch (active-low, open-collector).
 * Output is pulled high by the ESP32 internal pull-up and driven low when a
 * magnet (south pole) passes the sensor face.  Falling-edge interrupts are
 * used to detect each revolution.
 * -------------------------------------------------------------------------- */

/** GPIO connected to the wheel hall-effect sensor output. */
#define HALL_WHEEL_GPIO         18

/** GPIO connected to the crank hall-effect sensor output. */
#define HALL_CRANK_GPIO         19

/* --------------------------------------------------------------------------
 * Hall sensor — timing
 * -------------------------------------------------------------------------- */

/**
 * Minimum time between accepted pulses on each sensor, in milliseconds.
 * Pulses arriving faster than this are treated as contact bounce and ignored.
 */
#define HALL_DEBOUNCE_MS        50

/* --------------------------------------------------------------------------
 * Wheel geometry
 * -------------------------------------------------------------------------- */

/** Wheel circumference in millimetres (700c road bike tyre, ~25 mm width). */
#define WHEEL_CIRCUMFERENCE_MM  2096

/* --------------------------------------------------------------------------
 * BLE
 * -------------------------------------------------------------------------- */

/** BLE device name broadcast in advertising packets. */
#define DEVICE_NAME             "CadenceSensor"

/**
 * Sensor Location (characteristic 0x2A5D).
 * Value 6 = "Rear Wheel" per the BT GATT Assigned Numbers specification.
 */
#define SENSOR_LOCATION         6U
