#include "pulse_engine.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "PULSE";

static QueueHandle_t s_q = NULL;
static gptimer_handle_t s_tmr = NULL;

static int s_gpio = -1;

static volatile bool s_level_high = false;
static volatile uint32_t s_pulses_left = 0;
static volatile uint32_t s_half_period_us = 0; // HIGH duration == LOW duration (v1)

static bool IRAM_ATTR on_timer_alarm(gptimer_handle_t timer,
                                     const gptimer_alarm_event_data_t *edata,
                                     void *user_data)
{
    s_level_high = !s_level_high;
    gpio_set_level(s_gpio, s_level_high ? 1 : 0);

    if (!s_level_high) {
        // Un pulso completo terminó cuando volvemos a LOW
        if (s_pulses_left > 0) s_pulses_left--;
        if (s_pulses_left == 0) {
            gptimer_stop(timer);
            gpio_set_level(s_gpio, 0);
        }
    }
    return false;
}

static void pulse_task(void *arg)
{
    pulse_req_t req;

    while (1) {
        if (xQueueReceive(s_q, &req, portMAX_DELAY) == pdTRUE) {

            if (req.count == 0) continue;

            // Clamp básico
            if (req.pulse_ms < 30) req.pulse_ms = 30;
            if (req.pulse_ms > 300) req.pulse_ms = 300;

            s_half_period_us = req.pulse_ms * 1000UL;
            s_pulses_left = req.count;
            s_level_high = false;
            gpio_set_level(s_gpio, 0);

            ESP_LOGI(TAG, "Start: count=%lu pulse_ms=%lu",
                     (unsigned long)req.count, (unsigned long)req.pulse_ms);

            gptimer_alarm_config_t alarm_cfg = {
                .alarm_count = s_half_period_us,
                .reload_count = 0,
                .flags.auto_reload_on_alarm = true,
            };

            ESP_ERROR_CHECK(gptimer_set_alarm_action(s_tmr, &alarm_cfg));
            ESP_ERROR_CHECK(gptimer_set_raw_count(s_tmr, 0));
            ESP_ERROR_CHECK(gptimer_start(s_tmr));
        }
    }
}

bool pulse_engine_init(int gpio_out)
{
    s_gpio = gpio_out;

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << s_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(s_gpio, 0);

    s_q = xQueueCreate(8, sizeof(pulse_req_t));
    ESP_RETURN_ON_FALSE(s_q != NULL, false, TAG, "Queue create failed");

    gptimer_config_t tcfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1MHz -> 1 tick = 1us
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&tcfg, &s_tmr));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = on_timer_alarm,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(s_tmr, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(s_tmr));

    xTaskCreate(pulse_task, "pulse_task", 4096, NULL, 10, NULL);

    ESP_LOGI(TAG, "Pulse engine init OK (gpio=%d)", s_gpio);
    return true;
}

bool pulse_engine_request(pulse_req_t req)
{
    if (!s_q) return false;
    return xQueueSend(s_q, &req, 0) == pdTRUE;
}
