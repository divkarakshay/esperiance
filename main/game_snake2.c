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

#define CELL        8
#define GRID_W      16
#define GRID_H      7
#define FIELD_Y     8
#define MAX_SNAKE   (GRID_W * GRID_H)

#define LEVEL_COUNT 3
#define FOOD_LEVEL  8           /* foods per level */

typedef struct {
    int8_t x, y;
} pos_t;

static const char wall_maps[LEVEL_COUNT][GRID_H][GRID_W + 1] = {
    {
        "..##....##......",
        "................",
        ".....##..##.....",
        "................",
        "..##....##......",
        "................",
        "................",
    },
    {
        ".##..##..##..##.",
        ".##..##..##..##.",
        "................",
        ".###.####.###...",
        "................",
        ".##..##..##..##.",
        ".##..##..##..##.",
    },
    {
        ".####....####...",
        "....#....#......",
        "####.##..##.####",
        "....#....#......",
        "###...##...#####",
        "..##......##....",
        "....#######.....",
    },
};

static pos_t snake[MAX_SNAKE];
static int snake_len;
static int dir_x, dir_y;
static int next_x, next_y;
static pos_t food;
static uint32_t score;
static uint32_t tick_ms;
static bool dead;
static int lives;
static int level;
static int food_eaten;

static bool cell_is_wall(int x, int y)
{
    if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H) return true;
    return wall_maps[level][y][x] == '#';
}

static bool cell_occupied(int x, int y)
{
    for (int i = 0; i < snake_len; i++) {
        if (snake[i].x == x && snake[i].y == y) return true;
    }
    return false;
}

static void snake_reset(void)
{
    snake_len = 4;
    for (int i = 0; i < snake_len; i++) {
        snake[i] = (pos_t){ (int8_t)(4 - i), 3 };
    }
    dir_x = 1; dir_y = 0;
    next_x = 1; next_y = 0;
    dead = false;
    tick_ms = 150 - (uint32_t)(level - 1) * 20;
    if (tick_ms < 80) tick_ms = 80;
}

static void spawn_food(void)
{
    do {
        food.x = (int8_t)(esp_random() % GRID_W);
        food.y = (int8_t)(esp_random() % GRID_H);
    } while (cell_is_wall(food.x, food.y) || cell_occupied(food.x, food.y));
}

static void snake2_reset(void)
{
    level = 0;
    lives = 3;
    score = 0;
    food_eaten = 0;
    snake_reset();
    spawn_food();
}

static void snake_step(void)
{
    if (!(next_x == -dir_x && next_y == -dir_y)) {
        dir_x = next_x;
        dir_y = next_y;
    }

    pos_t head = { (int8_t)(snake[0].x + dir_x),
                   (int8_t)(snake[0].y + dir_y) };

    if (cell_is_wall(head.x, head.y)) {
        dead = true;
        buzzer_tone(200, 200);
        return;
    }

    bool eating = (head.x == food.x && head.y == food.y);

    int limit = eating ? snake_len : snake_len - 1;
    for (int i = 0; i < limit; i++) {
        if (snake[i].x == head.x && snake[i].y == head.y) {
            dead = true;
            buzzer_tone(200, 200);
            return;
        }
    }

    memmove(&snake[1], &snake[0], snake_len * sizeof(pos_t));
    snake[0] = head;

    if (eating) {
        snake_len++;
        score += 10;
        food_eaten++;
        if (tick_ms > 70) tick_ms -= 2;
        buzzer_tone(880, 40);
        spawn_food();
    }
}

static void render(void)
{
    display_clear();

    /* walls */
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            if (cell_is_wall(x, y)) {
                display_draw_rect(x * CELL, FIELD_Y + y * CELL, CELL, CELL,
                                  true, true);
            }
        }
    }

    /* food */
    display_draw_rect(food.x * CELL + 2, FIELD_Y + food.y * CELL + 2,
                      CELL - 4, CELL - 4, true, true);

    /* snake */
    for (int i = 0; i < snake_len; i++) {
        display_draw_rect(snake[i].x * CELL + 1,
                          FIELD_Y + snake[i].y * CELL + 1,
                          CELL - 2, CELL - 2, true, true);
    }

    /* HUD */
    char buf[24];
    snprintf(buf, sizeof(buf), "S:%lu", (unsigned long)score);
    display_draw_text(2, 0, buf, true);
    snprintf(buf, sizeof(buf), "LV %d/%d", level + 1, LEVEL_COUNT);
    display_draw_text(52, 0, buf, true);
    snprintf(buf, sizeof(buf), "L%d", lives);
    display_draw_text(112, 0, buf, true);
}

static void render_gameover(void)
{
    display_clear();
    display_draw_rect(0, 0, DISPLAY_WIDTH, 12, true, true);
    display_draw_text_centered(3, "GAME OVER", false);

    char buf[24];
    snprintf(buf, sizeof(buf), "Score: %lu", (unsigned long)score);
    display_draw_text_centered(22, buf, true);
    snprintf(buf, sizeof(buf), "High: %lu", (unsigned long)score_get("snake2"));
    display_draw_text_centered(32, buf, true);

    display_draw_text_centered(46, "OK:Retry  BACK:Menu", true);
}

void game_snake2_run(void)
{
    snake2_reset();
    bool show_gameover = false;
    uint32_t last_tick = esp_timer_get_time();

    while (1) {
        input_event_t ev;
        while (input_get_event(&ev, 0)) {
            if (ev.type != INPUT_EVENT_PRESS) continue;
            switch (ev.key) {
            case INPUT_KEY_UP:    next_x = 0;  next_y = -1; break;
            case INPUT_KEY_DOWN:  next_x = 0;  next_y = 1;  break;
            case INPUT_KEY_LEFT:  next_x = -1; next_y = 0;  break;
            case INPUT_KEY_RIGHT: next_x = 1;  next_y = 0;  break;
            case INPUT_KEY_BACK:
                return;
            case INPUT_KEY_OK:
                if (show_gameover) {
                    snake2_reset();
                    show_gameover = false;
                    last_tick = esp_timer_get_time();
                }
                break;
            default:
                break;
            }
        }

        if (!dead && !show_gameover) {
            uint32_t now = esp_timer_get_time();
            if (now - last_tick >= (uint32_t)tick_ms * 1000) {
                last_tick += (uint32_t)tick_ms * 1000;
                snake_step();
                if (dead) {
                    lives--;
                    buzzer_tone(300, 150);
                    if (lives <= 0) {
                        show_gameover = true;
                        score_submit("snake2", score);
                    } else {
                        /* continue on the same level */
                        snake_reset();
                        spawn_food();
                        dead = false;
                    }
                }
            }
        }

        /* level up */
        if (!dead && !show_gameover && food_eaten >= FOOD_LEVEL) {
            food_eaten = 0;
            level++;
            if (level >= LEVEL_COUNT) level = 0;
            score += 50;
            buzzer_tone(660, 60);
            buzzer_tone(880, 60);
            snake_reset();
            spawn_food();
            last_tick = esp_timer_get_time();
        }

        if (show_gameover) {
            render_gameover();
        } else {
            render();
        }
        display_show();

        vTaskDelay(pdMS_TO_TICKS(16));
    }
}
