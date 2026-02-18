#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_timer.h"

#include "pulse_engine.h"

static const char *TAG = "BOOT";

void app_main(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    ESP_LOGI(TAG, "==============================");
    ESP_LOGI(TAG, "Industrial Pulse Controller");
    ESP_LOGI(TAG, "ESP32-C3 - ESP-IDF v5.5.1");
    ESP_LOGI(TAG, "Cores: %d", chip_info.cores);
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "Time since boot: %llu us", (unsigned long long)esp_timer_get_time());
    ESP_LOGI(TAG, "==============================");

    // Init pulse engine on GPIO0
    pulse_engine_init(0);

    // Test: 10 pulses, 100 ms HIGH, 100 ms LOW (v1)
    pulse_rc_t rc = pulse_engine_request((pulse_req_t){ .count = 10, .pulse_ms = 100 });
    ESP_LOGI(TAG, "request rc=%d busy=%d", rc, pulse_engine_is_busy());


    ESP_LOGI(TAG, "Pulse test requested (GPIO0)");
}
