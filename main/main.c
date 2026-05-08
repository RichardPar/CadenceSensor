// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Richard Parsons

#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "hall_sensor.h"
#include "ble_csc.h"

static const char *TAG = "main";

void app_main(void)
{
    /*
     * NVS is required by the NimBLE host for storing bonding / peer data.
     * Erase and reinitialise if the partition is full or its version changed.
     */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    hall_sensor_init();
    ble_csc_init();

    ESP_LOGI(TAG, "Cadence sensor started — advertising as \"CadenceSensor\"");

    csc_measurement_t meas;

    /*
     * Send a CSC Measurement notification every second.
     * The BT CSC Profile recommends a maximum interval of 2 s; 1 s gives
     * smooth speed and cadence readings in typical cycling apps.
     */
    while (1) {
        hall_sensor_get_measurement(&meas);
        ble_csc_send_measurement(&meas);

        float speed_kmh = hall_sensor_get_speed_kmh();
        ESP_LOGI(TAG, "Speed: %.1f km/h  wheel: %" PRIu32 "  crank: %u  rise: %" PRIu32 "  rejected: %" PRIu32 "  raw: %" PRIu32,
                 speed_kmh, meas.cumulative_wheel_revs, meas.cumulative_crank_revs,
                 meas.crank_rise_count, meas.crank_rejected, meas.crank_raw_triggers);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
