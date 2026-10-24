#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buzzer.h"
#include "display.h"
#include "games.h"
#include "input.h"
#include "menu.h"

#define SOUND_KEY "sound"
#define TAG "menu"

typedef enum {
    STATE_TITLE = 0,
    STATE_MENU,
    STATE_SETTINGS,
    STATE_ABOUT,
    STATE_INPUT_TEST,
} menu_state_t;

static menu_state_t state = STATE_TITLE;
static int cursor = 0;        /* index into items[] */
static int menu_top = 0;      /* scroll offset */
static bool sound_on = true;

static const char *settings_items[] = { "Sound", "Input Test", "About" };
#define SETTINGS_COUNT 3

static void beep(uint16_t freq, uint16_t dur)
{
    if (sound_on) {
        buzzer_tone(freq, dur);
    }
}

/* -------- renderers -------- */

static void render_title(void)
{
    display_clear();
    display_draw_rect(0, 0, DISPLAY_WIDTH, 12, true, true);
    display_draw_text_centered(3, "ESPERIENCE", false);
    display_draw_text_centered(22, "retro game console", true);
    display_draw_text_centered(32, "v0.2.0", true);

    uint32_t t = esp_timer_get_time() / 1000;
    if ((t / 500) % 2 == 0) {
        display_draw_text_centered(48, "Press OK", true);
    }
}

static void render_menu(void)
{
    display_clear();
    display_draw_rect(0, 0, DISPLAY_WIDTH, 10, true, true);
    display_draw_text(4, 2, "ESPERIENCE", false);

    /* items: games + settings */
    const int item_count = game_count + 1;
    const int visible = 5;
    const int row_h = 8;
    const int y0 = 12;

    for (int i = 0; i < item_count; i++) {
        int row = i - menu_top;
        if (row < 0 || row >= visible) continue;
        int y = y0 + row * row_h;

        char label[24];
        if (i < game_count) {
            snprintf(label, sizeof(label), "%s", games[i].name);
        } else {
            snprintf(label, sizeof(label), "Settings");
        }

        if (i == cursor) {
            display_draw_rect(1, y, 126, row_h - 1, true, true);
            display_draw_text(3, y + 1, label, false);
            display_draw_char(1, y + 1, '>', false);
        } else {
            display_draw_text(3, y + 1, label, true);
        }

        /* high score hint for games */
        if (i < game_count) {
            char hs[16];
            snprintf(hs, sizeof(hs), "HI %lu", (unsigned long)score_get(games[i].nvs_key));
            int x = DISPLAY_WIDTH - display_text_width(hs) - 3;
            if (i == cursor) {
                display_draw_text(x, y + 1, hs, false);
            } else {
                display_draw_text(x, y + 1, hs, true);
            }
        }
    }

    display_draw_rect(0, 55, DISPLAY_WIDTH, 9, false, true);
    display_draw_text(4, 57, "OK:Play  BACK:Exit", true);
}

static void render_settings(void)
{
    display_clear();
    display_draw_rect(0, 0, DISPLAY_WIDTH, 10, true, true);
    display_draw_text(4, 2, "SETTINGS", false);

    for (int i = 0; i < SETTINGS_COUNT; i++) {
        int y = 14 + i * 12;
        if (i == cursor) {
            display_draw_rect(1, y - 1, 126, 10, true, true);
            display_draw_text(4, y, settings_items[i], false);
        } else {
            display_draw_text(4, y, settings_items[i], true);
        }
        if (i == 0) {
            display_draw_text(90, y, sound_on ? "ON" : "OFF",
                              i == cursor ? false : true);
        }
    }
    display_draw_rect(0, 55, DISPLAY_WIDTH, 9, false, true);
    display_draw_text(4, 57, "OK:Select  BACK:Menu", true);
}

static void render_about(void)
{
    display_clear();
    display_draw_rect(0, 0, DISPLAY_WIDTH, 10, true, true);
    display_draw_text_centered(2, "ABOUT", false);

    display_draw_text(8, 16, "Esperiance v0.2.0", true);
    display_draw_text(8, 26, "ESP32 retro console", true);
    display_draw_text(8, 36, "by Akshay Divkar", true);
    display_draw_text(8, 46, "SSD1306 + 6 buttons", true);
    display_draw_text(8, 56, " + analog joystick", true);
}

static void render_input_test(void)
{
    static const char *names[INPUT_KEY_COUNT] = {
        "UP", "DOWN", "LEFT", "RIGHT", "OK", "BACK", "SHIFT",
    };
    display_clear();
    display_draw_rect(0, 0, DISPLAY_WIDTH, 10, true, true);
    display_draw_text(4, 2, "INPUT TEST", false);

    for (int i = 0; i < INPUT_KEY_COUNT; i++) {
        int x = 4 + (i % 4) * 31;
        int y = 13 + (i / 4) * 11;
        display_draw_rect(x, y, 28, 9, input_is_pressed((input_key_t)i),
                          true);
        display_draw_text(x + 1, y + 1, names[i],
                          !input_is_pressed((input_key_t)i));
    }

    int jx, jy;
    input_get_joystick(&jx, &jy);
    display_draw_rect(4, 37, 120, 8, false, true);
    display_draw_rect(64 - 1, 37, 2, 8, true, true);      /* center mark */
    display_draw_rect(64 + jx * 58 / 100 - 1, 37, 2, 8, true, true);
    display_draw_text(4, 48, "X", true);
    display_draw_text(20, 48, "Y", true);

    display_draw_rect(4, 55, 120, 8, false, true);
    display_draw_rect(64 - 1, 55, 2, 8, true, true);
    display_draw_rect(64 + jy * 58 / 100 - 1, 55, 2, 8, true, true);

    char buf[32];
    snprintf(buf, sizeof(buf), "%4d %4d", jx, jy);
    display_draw_text(40, 48, buf, true);
}

