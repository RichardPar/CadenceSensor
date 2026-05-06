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
 * Set to 1 to use only the crank hall sensor and derive wheel revolutions
 * from the fixed gear ratio defined below.  Set to 0 to fit a dedicated
 * wheel sensor.
 *
 * Because the wheel and crank are coupled by a fixed gear, wheel speed can
 * be computed from cadence alone:
 *   wheel_revs = crank_revs × GEAR_RATIO_CHAINRING / GEAR_RATIO_SPROCKET
 */
#define SINGLE_SENSOR_MODE      0

/**
 * Wheel revolutions per crank revolution.
 * Calculated as chainring teeth ÷ sprocket teeth.
 * Used only when SINGLE_SENSOR_MODE = 1.
 *
 * Examples:
 *   50T chainring, 17T sprocket → 50.0f / 17.0f ≈ 2.94f
 *   42T chainring, 16T sprocket → 42.0f / 16.0f  = 2.625f
 */
#define GEAR_RATIO              (50.0f / 17.0f)

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

/* --------------------------------------------------------------------------
 * Stepper motor — magnetic brake
 *
 * Step/direction interface compatible with A4988 / DRV8825 drivers.
 * Set STEPPER_LIMIT_GPIO to -1 if no limit switch is fitted (homing will
 * rely solely on the STEPPER_MAX_STEPS safety limit).
 * -------------------------------------------------------------------------- */

/** GPIO connected to the driver STEP input. */
#define STEPPER_STEP_GPIO       25

/** GPIO connected to the driver DIR input. */
#define STEPPER_DIR_GPIO        26

/**
 * GPIO connected to the homing limit switch (active-low).
 * Set to -1 if no limit switch is fitted.
 */
#define STEPPER_LIMIT_GPIO      -1

/** Direction level that moves the magnet toward the flywheel (more resistance). */
#define STEPPER_DIR_INCREASE    1

/** Direction level that moves the magnet away from the flywheel (less resistance). */
#define STEPPER_DIR_DECREASE    0

/** Maximum travel in steps from home (tune to match your physical mechanism). */
#define STEPPER_MAX_STEPS       2000

/** Step pulse high time in microseconds (A4988 minimum is 1 µs). */
#define STEPPER_PULSE_US        2

/** Total period per step in microseconds — sets motor speed (500 µs = 2000 steps/s). */
#define STEPPER_STEP_PERIOD_US  500
