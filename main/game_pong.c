#include <stdio.h>
#include <math.h>

#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buzzer.h"
#include "display.h"
#include "games.h"
#include "input.h"

#define PAD_W       4
#define PAD_H       20
#define PAD_L_X     8
#define PAD_R_X     (DISPLAY_WIDTH - 8 - PAD_W)
#define BALL_SIZE   3
#define WIN_SCORE   7

#define BASE_SPEED  95.0f
#define MAX_SPEED   230.0f
#define AI_MAX_SPEED 65.0f
#define PLAYER_SPEED 85.0f

static float pad_l_y, pad_r_y;      /* top of paddles */
static float bx, by, bvx, bvy;
static float ball_speed;
static int score_l, score_r;
static bool serving;
static uint32_t serve_until_us;
static int serve_dir;               /* +1 right, -1 left */
static bool over;
static bool player_won;

static void reset_ball(void)
{
    bx = DISPLAY_WIDTH / 2.0f;
    by = DISPLAY_HEIGHT / 2.0f;
    ball_speed = BASE_SPEED;
    serving = true;
    serve_until_us = esp_timer_get_time() + 1000000;
}

static void serve(int dir)
{
    serve_dir = dir;
    reset_ball();
    int rnd = (int)(esp_random() % 30) - 15;
    float ang = (float)rnd * M_PI / 180.0f;
    bvx = (float)serve_dir * cosf(ang);
    bvy = sinf(ang);
    /* normalize to ball_speed */
    float len = sqrtf(bvx * bvx + bvy * bvy);
    bvx = bvx / len * ball_speed;
    bvy = bvy / len * ball_speed;
}

static void ai_move(float dt)
{
    if (bvx < 0) return;            /* ball moving away: hold position */
    float target = by - PAD_H / 2.0f;
    float diff = target - pad_r_y;
    float max = AI_MAX_SPEED * dt;
    if (diff > max) diff = max;
    if (diff < -max) diff = -max;
    pad_r_y += diff;
    if (pad_r_y < 0) pad_r_y = 0;
    if (pad_r_y > DISPLAY_HEIGHT - PAD_H) pad_r_y = DISPLAY_HEIGHT - PAD_H;
}

static void player_bounce(float pad_y)
{
    float rel = (by - (pad_y + PAD_H / 2.0f)) / (PAD_H / 2.0f);
    if (rel > 1.0f) rel = 1.0f;
    if (rel < -1.0f) rel = -1.0f;
    float ang = rel * 60.0f * M_PI / 180.0f;
    float dir = (bvx < 0) ? 1.0f : -1.0f;
    bvx = dir * cosf(ang) * ball_speed;
    bvy = sinf(ang) * ball_speed;
    buzzer_tone(600, 15);
}

static void physics(float dt)
{
    if (serving) {
        if (esp_timer_get_time() >= serve_until_us) {
            serving = false;
            bx += bvx * dt;
            by += bvy * dt;
        }
        return;
    }

    bx += bvx * dt;
    by += bvy * dt;

    /* walls */
    if (by < 1.0f) { by = 1.0f; bvy = -bvy; buzzer_tone(400, 10); }
    if (by > DISPLAY_HEIGHT - 1.0f - BALL_SIZE) {
        by = DISPLAY_HEIGHT - 1.0f - BALL_SIZE;
        bvy = -bvy;
        buzzer_tone(400, 10);
    }

    /* paddles */
    if (bvx < 0 && bx < PAD_L_X + PAD_W &&
        bx + BALL_SIZE > PAD_L_X &&
        by + BALL_SIZE > pad_l_y && by < pad_l_y + PAD_H) {
        bx = PAD_L_X + PAD_W;
        ball_speed += 8.0f;
        if (ball_speed > MAX_SPEED) ball_speed = MAX_SPEED;
        player_bounce(pad_l_y);
    }
    if (bvx > 0 && bx + BALL_SIZE > PAD_R_X &&
        bx < PAD_R_X + PAD_W &&
        by + BALL_SIZE > pad_r_y && by < pad_r_y + PAD_H) {
        bx = PAD_R_X - BALL_SIZE;
        ball_speed += 8.0f;
        if (ball_speed > MAX_SPEED) ball_speed = MAX_SPEED;
        player_bounce(pad_r_y);
    }

    /* scoring */
    if (bx < -BALL_SIZE) {
        score_r++;
        buzzer_tone(220, 120);
        serve(-1);
    } else if (bx > DISPLAY_WIDTH + BALL_SIZE) {
        score_l++;
        buzzer_tone(220, 120);
        serve(1);
    }

    if (score_l >= WIN_SCORE || score_r >= WIN_SCORE) {
        over = true;
        player_won = (score_l >= WIN_SCORE);
        score_submit("pong", (uint32_t)score_l);
        buzzer_tone(player_won ? 1568 : 392, 150);
    }
}

