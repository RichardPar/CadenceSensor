// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Richard Parsons

#include "stepper.h"
#include "config.h"

#include <inttypes.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "rom/ets_sys.h"

static const char *TAG = "stepper";

/* Current absolute position in steps (0 = home / minimum resistance). */
static int32_t s_position = 0;

/* ---- helpers ------------------------------------------------------------- */

static inline void step_pulse(void)
{
    gpio_set_level(STEPPER_STEP_GPIO, 1);
    ets_delay_us(STEPPER_PULSE_US);
    gpio_set_level(STEPPER_STEP_GPIO, 0);
    ets_delay_us(STEPPER_STEP_PERIOD_US - STEPPER_PULSE_US);
}

/* ---- Public API ---------------------------------------------------------- */

void stepper_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << STEPPER_STEP_GPIO) | (1ULL << STEPPER_DIR_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    gpio_set_level(STEPPER_STEP_GPIO, 0);
    gpio_set_level(STEPPER_DIR_GPIO,  0);

    ESP_LOGI(TAG, "Stepper initialised — STEP GPIO %d, DIR GPIO %d",
             STEPPER_STEP_GPIO, STEPPER_DIR_GPIO);
}

void stepper_home(void)
{
    ESP_LOGI(TAG, "Homing — stepping to position 0");

    /* Step in the decreasing direction until the limit switch fires or we
     * have walked back the full travel range (safety limit). */
    gpio_set_level(STEPPER_DIR_GPIO, STEPPER_DIR_DECREASE);

    for (int32_t i = 0; i < STEPPER_MAX_STEPS; i++) {
#if STEPPER_LIMIT_GPIO >= 0
        if (gpio_get_level(STEPPER_LIMIT_GPIO) == 0) {
            /* Limit switch active-low — we are at the end-stop. */
            break;
        }
#endif
        step_pulse();
    }

    s_position = 0;
    ESP_LOGI(TAG, "Homing complete");
}

void stepper_move_to(int32_t target)
{
    if (target < 0)                target = 0;
    if (target > STEPPER_MAX_STEPS) target = STEPPER_MAX_STEPS;

    int32_t delta = target - s_position;
    if (delta == 0) return;

    gpio_set_level(STEPPER_DIR_GPIO,
                   delta > 0 ? STEPPER_DIR_INCREASE : STEPPER_DIR_DECREASE);

    int32_t steps = delta > 0 ? delta : -delta;
    for (int32_t i = 0; i < steps; i++) {
        step_pulse();
    }

    s_position = target;
    ESP_LOGD(TAG, "Moved to position %" PRId32, s_position);
}

int32_t stepper_get_position(void)
{
    return s_position;
}
