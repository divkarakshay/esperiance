#include <stdio.h>

#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buzzer.h"
#include "display.h"
#include "games.h"
#include "input.h"

#define BUCKET_W    20
#define BUCKET_H    5
#define BUCKET_Y    (DISPLAY_HEIGHT - BUCKET_H - 2)
#define MAX_DROPS   8
#define DROP_W      3
#define DROP_H      5
#define BUCKET_SPEED 150.0f
#define BASE_SPEED  55.0f

typedef struct {
    float x, y;
    float vy;
    bool active;
} drop_t;

static float bucket_x;
static int lives;
static uint32_t score;
static int level;
static drop_t drops[MAX_DROPS];
static uint32_t next_spawn_ms;
static bool over;

static void raindrops_reset(void)
{
    bucket_x = (DISPLAY_WIDTH - BUCKET_W) / 2.0f;
    lives = 3;
    score = 0;
    level = 1;
    over = false;
    for (int i = 0; i < MAX_DROPS; i++) drops[i].active = false;
    next_spawn_ms = 200;
}

static void spawn_drop(void)
{
    for (int i = 0; i < MAX_DROPS; i++) {
        if (!drops[i].active) {
            drops[i].active = true;
            drops[i].x = 4.0f + (float)(esp_random() % (DISPLAY_WIDTH - DROP_W - 8));
            drops[i].y = 10.0f;
            drops[i].vy = BASE_SPEED + level * 9.0f +
                          (float)(esp_random() % 60);
            if (drops[i].vy > 240.0f) drops[i].vy = 240.0f;
            return;
        }
    }
}

static void update(float dt, uint32_t now_ms)
{
    if (now_ms >= next_spawn_ms) {
        spawn_drop();
        next_spawn_ms = now_ms + (uint32_t)(500 - level * 40);
        if (next_spawn_ms < 220) next_spawn_ms = 220;
    }

    for (int i = 0; i < MAX_DROPS; i++) {
        if (!drops[i].active) continue;
        drops[i].y += drops[i].vy * dt;

        /* caught by the bucket */
        if (drops[i].y + DROP_H >= BUCKET_Y &&
            drops[i].y < BUCKET_Y + BUCKET_H &&
            drops[i].x + DROP_W > bucket_x &&
            drops[i].x < bucket_x + BUCKET_W) {
            drops[i].active = false;
            score++;
            if (score % 8 == 0 && level < 8) level++;
            buzzer_tone(880, 30);
            continue;
        }

        /* missed */
        if (drops[i].y > DISPLAY_HEIGHT) {
            drops[i].active = false;
            lives--;
            buzzer_tone(220, 150);
            if (lives <= 0) {
                over = true;
                score_submit("raindrops", score);
            }
        }
    }
}

static void render(void)
{
    display_clear();

    /* HUD */
    char buf[24];
    snprintf(buf, sizeof(buf), "SC %lu", (unsigned long)score);
    display_draw_text(2, 0, buf, true);
    snprintf(buf, sizeof(buf), "LV %d", level);
    display_draw_text(70, 0, buf, true);
    for (int i = 0; i < lives; i++) {
        display_draw_rect(104 + i * 7, 1, 5, 5, true, true);
    }

    /* drops */
    for (int i = 0; i < MAX_DROPS; i++) {
        if (drops[i].active) {
            int dx = (int)drops[i].x;
            int dy = (int)drops[i].y;
            display_draw_rect(dx, dy, DROP_W, DROP_H, true, true);
            display_draw_pixel(dx + 1, dy - 1, true);
        }
    }

    /* bucket */
    display_draw_rect((int)bucket_x, BUCKET_Y, BUCKET_W, BUCKET_H, true, true);
    display_draw_rect((int)bucket_x + 1, BUCKET_Y + BUCKET_H, 2, 2, true, true);
    display_draw_rect((int)bucket_x + BUCKET_W - 3, BUCKET_Y + BUCKET_H,
                      2, 2, true, true);
}

static void render_gameover(void)
{
    display_clear();
    display_draw_rect(0, 0, DISPLAY_WIDTH, 12, true, true);
    display_draw_text_centered(3, "GAME OVER", false);

    char buf[24];
    snprintf(buf, sizeof(buf), "Catch: %lu", (unsigned long)score);
    display_draw_text_centered(22, buf, true);
    snprintf(buf, sizeof(buf), "High: %lu", (unsigned long)score_get("raindrops"));
    display_draw_text_centered(32, buf, true);

    display_draw_text_centered(46, "OK:Retry  BACK:Menu", true);
}

void game_raindrops_run(void)
{
    raindrops_reset();
    uint32_t last = esp_timer_get_time();
    bool show_gameover = false;

    while (1) {
        input_event_t ev;
        while (input_get_event(&ev, 0)) {
            if (ev.type != INPUT_EVENT_PRESS) continue;
            if (ev.key == INPUT_KEY_BACK) return;
            if (ev.key == INPUT_KEY_OK && show_gameover) {
                raindrops_reset();
                show_gameover = false;
                last = esp_timer_get_time();
            }
        }

        uint32_t now = esp_timer_get_time();
        float dt = (float)(now - last) / 1000000.0f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;
        uint32_t now_ms = now / 1000;

        if (!over) {
            float move = 0.0f;
            if (input_is_pressed(INPUT_KEY_LEFT))  move -= BUCKET_SPEED;
            if (input_is_pressed(INPUT_KEY_RIGHT)) move += BUCKET_SPEED;
            int jx, jy;
            input_get_joystick(&jx, &jy);
            if (jx > 20)       move += BUCKET_SPEED * (jx - 20) / 80.0f;
            else if (jx < -20) move -= BUCKET_SPEED * (-jx - 20) / 80.0f;

            bucket_x += move * dt;
            if (bucket_x < 0) bucket_x = 0;
            if (bucket_x > DISPLAY_WIDTH - BUCKET_W) {
                bucket_x = DISPLAY_WIDTH - BUCKET_W;
            }

            update(dt, now_ms);
            if (over) show_gameover = true;
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