static void render(void)
{
    display_clear();

    /* dashed center line */
    for (int y = 4; y < DISPLAY_HEIGHT; y += 8) {
        display_draw_rect(DISPLAY_WIDTH / 2 - 1, y, 1, 4, true, true);
    }

    /* paddles */
    display_draw_rect(PAD_L_X, (int)pad_l_y, PAD_W, PAD_H, true, true);
    display_draw_rect(PAD_R_X, (int)pad_r_y, PAD_W, PAD_H, true, true);

    /* ball */
    display_draw_rect((int)bx, (int)by, BALL_SIZE, BALL_SIZE, true, true);

    /* scores */
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", score_l);
    display_draw_text(52, 0, buf, true);
    snprintf(buf, sizeof(buf), "%d", score_r);
    display_draw_text(74, 0, buf, true);
}

static void render_result(void)
{
    display_clear();
    display_draw_rect(0, 0, DISPLAY_WIDTH, 12, true, true);
    display_draw_text_centered(3, player_won ? "YOU WIN!" : "YOU LOSE",
                               false);

    char buf[24];
    snprintf(buf, sizeof(buf), "You %d - %d AI", score_l, score_r);
    display_draw_text_centered(22, buf, true);

    snprintf(buf, sizeof(buf), "High: %lu", (unsigned long)score_get("pong"));
    display_draw_text_centered(32, buf, true);

    display_draw_text_centered(46, "OK:Retry  BACK:Menu", true);
}

static void pong_reset(void)
{
    pad_l_y = (DISPLAY_HEIGHT - PAD_H) / 2.0f;
    pad_r_y = (DISPLAY_HEIGHT - PAD_H) / 2.0f;
    score_l = score_r = 0;
    over = false;
    serve(1);
}

void game_pong_run(void)
{
    pong_reset();
    uint32_t last = esp_timer_get_time();

    while (1) {
        input_event_t ev;
        while (input_get_event(&ev, 0)) {
            if (ev.type == INPUT_EVENT_PRESS) {
                if (ev.key == INPUT_KEY_BACK) return;
                if (ev.key == INPUT_KEY_OK && over) {
                    pong_reset();
                    last = esp_timer_get_time();
                }
            }
        }

        uint32_t now = esp_timer_get_time();
        float dt = (float)(now - last) / 1000000.0f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;

        if (!over) {
            /* player paddle: buttons + analog joystick */
            float move = 0.0f;
            if (input_is_pressed(INPUT_KEY_UP))    move -= PLAYER_SPEED;
            if (input_is_pressed(INPUT_KEY_DOWN))  move += PLAYER_SPEED;
            int jx, jy;
            input_get_joystick(&jx, &jy);
            if (jy > 20)       move -= PLAYER_SPEED * (jy - 20) / 80.0f;
            else if (jy < -20) move += PLAYER_SPEED * (-jy - 20) / 80.0f;

            pad_l_y += move * dt;
            if (pad_l_y < 0) pad_l_y = 0;
            if (pad_l_y > DISPLAY_HEIGHT - PAD_H) pad_l_y = DISPLAY_HEIGHT - PAD_H;

            ai_move(dt);
            physics(dt);
        }

        if (over) {
            render_result();
        } else {
            render();
        }
        display_show();

        vTaskDelay(pdMS_TO_TICKS(16));
    }
}