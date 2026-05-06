// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Richard Parsons

#include "hall_sensor.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "hall_sensor";

/* Spinlock protecting all sensor counters against concurrent access between
 * the ISR (any core) and the application task reading measurements. */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/* Wheel counters (ISR-updated) */
static volatile uint32_t s_wheel_revs           = 0;
static volatile uint64_t s_last_wheel_us         = 0;
static volatile uint16_t s_wheel_event_time_1024 = 0;
static volatile uint64_t s_wheel_interval_us     = 0; /* interval between last two pulses */

/* Crank counters (ISR-updated) */
static volatile uint16_t s_crank_revs          = 0;
static volatile uint64_t s_last_crank_us        = 0;
static volatile uint16_t s_crank_event_time_1024 = 0;

/**
 * Convert a microsecond timestamp to the 1/1024-second unit used by the
 * CSC Measurement characteristic.  The result is a 16-bit value and is
 * intentionally allowed to roll over (per BT spec §3.70).
 */
static inline uint16_t us_to_1024(uint64_t us)
{
    return (uint16_t)((us * 1024ULL) / 1000000ULL);
}

/* ---- ISR handlers ---------------------------------------------------- */

static void IRAM_ATTR wheel_isr(void *arg)
{
    uint64_t now = esp_timer_get_time();  /* safe to call from ISR */

    portENTER_CRITICAL_ISR(&s_mux);
    if (now - s_last_wheel_us >= (uint64_t)HALL_DEBOUNCE_MS * 1000ULL) {
        if (s_last_wheel_us != 0) {
            s_wheel_interval_us = now - s_last_wheel_us;
        }
        s_wheel_revs++;
        s_last_wheel_us         = now;
        s_wheel_event_time_1024 = us_to_1024(now);
    }
    portEXIT_CRITICAL_ISR(&s_mux);
}

static void IRAM_ATTR crank_isr(void *arg)
{
    uint64_t now = esp_timer_get_time();

    portENTER_CRITICAL_ISR(&s_mux);
    if (now - s_last_crank_us >= (uint64_t)HALL_DEBOUNCE_MS * 1000ULL) {
        s_crank_revs++;
        s_last_crank_us         = now;
        s_crank_event_time_1024 = us_to_1024(now);
    }
    portEXIT_CRITICAL_ISR(&s_mux);
}

/* ---- Public API ------------------------------------------------------- */

void hall_sensor_init(void)
{
    /* Configure both GPIO pins as inputs with internal pull-ups.
     * Sensors pull the line low when a magnet passes. */
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << HALL_WHEEL_GPIO) | (1ULL << HALL_CRANK_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    /* gpio_install_isr_service may already have been called by another
     * component; ESP_ERR_INVALID_STATE in that case is benign. */
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }

    ESP_ERROR_CHECK(gpio_isr_handler_add(HALL_WHEEL_GPIO, wheel_isr, NULL));
    ESP_ERROR_CHECK(gpio_isr_handler_add(HALL_CRANK_GPIO, crank_isr, NULL));

    ESP_LOGI(TAG, "Hall sensors ready — wheel GPIO %d, crank GPIO %d",
             HALL_WHEEL_GPIO, HALL_CRANK_GPIO);
}

void hall_sensor_get_measurement(csc_measurement_t *meas)
{
    portENTER_CRITICAL(&s_mux);
    meas->cumulative_wheel_revs = s_wheel_revs;
    meas->last_wheel_event_time = s_wheel_event_time_1024;
    meas->cumulative_crank_revs = s_crank_revs;
    meas->last_crank_event_time = s_crank_event_time_1024;
    portEXIT_CRITICAL(&s_mux);
}

float hall_sensor_get_speed_kmh(void)
{
    portENTER_CRITICAL(&s_mux);
    uint64_t interval = s_wheel_interval_us;
    uint64_t last     = s_last_wheel_us;
    portEXIT_CRITICAL(&s_mux);

    if (last == 0 || interval == 0) {
        return 0.0f;
    }

    /* If no pulse for more than 3 s, the wheel has stopped. */
    if (esp_timer_get_time() - last > 3000000ULL) {
        return 0.0f;
    }

    /* speed (km/h) = circumference (mm) / interval (us) * 3600 */
    return (float)((uint64_t)WHEEL_CIRCUMFERENCE_MM * 3600ULL) / (float)interval;
}

void hall_sensor_reset_wheel_revs(void)
{
    portENTER_CRITICAL(&s_mux);
    s_wheel_revs = 0;
    portEXIT_CRITICAL(&s_mux);
}
