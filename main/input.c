#include <string.h>

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "input.h"

#define TAG "input"

#define POLL_MS        5
#define DEBOUNCE_TICKS 6            /* 6 * 5ms = 30ms */

/* Joystick axes are normalized to -100..100. */
#define JOY_ACTIVE 30               /* direction press threshold */
#define JOY_RELEASE 18              /* hysteresis: release below this */

static const gpio_num_t btn_pins[INPUT_KEY_COUNT] = {
    [INPUT_KEY_UP]    = 13,
    [INPUT_KEY_DOWN]  = 14,
    [INPUT_KEY_LEFT]  = 27,
    [INPUT_KEY_RIGHT] = 12,
    [INPUT_KEY_OK]    = 32,         /* joystick click */
    [INPUT_KEY_BACK]  = 25,
    [INPUT_KEY_SHIFT] = 26,
};

#define ADC_X_CH ADC_CHANNEL_7      /* pin 35 = VRX */
#define ADC_Y_CH ADC_CHANNEL_6      /* pin 34 = VRY */

static adc_oneshot_unit_handle_t adc;
static int adc_center_x, adc_center_y;

static bool btn_state[INPUT_KEY_COUNT];   /* debounced, true = pressed */
static bool btn_raw[INPUT_KEY_COUNT];
static int  debounce_cnt[INPUT_KEY_COUNT];

static bool joy_dir_state[4];   /* synthesized: up/down/left/right */
static int  joy_x, joy_y;       /* raw normalized axes, -100..100 */

static QueueHandle_t evq;

static void push_event(input_key_t key, input_event_type_t type)
{
    input_event_t ev = { .key = key, .type = type };
    xQueueSend(evq, &ev, 0);
}

static int normalize_axis(int raw, int center)
{
    int v = (raw - center) * 100 / 2048;
    if (v > 100) v = 100;
    if (v < -100) v = -100;
    return v;
}

static void poll_joystick(void)
{
    int rx, ry;
    if (adc_oneshot_read(adc, ADC_X_CH, &rx) != ESP_OK) return;
    if (adc_oneshot_read(adc, ADC_Y_CH, &ry) != ESP_OK) return;

    joy_x = normalize_axis(rx, adc_center_x);
    joy_y = normalize_axis(ry, adc_center_y);

    /* Synthesize directional presses with hysteresis. */
    static const struct {
        input_key_t key;
        int *val;
    } dirs[] = {
        { INPUT_KEY_LEFT,  &joy_x },
        { INPUT_KEY_RIGHT, &joy_x },
        { INPUT_KEY_UP,    &joy_y },
        { INPUT_KEY_DOWN,  &joy_y },
    };
    static const bool neg[4] = { true, false, false, true };

    for (int i = 0; i < 4; i++) {
        int v = *dirs[i].val;
        bool active = neg[i] ? (v < -JOY_ACTIVE) : (v > JOY_ACTIVE);
        if (active && !joy_dir_state[i]) {
            joy_dir_state[i] = true;
            push_event(dirs[i].key, INPUT_EVENT_PRESS);
        } else if (!active && joy_dir_state[i]) {
            bool released = neg[i] ? (v > -JOY_RELEASE) : (v < JOY_RELEASE);
            if (released) {
                joy_dir_state[i] = false;
                push_event(dirs[i].key, INPUT_EVENT_RELEASE);
            }
        }
    }
}

static void input_task(void *arg)
{
    while (1) {
        /* Physical buttons: active low with pull-up, software debounce. */
        for (int k = 0; k < INPUT_KEY_COUNT; k++) {
            bool raw = (gpio_get_level(btn_pins[k]) == 0);
            if (raw != btn_raw[k]) {
                btn_raw[k] = raw;
                debounce_cnt[k] = 0;
            } else if (debounce_cnt[k] < DEBOUNCE_TICKS) {
                debounce_cnt[k]++;
                if (debounce_cnt[k] == DEBOUNCE_TICKS && raw != btn_state[k]) {
                    btn_state[k] = raw;
                    push_event((input_key_t)k, raw ? INPUT_EVENT_PRESS
                                                   : INPUT_EVENT_RELEASE);
                }
            }
        }

        poll_joystick();
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

void input_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 0,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    for (int i = 0; i < INPUT_KEY_COUNT; i++) {
        io.pin_bit_mask |= (1ULL << btn_pins[i]);
    }
    gpio_config(&io);

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, ADC_X_CH, &chan_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, ADC_Y_CH, &chan_cfg));

    /* Calibrate rest position: average a few samples with stick released. */
    long sx = 0, sy = 0;
    const int samples = 16;
    for (int i = 0; i < samples; i++) {
        int x, y;
        adc_oneshot_read(adc, ADC_X_CH, &x);
        adc_oneshot_read(adc, ADC_Y_CH, &y);
        sx += x;
        sy += y;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    adc_center_x = (int)(sx / samples);
    adc_center_y = (int)(sy / samples);
    ESP_LOGI(TAG, "joystick center x=%d y=%d", adc_center_x, adc_center_y);

    evq = xQueueCreate(24, sizeof(input_event_t));
    xTaskCreatePinnedToCore(input_task, "input", 4096, NULL, 5, NULL, 1);
}

bool input_get_event(input_event_t *ev, uint32_t timeout_ms)
{
    return xQueueReceive(evq, ev, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

bool input_is_pressed(input_key_t key)
{
    if (key < 0 || key >= INPUT_KEY_COUNT) {
        return false;
    }
    if (key == INPUT_KEY_UP)    return btn_state[key] || joy_dir_state[2];
    if (key == INPUT_KEY_DOWN)  return btn_state[key] || joy_dir_state[3];
    if (key == INPUT_KEY_LEFT)  return btn_state[key] || joy_dir_state[0];
    if (key == INPUT_KEY_RIGHT) return btn_state[key] || joy_dir_state[1];
    return btn_state[key];
}

void input_get_joystick(int *x, int *y)
{
    if (x) *x = joy_x;
    if (y) *y = joy_y;
}