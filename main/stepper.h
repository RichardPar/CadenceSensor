// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Richard Parsons

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @file stepper.h
 * @brief Stepper motor driver for magnetic brake positioning.
 *
 * Drives a stepper motor via a step/direction interface compatible with
 * common drivers such as the A4988 or DRV8825.
 *
 * The driver tracks an absolute step position.  Position 0 is the home
 * (minimum resistance) end-stop, established by calling stepper_home().
 * Positive positions move the magnet assembly towards the flywheel,
 * increasing resistance.
 *
 * GPIO assignments are defined in config.h.
 */

/**
 * Initialise the step and direction GPIO pins.
 * Must be called once before any other stepper function.
 * The motor is not moved; call stepper_home() to establish the zero position.
 */
void stepper_init(void);

/**
 * Move the motor to the home position (position 0) by stepping toward the
 * end-stop until the limit switch is triggered, then zeroing the position
 * counter.
 *
 * Blocks until homing is complete.
 */
void stepper_home(void);

/**
 * Move to an absolute step position.
 * Positive values increase resistance (magnet closer to flywheel).
 * The move is clamped to [0, STEPPER_MAX_STEPS].
 *
 * Blocks until the move is complete.
 *
 * @param position  Target step position.
 */
void stepper_move_to(int32_t position);

/**
 * Return the current absolute step position.
 */
int32_t stepper_get_position(void);
