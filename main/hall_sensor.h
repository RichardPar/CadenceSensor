// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Richard Parsons

#pragma once

#include <stdint.h>
#include "config.h"

/**
 * @file hall_sensor.h
 * @brief SS49E linear Hall-effect sensor driver for wheel and crank revolution counting.
 *
 * The SS49E output idles at Vcc/2 with no field present.  As a magnet passes,
 * the output swings above or below the GPIO threshold, producing a pulse whose
 * width corresponds to the time the magnet face is near the sensor.  The crank
 * ISR measures that pulse width to distinguish real passes from noise spikes.
 * Internal GPIO pull resistors must be disabled on all sensor pins.
 */

/**
 * CSC Measurement data as defined by the Bluetooth CSC Measurement
 * characteristic (UUID 0x2A5B).
 *
 * Event times are 16-bit rolling counters in units of 1/1024 seconds.
 * Receivers are expected to handle roll-over (per the BT spec).
 */
typedef struct {
    uint32_t cumulative_wheel_revs;  /**< Total wheel revolutions since last reset. */
    uint16_t last_wheel_event_time;  /**< Timestamp of last wheel pulse (1/1024 s). */
    uint16_t cumulative_crank_revs;  /**< Total crank revolutions since power-on. */
    uint16_t last_crank_event_time;  /**< Timestamp of last crank pulse (1/1024 s). */
    uint32_t crank_raw_triggers;     /**< Raw ISR entry count before debounce (debug). */
    uint32_t crank_rise_count;       /**< Rising edges seen — 0 means wrong pin or dead sensor (debug). */
    uint32_t crank_rejected;         /**< Falling edges rejected: pulse too short (debug). */
} csc_measurement_t;

/**
 * Initialise GPIO pins and install ISR handlers for both hall sensors.
 * Must be called once before any other hall-sensor function.
 */
void hall_sensor_init(void);

/**
 * Return the current wheel speed in km/h, computed from the interval between
 * the last two wheel pulses.  Returns 0.0 if no pulses have been received or
 * the wheel has been stationary for more than 3 seconds.
 */
float hall_sensor_get_speed_kmh(void);

/**
 * Atomically copy the current sensor counters into @p meas.
 */
void hall_sensor_get_measurement(csc_measurement_t *meas);

/**
 * Reset the cumulative wheel revolution counter to zero.
 * Called when the BLE SC Control Point receives a Reset Cumulative Value
 * command (opcode 0x01).  Crank revolutions are unaffected.
 */
void hall_sensor_reset_wheel_revs(void);
