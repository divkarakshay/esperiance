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

#define CELL        4
#define GRID_W      31
#define GRID_H      15
#define FIELD_X     2
#define FIELD_Y     2
#define MAX_SNAKE   (GRID_W * GRID_H)

typedef struct {
    int8_t x, y;
} pos_t;

static pos_t snake[MAX_SNAKE];
static int snake_len;
static int dir_x, dir_y;         /* current direction */
static int next_x, next_y;       /* buffered input direction */
static pos_t food;
static uint32_t score;
static uint32_t tick_ms;
static bool dead;

static void snake_reset(void)
{
    snake_len = 4;
    for (int i = 0; i < snake_len; i++) {
        snake[i] = (pos_t){ (int8_t)(12 - i), 7 };
    }
    dir_x = 1; dir_y = 0;
    next_x = 1; next_y = 0;
    score = 0;
    tick_ms = 160;
    dead = false;
}

static bool cell_occupied(int x, int y)
{
    for (int i = 0; i < snake_len; i++) {
        if (snake[i].x == x && snake[i].y == y) return true;
    }
    return false;
}

static void spawn_food(void)
{
    do {
        food.x = (int8_t)(esp_random() % GRID_W);
        food.y = (int8_t)(esp_random() % GRID_H);
    } while (cell_occupied(food.x, food.y));
}

static void snake_step(void)
{
    /* apply buffered direction (no 180 degree reversal) */
    if (!(next_x == -dir_x && next_y == -dir_y)) {
        dir_x = next_x;
        dir_y = next_y;
    }

    pos_t head = { (int8_t)(snake[0].x + dir_x),
                   (int8_t)(snake[0].y + dir_y) };

    /* wall hit */
    if (head.x < 0 || head.x >= GRID_W || head.y < 0 || head.y >= GRID_H) {
        dead = true;
        buzzer_tone(300, 200);
        return;
    }

    bool eating = (head.x == food.x && head.y == food.y);

    /* self collision (tail cell is safe when not growing) */
    int limit = eating ? snake_len : snake_len - 1;
    for (int i = 0; i < limit; i++) {
        if (snake[i].x == head.x && snake[i].y == head.y) {
            dead = true;
            buzzer_tone(300, 200);
            return;
        }
    }

    memmove(&snake[1], &snake[0], snake_len * sizeof(pos_t));
    snake[0] = head;

    if (eating) {
        snake_len++;
        score++;
        if (tick_ms > 70) tick_ms -= 4;
        buzzer_tone(880, 40);
        spawn_food();
    }
}

static void render(void)
{
    display_clear();

    /* border */
    display_draw_rect(1, 1, DISPLAY_WIDTH - 2, DISPLAY_HEIGHT - 2, false, true);

    /* score */
    char buf[16];
    snprintf(buf, sizeof(buf), "S:%lu", (unsigned long)score);
    display_draw_text(4, 5, buf, true);

    /* food: full cell */
    display_draw_rect(FIELD_X + food.x * CELL, FIELD_Y + food.y * CELL,
                      CELL, CELL, true, true);

    /* snake: 2x2 inner pixels for a chunkier look */
    for (int i = 0; i < snake_len; i++) {
        display_draw_rect(FIELD_X + snake[i].x * CELL + 1,
                          FIELD_Y + snake[i].y * CELL + 1,
                          CELL - 2, CELL - 2, true, true);
    }
}

static void render_gameover(void)
{
    display_clear();
    display_draw_rect(0, 0, DISPLAY_WIDTH, 12, true, true);
    display_draw_text_centered(3, "GAME OVER", false);

    char buf[24];
    snprintf(buf, sizeof(buf), "Score: %lu", (unsigned long)score);
    display_draw_text_centered(22, buf, true);

    snprintf(buf, sizeof(buf), "High: %lu", (unsigned long)score_get("snake"));
    display_draw_text_centered(32, buf, true);

    display_draw_text_centered(46, "OK:Retry  BACK:Menu", true);
}

void game_snake_run(void)
{
    snake_reset();
    spawn_food();
    bool show_gameover = false;

    uint32_t last_tick = esp_timer_get_time();

    while (1) {
        /* input */
        input_event_t ev;
        while (input_get_event(&ev, 0)) {
            if (ev.type != INPUT_EVENT_PRESS) continue;
            switch (ev.key) {
            case INPUT_KEY_UP:    next_x = 0;  next_y = -1; break;
            case INPUT_KEY_DOWN:  next_x = 0;  next_y = 1;  break;
            case INPUT_KEY_LEFT:  next_x = -1; next_y = 0;  break;
            case INPUT_KEY_RIGHT: next_x = 1;  next_y = 0;  break;
            case INPUT_KEY_BACK:
                return;                          /* exit to menu */
            case INPUT_KEY_OK:
                if (show_gameover) {
                    snake_reset();
                    spawn_food();
                    show_gameover = false;
                    last_tick = esp_timer_get_time();
                }
                break;
            default:
                break;
            }
        }

        /* game tick */
        if (!dead && !show_gameover) {
            uint32_t now = esp_timer_get_time();
            if (now - last_tick >= (uint32_t)tick_ms * 1000) {
                last_tick += (uint32_t)tick_ms * 1000;
                snake_step();
                if (dead) {
                    show_gameover = true;
                    score_submit("snake", score);
                }
            }
        }

        if (show_gameover) {
            render_gameover();
        } else {
            render();
        }
        display_show();

        vTaskDelay(pdMS_TO_TICKS(16));   /* ~60 fps */
    }
}