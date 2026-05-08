// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Richard Parsons

#pragma once

/* --------------------------------------------------------------------------
 * Hardware — GPIO pin assignments
 *
 * Sensors: SS49E linear ratiometric Hall-effect sensor.
 * Output idles at Vcc/2 (~1.65 V on 3.3 V supply) with no magnetic field.
 * As a magnet passes, the output swings above or below the GPIO threshold
 * producing a clean pulse.  Internal pull resistors MUST be disabled so they
 * do not disturb the quiescent operating point.
 * -------------------------------------------------------------------------- */

/** GPIO connected to the wheel SS49E sensor output. */
#define HALL_WHEEL_GPIO         18

/** GPIO connected to the crank SS49E sensor output. */
#define HALL_CRANK_GPIO         33

/* --------------------------------------------------------------------------
 * Sensor — interrupt edge
 *
 * SS49E output swings above the HIGH threshold when a south pole approaches
 * and below the LOW threshold for a north pole.  ANYEDGE captures whichever
 * direction the magnet produces and the pulse-width filter (HALL_CRANK_MIN_PULSE_US)
 * rejects noise spikes from the quiescent ~1.65 V region.
 *
 * Use GPIO_INTR_POSEDGE / NEGEDGE if only one pole faces the sensor.
 * -------------------------------------------------------------------------- */

/** Interrupt edge for the wheel proximity sensor. */
#define HALL_WHEEL_INTR_EDGE    GPIO_INTR_NEGEDGE

/**
 * The crank ISR uses both edges to measure pulse width; do not change this.
 * Polarity is handled in software: a count is accepted on the falling edge
 * only if the preceding HIGH pulse was at least HALL_CRANK_MIN_PULSE_US long.
 */
#define HALL_CRANK_INTR_EDGE    GPIO_INTR_ANYEDGE

/* --------------------------------------------------------------------------
 * Sensor — timing
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
#define SINGLE_SENSOR_MODE      1

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
 * 500 ms → max ~120 RPM cadence; rejects electrical noise from the stepper
 * motor driver coupling into the sensor GPIO.
 */
#define HALL_DEBOUNCE_MS        500

/**
 * Minimum duration the crank sensor output must stay HIGH before the pulse
 * is accepted as a real revolution.  Noise spikes from the stepper driver are
 * typically sub-microsecond; a genuine sensor pass lasts several milliseconds.
 * Increase if spurious counts persist; decrease if slow-speed counts are missed.
 */
#define HALL_CRANK_MIN_PULSE_US 5000   /* 5 ms */

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
