#include <stdio.h>
#include <math.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buzzer.h"
#include "display.h"
#include "games.h"
#include "input.h"

#define BRICK_COLS  8
#define BRICK_ROWS  5
#define BRICK_W     14
#define BRICK_H     5
#define BRICK_GAP   2
#define BRICK_X0    1
#define BRICK_Y0    12

#define PAD_W       22
#define PAD_H       3
#define PAD_Y       57
#define BALL_SIZE   3

#define PAD_SPEED   130.0f
#define BASE_SPEED  95.0f
#define MAX_SPEED   260.0f

static uint8_t bricks[BRICK_ROWS][BRICK_COLS];   /* 0 = gone, 1..3 = hp */
static float pad_x;                              /* paddle left x */
static float bx, by, bvx, bvy;
static float ball_speed;
static int lives;
static uint32_t score;
static int level;
static bool launched;
static uint32_t launch_until_us;                 /* auto-launch deadline */
static bool over;

static void rebuild_bricks(void)
{
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            bricks[r][c] = (uint8_t)(3 - r / 2);  /* rows 0-1:3, 2-3:2, 4:1 */
        }
    }
}

static void reset_ball(void)
{
    bx = pad_x + PAD_W / 2.0f - BALL_SIZE / 2.0f;
    by = PAD_Y - BALL_SIZE;
    launched = false;
    launch_until_us = esp_timer_get_time() + 1500000;
}

static void launch_ball(void)
{
    launched = true;
    bvx = 1.0f;
    bvy = -1.0f;
    float len = sqrtf(bvx * bvx + bvy * bvy);
    bvx = bvx / len * ball_speed;
    bvy = bvy / len * ball_speed;
}

static void breakout_reset(void)
{
    level = 1;
    lives = 3;
    score = 0;
    ball_speed = BASE_SPEED;
    over = false;
    pad_x = (DISPLAY_WIDTH - PAD_W) / 2.0f;
    rebuild_bricks();
    reset_ball();
}

static void game_over_seq(void)
{
    over = true;
    score_submit("breakout", score);
    buzzer_tone(200, 200);
}

static void paddle_bounce(void)
{
    float rel = (bx + BALL_SIZE / 2.0f - (pad_x + PAD_W / 2.0f)) / (PAD_W / 2.0f);
    if (rel > 1.0f) rel = 1.0f;
    if (rel < -1.0f) rel = -1.0f;
    /* horizontal component from hit position, vertical always up */
    float ang = rel * 60.0f * M_PI / 180.0f;
    bvx = sinf(ang) * ball_speed;
    bvy = -fabsf(cosf(ang)) * ball_speed;
    buzzer_tone(500, 15);
}

static void hit_brick(int r, int c)
{
    int val = bricks[r][c];
    bricks[r][c] = 0;
    score += (uint32_t)val * 10;
    ball_speed += 10.0f;
    if (ball_speed > MAX_SPEED) ball_speed = MAX_SPEED;
    buzzer_tone((uint16_t)(700 + r * 100), 30);
}

static void physics(float dt)
{
    if (!launched) {
        /* ball sticks to the paddle */
        bx = pad_x + PAD_W / 2.0f - BALL_SIZE / 2.0f;
        by = PAD_Y - BALL_SIZE;
        if (esp_timer_get_time() >= launch_until_us) {
            launch_ball();
        }
        return;
    }

    bx += bvx * dt;
    by += bvy * dt;

    /* walls */
    if (bx < 0.0f) { bx = 0.0f; bvx = -bvx; buzzer_tone(350, 10); }
    if (bx > DISPLAY_WIDTH - BALL_SIZE) {
        bx = DISPLAY_WIDTH - BALL_SIZE;
        bvx = -bvx;
        buzzer_tone(350, 10);
    }
    if (by < 0.0f) { by = 0.0f; bvy = -bvy; buzzer_tone(350, 10); }

    /* paddle */
    if (bvy > 0.0f && by + BALL_SIZE >= PAD_Y && by + BALL_SIZE <= PAD_Y + PAD_H &&
        bx + BALL_SIZE > pad_x && bx < pad_x + PAD_W) {
        by = PAD_Y - BALL_SIZE;
        paddle_bounce();
    }

    /* bricks */
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            if (bricks[r][c] == 0) continue;
            int brx = BRICK_X0 + c * (BRICK_W + BRICK_GAP);
            int bry = BRICK_Y0 + r * (BRICK_H + BRICK_GAP);
            if (bx + BALL_SIZE <= brx || bx >= brx + BRICK_W ||
                by + BALL_SIZE <= bry || by >= bry + BRICK_H) {
                continue;
            }
            int ox = (int)(bx + BALL_SIZE) - brx;                 /* overlap right */
            int ox2 = brx + BRICK_W - (int)bx;                    /* overlap left  */
            int oy = (int)(by + BALL_SIZE) - bry;
            int oy2 = bry + BRICK_H - (int)by;
            int minx = ox < ox2 ? ox : ox2;
            int miny = oy < oy2 ? oy : oy2;
            if (minx < miny) {
                bvx = -bvx;
                bx = (bvx < 0) ? (float)(brx - BALL_SIZE) : (float)(brx + BRICK_W);
            } else {
                bvy = -bvy;
                by = (bvy < 0) ? (float)(bry - BALL_SIZE) : (float)(bry + BRICK_H);
            }
            hit_brick(r, c);
            break;
        }
    }

    /* lost the ball */
    if (by > DISPLAY_HEIGHT) {
        lives--;
        buzzer_tone(250, 150);
        if (lives <= 0) {
            game_over_seq();
            return;
        }
        reset_ball();
        return;
    }

    /* level cleared */
    bool any = false;
    for (int r = 0; r < BRICK_ROWS && !any; r++) {
        for (int c = 0; c < BRICK_COLS && !any; c++) {
            any = bricks[r][c] != 0;
        }
    }
    if (!any) {
        level++;
        ball_speed += 20.0f;
        if (ball_speed > MAX_SPEED) ball_speed = MAX_SPEED;
        buzzer_tone(660, 80);
        buzzer_tone(880, 80);
        buzzer_tone(990, 80);
        rebuild_bricks();
        reset_ball();
    }
}

