#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_timer.h"

static const char *TAG = "BOOT";

void app_main(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    ESP_LOGI(TAG, "==============================");
    ESP_LOGI(TAG, "Industrial Pulse Controller");
    ESP_LOGI(TAG, "ESP32-C3 - ESP-IDF v5.5.1");
    ESP_LOGI(TAG, "Cores: %d", chip_info.cores);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Time since boot: %llu us", esp_timer_get_time());
    ESP_LOGI(TAG, "==============================");
}
