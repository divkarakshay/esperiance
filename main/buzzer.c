#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "buzzer.h"

#define BUZZER_PIN      33
#define QUEUE_LEN       8
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define DUTY_50_PERCENT 128        /* 8-bit resolution */

typedef struct {
    uint16_t freq_hz;
    uint16_t dur_ms;
} tone_msg_t;

static QueueHandle_t tone_q;

static void buzzer_task(void *arg)
{
    bool playing = false;
    tone_msg_t msg;

    for (;;) {
        if (!playing) {
            if (xQueueReceive(tone_q, &msg, portMAX_DELAY) != pdTRUE) {
                continue;
            }
        }
        if (msg.freq_hz == 0) {
            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
            playing = false;
            continue;
        }
        ledc_set_freq(LEDC_MODE, LEDC_TIMER, msg.freq_hz);
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, DUTY_50_PERCENT);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
        playing = true;

        tone_msg_t next;
        if (xQueueReceive(tone_q, &next, pdMS_TO_TICKS(msg.dur_ms)) == pdTRUE) {
            /* New tone arrived: cut current short and play it next. */
            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
            msg = next;
            continue;
        }
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
        playing = false;
    }
}

void buzzer_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t chan_cfg = {
        .gpio_num = BUZZER_PIN,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&chan_cfg);

    tone_q = xQueueCreate(QUEUE_LEN, sizeof(tone_msg_t));
    xTaskCreate(buzzer_task, "buzzer", 2048, NULL, 3, NULL);
}

void buzzer_tone(uint16_t freq_hz, uint16_t dur_ms)
{
    tone_msg_t msg = { .freq_hz = freq_hz, .dur_ms = dur_ms };
    xQueueSend(tone_q, &msg, 0);
}