static void draw_brick(int r, int c, int x, int y)
{
    uint8_t hp = bricks[r][c];
    if (hp == 0) return;
    display_draw_rect(x, y, BRICK_W, BRICK_H, true, true);
    if (hp == 2) {
        display_draw_rect(x + 6, y + 2, 2, 1, true, false);
    } else if (hp == 1) {
        for (int i = 0; i < BRICK_W; i += 2) {
            display_draw_pixel(x + i, y + 1, false);
            display_draw_pixel(x + i + 1, y + 3, false);
        }
    }
}

static void render(void)
{
    display_clear();

    /* bricks */
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            int x = BRICK_X0 + c * (BRICK_W + BRICK_GAP);
            int y = BRICK_Y0 + r * (BRICK_H + BRICK_GAP);
            draw_brick(r, c, x, y);
        }
    }

    /* paddle */
    display_draw_rect((int)pad_x, PAD_Y, PAD_W, PAD_H, true, true);

    /* ball */
    if (lives > 0) {
        display_draw_rect((int)bx, (int)by, BALL_SIZE, BALL_SIZE, true, true);
    }

    /* HUD */
    char buf[24];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)score);
    display_draw_text(2, 0, buf, true);
    snprintf(buf, sizeof(buf), "L%d", lives);
    display_draw_text(112, 0, buf, true);
    snprintf(buf, sizeof(buf), "LV%d", level);
    display_draw_text(54, 0, buf, true);
}

static void render_gameover(void)
{
    display_clear();
    display_draw_rect(0, 0, DISPLAY_WIDTH, 12, true, true);
    display_draw_text_centered(3, "GAME OVER", false);

    char buf[24];
    snprintf(buf, sizeof(buf), "Score: %lu", (unsigned long)score);
    display_draw_text_centered(22, buf, true);

    snprintf(buf, sizeof(buf), "High: %lu", (unsigned long)score_get("breakout"));
    display_draw_text_centered(32, buf, true);

    display_draw_text_centered(46, "OK:Retry  BACK:Menu", true);
}

void game_breakout_run(void)
{
    breakout_reset();
    uint32_t last = esp_timer_get_time();

    while (1) {
        input_event_t ev;
        while (input_get_event(&ev, 0)) {
            if (ev.type == INPUT_EVENT_PRESS) {
                if (ev.key == INPUT_KEY_BACK) return;
                if (ev.key == INPUT_KEY_OK) {
                    if (over) {
                        breakout_reset();
                        last = esp_timer_get_time();
                    } else if (!launched) {
                        launch_ball();
                    }
                }
            }
        }

        uint32_t now = esp_timer_get_time();
        float dt = (float)(now - last) / 1000000.0f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;

        if (!over) {
            /* paddle: buttons + joystick */
            float move = 0.0f;
            if (input_is_pressed(INPUT_KEY_LEFT))  move -= PAD_SPEED;
            if (input_is_pressed(INPUT_KEY_RIGHT)) move += PAD_SPEED;
            int jx, jy;
            input_get_joystick(&jx, &jy);
            if (jx > 20)       move += PAD_SPEED * (jx - 20) / 80.0f;
            else if (jx < -20) move -= PAD_SPEED * (-jx - 20) / 80.0f;

            pad_x += move * dt;
            if (pad_x < 0) pad_x = 0;
            if (pad_x > DISPLAY_WIDTH - PAD_W) pad_x = DISPLAY_WIDTH - PAD_W;

            physics(dt);
        }

        if (over) {
            render_gameover();
        } else {
            render();
        }
        display_show();

        vTaskDelay(pdMS_TO_TICKS(16));
    }
}