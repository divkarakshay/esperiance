#include <stdio.h>
#include <string.h>

#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buzzer.h"
#include "display.h"
#include "games.h"
#include "input.h"

#define GRID     4
#define NCARDS   (GRID * GRID)
#define REVEAL_MS 800

typedef struct {
    uint8_t value;          /* 0..7 pattern id */
    bool face_up;
    bool matched;
} card_t;

static card_t cards[NCARDS];
static int cursor;          /* 0..15 */
static int first_pick;      /* -1 = no card flipped */
static bool locked;
static uint32_t lock_until;
static int matches;
static int misses;
static bool won;

static void shuffle_cards(void)
{
    for (int i = 0; i < NCARDS; i++) {
        cards[i].value = (uint8_t)(i / 2);
        cards[i].face_up = false;
        cards[i].matched = false;
    }
    /* Fisher-Yates */
    for (int i = NCARDS - 1; i > 0; i--) {
        int j = (int)(esp_random() % (uint32_t)(i + 1));
        card_t tmp = cards[i];
        cards[i] = cards[j];
        cards[j] = tmp;
    }
    cursor = 0;
    first_pick = -1;
    locked = false;
    matches = 0;
    misses = 0;
    won = false;
}

static int score_now(void)
{
    int s = matches * 10 - misses * 2;
    return s < 0 ? 0 : s;
}

static void draw_pattern(int x, int y, int w, int h, uint8_t value)
{
    switch (value) {
    case 0:                     /* solid block */
        display_draw_rect(x, y, w, h, true, true);
        break;
    case 1:                     /* ring */
        display_draw_rect(x, y, w, h, false, true);
        break;
    case 2:                     /* top half */
        display_draw_rect(x, y, w, h / 2, true, true);
        break;
    case 3:                     /* bottom half */
        display_draw_rect(x, y + h / 2, w, h - h / 2, true, true);
        break;
    case 4:                     /* left half */
        display_draw_rect(x, y, w / 2, h, true, true);
        break;
    case 5:                     /* right half */
        display_draw_rect(x + w / 2, y, w - w / 2, h, true, true);
        break;
    case 6:                     /* main diagonal */
        for (int i = 0; i < w && i < h; i++) {
            display_draw_pixel(x + i, y + i, true);
        }
        break;
    case 7:                     /* anti diagonal */
        for (int i = 0; i < w && i < h; i++) {
            display_draw_pixel(x + w - 1 - i, y + i, true);
        }
        break;
    }
}

static void render(void)
{
    display_clear();

    const int cw = DISPLAY_WIDTH / GRID;
    const int ch = 14;
    const int y0 = 10;

    for (int i = 0; i < NCARDS; i++) {
        int cx = i % GRID;
        int cy = i / GRID;
        int x = cx * cw + (cw - 30) / 2;
        int y = y0 + cy * (ch + 2);

        if (cards[i].matched || cards[i].face_up) {
            display_draw_rect(x, y, 30, ch, true, true);
            draw_pattern(x + 2, y + 2, 26, ch - 4, cards[i].value);
        } else {
            /* card back: filled with a small dot pattern */
            display_draw_rect(x, y, 30, ch, true, true);
            for (int dx = 3; dx < 30; dx += 4) {
                for (int dy = 2; dy < ch; dy += 4) {
                    display_draw_pixel(x + dx, y + dy, false);
                }
            }
        }

        /* cursor: inverted border */
        if (i == cursor && !won) {
            display_draw_rect(x - 1, y - 1, 32, ch + 2, false, true);
        }
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "P %d/%d", matches, NCARDS / 2);
    display_draw_text(2, 1, buf, true);
    snprintf(buf, sizeof(buf), "%d", score_now());
    display_draw_text(118, 1, buf, true);
}

static void render_win(void)
{
    display_clear();
    display_draw_rect(0, 0, DISPLAY_WIDTH, 12, true, true);
    display_draw_text_centered(3, "PAIRS CLEARED!", false);

    char buf[32];
    snprintf(buf, sizeof(buf), "Flips: %d", matches * 2 + misses);
    display_draw_text_centered(22, buf, true);
    snprintf(buf, sizeof(buf), "Score: %d", score_now());
    display_draw_text_centered(32, buf, true);
    snprintf(buf, sizeof(buf), "High: %lu", (unsigned long)score_get("pairs"));
    display_draw_text_centered(42, buf, true);
    display_draw_text_centered(54, "OK:Retry  BACK:Menu", true);
}

void game_pairs_run(void)
{
    shuffle_cards();

    while (1) {
        input_event_t ev;
        while (input_get_event(&ev, 0)) {
            if (ev.type != INPUT_EVENT_PRESS || locked) continue;
            switch (ev.key) {
            case INPUT_KEY_UP:
            case INPUT_KEY_DOWN:
            case INPUT_KEY_LEFT:
            case INPUT_KEY_RIGHT: {
                int dx = 0, dy = 0;
                if (ev.key == INPUT_KEY_LEFT)  dx = -1;
                if (ev.key == INPUT_KEY_RIGHT) dx = 1;
                if (ev.key == INPUT_KEY_UP)    dy = -1;
                if (ev.key == INPUT_KEY_DOWN)  dy = 1;
                int cx = cursor % GRID;
                int cy = cursor / GRID;
                cx += dx;
                cy += dy;
                if (cx < 0) cx = GRID - 1;
                if (cx >= GRID) cx = 0;
                if (cy < 0) cy = GRID - 1;
                if (cy >= GRID) cy = 0;
                cursor = cy * GRID + cx;
                buzzer_tone(1100, 15);
                break;
            }
            case INPUT_KEY_OK: {
                if (cards[cursor].matched || cards[cursor].face_up) break;
                cards[cursor].face_up = true;
                buzzer_tone(900, 20);
                if (first_pick < 0) {
                    first_pick = cursor;
                } else {
                    if (cards[first_pick].value == cards[cursor].value) {
                        cards[first_pick].matched = true;
                        cards[cursor].matched = true;
                        matches++;
                        buzzer_tone(1568, 60);
                        buzzer_tone(1175, 60);
                    } else {
                        misses++;
                        buzzer_tone(400, 60);
                        locked = true;
                        lock_until = esp_timer_get_time() + REVEAL_MS * 1000;
                    }
                    first_pick = -1;
                }
                break;
            }
            case INPUT_KEY_SHIFT:
                shuffle_cards();
                buzzer_tone(700, 30);
                break;
            case INPUT_KEY_BACK:
                return;
            default:
                break;
            }
        }

        if (locked && esp_timer_get_time() >= lock_until) {
            for (int i = 0; i < NCARDS; i++) {
                if (!cards[i].matched) cards[i].face_up = false;
            }
            locked = false;
        }

        if (matches == NCARDS / 2 && !won) {
            won = true;
            score_submit("pairs", (uint32_t)score_now());
            buzzer_tone(880, 70);
            buzzer_tone(1175, 70);
            buzzer_tone(1568, 90);
        }

        if (won) {
            render_win();
        } else {
            render();
        }
        display_show();

        vTaskDelay(pdMS_TO_TICKS(16));
    }
}