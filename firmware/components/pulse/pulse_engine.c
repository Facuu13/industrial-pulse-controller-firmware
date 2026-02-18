#include "pulse_engine.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"


static const char *TAG = "PULSE";

static QueueHandle_t s_q = NULL;
static gptimer_handle_t s_tmr = NULL;

static int s_gpio = -1;

static volatile bool s_level_high = false;
static volatile uint32_t s_pulses_left = 0;
static volatile uint32_t s_half_period_us = 0; // HIGH duration == LOW duration (v1)


static volatile bool s_busy = false;
static volatile bool s_done_isr = false;

static uint32_t s_req_count = 0;
static int64_t s_start_us = 0;

static pulse_done_cb_t s_done_cb = NULL;


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
            s_done_isr = true;     // <- señal para la task
            s_busy = false;        // <- ya no está ocupado
        }

    }
    return false;
}

static void pulse_task(void *arg)
{
    pulse_req_t req;

    while (1) {

        // 1) Si terminó (flag seteado por ISR), reportamos desde la task
        if (s_done_isr) {
            s_done_isr = false;

            int64_t end_us = esp_timer_get_time();
            pulse_done_t done = {
                .pulses_done = s_req_count,
                .elapsed_ms = (uint32_t)((end_us - s_start_us) / 1000),
            };

            ESP_LOGI(TAG, "DONE: pulses=%lu elapsed=%lums",
                     (unsigned long)done.pulses_done,
                     (unsigned long)done.elapsed_ms);

            if (s_done_cb) {
                s_done_cb(done);
            }
        }

        // 2) Esperamos un request (pero no bloqueamos “para siempre”, así podemos reportar DONE)
        if (xQueueReceive(s_q, &req, pdMS_TO_TICKS(10)) == pdTRUE) {

            if (req.count == 0) continue;

            // Clamp básico
            if (req.pulse_ms < 30) req.pulse_ms = 30;
            if (req.pulse_ms > 300) req.pulse_ms = 300;

            // Política v1: si está ocupado, ignoramos el nuevo request
            // (más adelante podemos encolar o devolver BUSY desde pulse_engine_request)
            if (s_busy) {
                ESP_LOGW(TAG, "BUSY: ignoring request count=%lu",
                         (unsigned long)req.count);
                continue;
            }

            s_half_period_us = req.pulse_ms * 1000UL;
            s_pulses_left = req.count;
            s_level_high = false;
            gpio_set_level(s_gpio, 0);

            // Para medir duración total
            s_req_count = req.count;
            s_start_us = esp_timer_get_time();
            s_busy = true;
            s_done_isr = false;

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

        // 3) Pequeño respiro (ya estamos haciendo 10ms en receive, pero lo dejamos implícito)
        vTaskDelay(pdMS_TO_TICKS(1));
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


bool pulse_engine_is_busy(void)
{
    return s_busy;
}

void pulse_engine_set_done_cb(pulse_done_cb_t cb)
{
    s_done_cb = cb;
}

pulse_rc_t pulse_engine_request(pulse_req_t req)
{
    if (!s_q) return PULSE_ERR_NOT_INIT;

    // Política v1: si está ocupado, rechazamos
    if (s_busy) return PULSE_ERR_BUSY;

    if (xQueueSend(s_q, &req, 0) != pdTRUE) return PULSE_ERR_QUEUE_FULL;
    return PULSE_OK;
}