/* -------- input handling -------- */

static void handle_title(input_event_t *ev)
{
    if (ev->type == INPUT_EVENT_PRESS && ev->key != INPUT_KEY_NONE) {
        beep(1500, 60);
        state = STATE_MENU;
    }
}

static void handle_menu(input_event_t *ev)
{
    const int item_count = game_count + 1;

    if (ev->type != INPUT_EVENT_PRESS) return;

    switch (ev->key) {
    case INPUT_KEY_UP:
        cursor = (cursor + item_count - 1) % item_count;
        beep(1200, 20);
        break;
    case INPUT_KEY_DOWN:
        cursor = (cursor + 1) % item_count;
        beep(1200, 20);
        break;
    case INPUT_KEY_OK:
        if (cursor < game_count) {
            beep(1600, 60);
            games[cursor].run();        /* returns on BACK */
        } else {
            beep(1600, 60);
            cursor = 0;
            state = STATE_SETTINGS;
        }
        break;
    case INPUT_KEY_BACK:
        state = STATE_TITLE;
        beep(900, 30);
        break;
    case INPUT_KEY_SHIFT:              /* quick sound toggle */
        sound_on = !sound_on;
        settings_set(SOUND_KEY, sound_on ? 1 : 0);
        beep(sound_on ? 1600 : 800, 40);
        break;
    default:
        break;
    }

    /* keep cursor in view */
    if (cursor < menu_top) menu_top = cursor;
    if (cursor >= menu_top + 5) menu_top = cursor - 4;
}

static void handle_settings(input_event_t *ev)
{
    if (ev->type != INPUT_EVENT_PRESS) return;

    switch (ev->key) {
    case INPUT_KEY_UP:
        cursor = (cursor + SETTINGS_COUNT - 1) % SETTINGS_COUNT;
        beep(1200, 20);
        break;
    case INPUT_KEY_DOWN:
        cursor = (cursor + 1) % SETTINGS_COUNT;
        beep(1200, 20);
        break;
    case INPUT_KEY_OK:
        if (cursor == 0) {
            sound_on = !sound_on;
            settings_set(SOUND_KEY, sound_on ? 1 : 0);
            beep(sound_on ? 1600 : 800, 40);
        } else if (cursor == 1) {
            state = STATE_INPUT_TEST;
            beep(1400, 40);
        } else {
            state = STATE_ABOUT;
            beep(1400, 40);
        }
        break;
    case INPUT_KEY_BACK:
        state = STATE_MENU;
        cursor = 0;
        beep(900, 30);
        break;
    default:
        break;
    }
}

static void handle_about(input_event_t *ev)
{
    if (ev->type == INPUT_EVENT_PRESS &&
        (ev->key == INPUT_KEY_BACK || ev->key == INPUT_KEY_OK)) {
        state = STATE_SETTINGS;
        cursor = 1;
        beep(900, 30);
    }
}

static void handle_input_test(input_event_t *ev)
{
    static uint32_t last_log = 0;

    if (ev->type == INPUT_EVENT_PRESS) {
        ESP_LOGI(TAG, "input test: event %s pressed",
                 ev->key == INPUT_KEY_UP ? "UP" :
                 ev->key == INPUT_KEY_DOWN ? "DOWN" :
                 ev->key == INPUT_KEY_LEFT ? "LEFT" :
                 ev->key == INPUT_KEY_RIGHT ? "RIGHT" :
                 ev->key == INPUT_KEY_OK ? "OK" :
                 ev->key == INPUT_KEY_BACK ? "BACK" : "SHIFT");
        if (ev->key == INPUT_KEY_BACK) {
            state = STATE_SETTINGS;
            cursor = 1;
            beep(900, 30);
        }
    }

    /* periodic raw dump for wiring diagnostics */
    uint32_t now = esp_timer_get_time() / 1000;
    if (now - last_log > 800) {
        last_log = now;
        static const char *names[INPUT_KEY_COUNT] = {
            "UP", "DOWN", "LEFT", "RIGHT", "OK", "BACK", "SHIFT",
        };
        char line[128];
        int pos = 0;
        pos += snprintf(line + pos, sizeof(line) - pos, "input: ");
        for (int i = 0; i < INPUT_KEY_COUNT; i++) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%s=%d ",
                            names[i], input_is_pressed((input_key_t)i) ? 1 : 0);
        }
        int jx, jy;
        input_get_joystick(&jx, &jy);
        snprintf(line + pos, sizeof(line) - pos, "JX=%d JY=%d", jx, jy);
        ESP_LOGI(TAG, "%s", line);
    }
}

void menu_run(void)
{
    sound_on = settings_get(SOUND_KEY, 1) != 0;

    while (1) {
        input_event_t ev;
        if (input_get_event(&ev, 20)) {
            switch (state) {
            case STATE_TITLE:    handle_title(&ev);     break;
            case STATE_MENU:     handle_menu(&ev);      break;
            case STATE_SETTINGS: handle_settings(&ev);  break;
            case STATE_ABOUT:    handle_about(&ev);     break;
            case STATE_INPUT_TEST: handle_input_test(&ev); break;
            }
        }

        switch (state) {
        case STATE_TITLE:    render_title();    break;
        case STATE_MENU:     render_menu();     break;
        case STATE_SETTINGS: render_settings(); break;
        case STATE_ABOUT:    render_about();    break;
        case STATE_INPUT_TEST: render_input_test(); break;
        }
        display_show();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}