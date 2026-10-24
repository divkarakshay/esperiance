#include <stdio.h>

#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buzzer.h"
#include "display.h"
#include "games.h"
#include "input.h"

#define SHIP_W    8
#define SHIP_H    6
#define BULLET_W  2
#define BULLET_H  5
#define MAX_PB    8
#define MAX_AB    10
#define MAX_ALIENS 10
#define MAX_BOOM  8

#define PLAYER_SPEED  140.0f
#define BULLET_SPEED  170.0f
#define ABULLET_SPEED 90.0f
#define FIRE_COOLDOWN 220000      /* us */

typedef struct {
    float x, y;
    bool active;
} bullet_t;

typedef struct {
    float x, y;
    float vx;
    int hp;
    bool big;               /* type B: 2 hp */
    int fire_timer_ms;
    int phase;
    bool active;
} alien_t;

typedef struct {
    int x, y;
    int age_ms;
    bool active;
} boom_t;

static float ship_x, ship_y;
static int lives;
static uint32_t score;
static int wave;
static bullet_t pb[MAX_PB];
static bullet_t ab[MAX_AB];
static alien_t aliens[MAX_ALIENS];
static boom_t booms[MAX_BOOM];
static uint32_t last_fire;
static bool invuln;
static uint32_t invuln_until;
static bool over;
static int spawn_left;
static uint32_t next_spawn_ms;
static uint32_t wave_pause_until;

static void space_reset(void)
{
    ship_x = 6.0f;
    ship_y = 28.0f;
    lives = 3;
    score = 0;
    wave = 1;
    over = false;
    invuln = false;
    for (int i = 0; i < MAX_PB; i++) pb[i].active = false;
    for (int i = 0; i < MAX_AB; i++) ab[i].active = false;
    for (int i = 0; i < MAX_ALIENS; i++) aliens[i].active = false;
    for (int i = 0; i < MAX_BOOM; i++) booms[i].active = false;
    spawn_left = 5 + wave * 3;
    next_spawn_ms = 300;
    wave_pause_until = 0;
    last_fire = 0;
}

static alien_t *spawn_alien(void)
{
    for (int i = 0; i < MAX_ALIENS; i++) {
        if (!aliens[i].active) {
            aliens[i].active = true;
            aliens[i].big = (esp_random() % 3) == 0;
            aliens[i].hp = aliens[i].big ? 2 : 1;
            aliens[i].x = DISPLAY_WIDTH + 10.0f;
            aliens[i].y = 8.0f + (float)(esp_random() % 44);
            aliens[i].vx = (aliens[i].big ? 18.0f : 30.0f) + wave * 3.0f;
            if (aliens[i].vx > 80.0f) aliens[i].vx = 80.0f;
            aliens[i].fire_timer_ms = 800 + (int)(esp_random() % 4000);
            aliens[i].phase = (int)(esp_random() % 100);
            return &aliens[i];
        }
    }
    return NULL;
}

static void fire_bullet(void)
{
    for (int i = 0; i < MAX_PB; i++) {
        if (!pb[i].active) {
            pb[i].active = true;
            pb[i].x = ship_x + SHIP_W;
            pb[i].y = ship_y + SHIP_H / 2 - 1;
            return;
        }
    }
}

static void add_boom(int x, int y)
{
    for (int i = 0; i < MAX_BOOM; i++) {
        if (!booms[i].active) {
            booms[i].active = true;
            booms[i].x = x;
            booms[i].y = y;
            booms[i].age_ms = 0;
            return;
        }
    }
}

static void kill_alien(int i)
{
    aliens[i].active = false;
    add_boom((int)aliens[i].x, (int)aliens[i].y);
    score += aliens[i].big ? 25 : 10;
    buzzer_tone(700, 20);
}

static void hit_player(void)
{
    lives--;
    buzzer_tone(200, 150);
    add_boom((int)ship_x, (int)ship_y);
    if (lives <= 0) {
        over = true;
        score_submit("space", score);
        return;
    }
    invuln = true;
    invuln_until = esp_timer_get_time() + 2000000;
}

static bool collide(float ax, float ay, float aw, float ah,
                    float bx, float by, float bw, float bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static void update(float dt, uint32_t now_ms)
{
    if (over) return;

    /* ship movement: buttons + joystick */
    float move_x = 0, move_y = 0;
    if (input_is_pressed(INPUT_KEY_UP))    move_y -= PLAYER_SPEED;
    if (input_is_pressed(INPUT_KEY_DOWN))  move_y += PLAYER_SPEED;
    if (input_is_pressed(INPUT_KEY_LEFT))  move_x -= PLAYER_SPEED;
    if (input_is_pressed(INPUT_KEY_RIGHT)) move_x += PLAYER_SPEED;
    int jx, jy;
    input_get_joystick(&jx, &jy);
    if (jx > 20)       move_x += PLAYER_SPEED * (jx - 20) / 80.0f;
    else if (jx < -20) move_x -= PLAYER_SPEED * (-jx - 20) / 80.0f;
    if (jy > 20)       move_y -= PLAYER_SPEED * (jy - 20) / 80.0f;
    else if (jy < -20) move_y += PLAYER_SPEED * (-jy - 20) / 80.0f;

    ship_x += move_x * dt;
    ship_y += move_y * dt;
    if (ship_x < 2) ship_x = 2;
    if (ship_x > 60) ship_x = 60;
    if (ship_y < 0) ship_y = 0;
    if (ship_y > DISPLAY_HEIGHT - SHIP_H) ship_y = DISPLAY_HEIGHT - SHIP_H;

    /* auto-fire */
    if (now_ms >= last_fire + FIRE_COOLDOWN / 1000) {
        last_fire = now_ms;
        fire_bullet();
    }

    /* player bullets */
    for (int i = 0; i < MAX_PB; i++) {
        if (!pb[i].active) continue;
        pb[i].x += BULLET_SPEED * dt;
        if (pb[i].x > DISPLAY_WIDTH) {
            pb[i].active = false;
            continue;
        }
        for (int a = 0; a < MAX_ALIENS; a++) {
            if (!aliens[a].active) continue;
            float aw = aliens[a].big ? 9.0f : 6.0f;
            float ah = aliens[a].big ? 9.0f : 6.0f;
            if (collide(pb[i].x, pb[i].y, BULLET_W, BULLET_H,
                        aliens[a].x, aliens[a].y, aw, ah)) {
                pb[i].active = false;
                aliens[a].hp--;
                if (aliens[a].hp <= 0) {
                    kill_alien(a);
                } else {
                    add_boom((int)aliens[a].x, (int)aliens[a].y);
                    buzzer_tone(600, 15);
                }
            }
        }
    }

    /* wave spawn */
    if (now_ms >= next_spawn_ms && spawn_left > 0) {
        spawn_alien();
        spawn_left--;
        next_spawn_ms = now_ms + (uint32_t)(420 - wave * 20);
        if (next_spawn_ms < 250) next_spawn_ms = 250;
    }

    /* aliens */
    for (int a = 0; a < MAX_ALIENS; a++) {
        if (!aliens[a].active) continue;
        aliens[a].phase++;
        aliens[a].x -= aliens[a].vx * dt;
        aliens[a].y += 8.0f * dt * ((aliens[a].phase % 60) < 30 ? 1 : -1);
        if (aliens[a].x < -12.0f) {
            aliens[a].active = false;
            continue;
        }
        /* alien vs ship */
        if (!invuln && collide(ship_x, ship_y, SHIP_W, SHIP_H,
                               aliens[a].x, aliens[a].y,
                               aliens[a].big ? 9.0f : 6.0f,
                               aliens[a].big ? 9.0f : 6.0f)) {
            aliens[a].active = false;
            hit_player();
            continue;
        }
        /* alien fires */
        aliens[a].fire_timer_ms -= (int)(dt * 1000);
        if (aliens[a].fire_timer_ms <= 0) {
            aliens[a].fire_timer_ms = 1200 + (int)(esp_random() % 3000);
            for (int i = 0; i < MAX_AB; i++) {
                if (!ab[i].active) {
                    ab[i].active = true;
                    ab[i].x = aliens[a].x;
                    ab[i].y = aliens[a].y + 2.0f;
                    break;
                }
            }
        }
    }

    /* alien bullets */
    for (int i = 0; i < MAX_AB; i++) {
        if (!ab[i].active) continue;
        ab[i].x -= ABULLET_SPEED * dt;
        if (ab[i].x < -8.0f) {
            ab[i].active = false;
            continue;
        }
        if (!invuln && collide(ab[i].x, ab[i].y, BULLET_W, BULLET_H,
                               ship_x, ship_y, SHIP_W, SHIP_H)) {
            ab[i].active = false;
            hit_player();
        }
    }

    /* invulnerability expiry */
    if (invuln && esp_timer_get_time() >= invuln_until) {
        invuln = false;
    }

    /* wave cleared? */
    if (spawn_left == 0) {
        bool any = false;
        for (int i = 0; i < MAX_ALIENS; i++) {
            any = any || aliens[i].active;
        }
        if (!any && now_ms >= wave_pause_until) {
            wave++;
            spawn_left = 5 + wave * 3;
            next_spawn_ms = now_ms + 1000;
            wave_pause_until = now_ms + 1000;
            buzzer_tone(880, 60);
            buzzer_tone(1175, 60);
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
    snprintf(buf, sizeof(buf), "W%d L%d", wave, lives);
    display_draw_text(94, 0, buf, true);

    /* ship (blink while invulnerable) */
    if (!invuln || (esp_timer_get_time() / 100000) % 2 == 0) {
        display_draw_rect((int)ship_x, (int)ship_y, SHIP_W, SHIP_H, true, true);
        display_draw_rect((int)ship_x + SHIP_W, (int)ship_y + 2, 2, 2, true, true);
    }

    /* player bullets */
    for (int i = 0; i < MAX_PB; i++) {
        if (pb[i].active) {
            display_draw_rect((int)pb[i].x, (int)pb[i].y, BULLET_W, BULLET_H,
                              true, true);
        }
    }

    /* aliens */
    for (int a = 0; a < MAX_ALIENS; a++) {
        if (!aliens[a].active) continue;
        int ax = (int)aliens[a].x;
        int ay = (int)aliens[a].y;
        if (aliens[a].big) {
            display_draw_rect(ax, ay, 9, 9, true, true);
            display_draw_rect(ax + 2, ay + 2, 5, 5, true, false);
        } else {
            display_draw_rect(ax, ay, 6, 6, false, true);
        }
    }

    /* alien bullets */
    for (int i = 0; i < MAX_AB; i++) {
        if (ab[i].active) {
            display_draw_rect((int)ab[i].x, (int)ab[i].y, BULLET_W, BULLET_H,
                              true, true);
        }
    }

    /* explosions */
    for (int i = 0; i < MAX_BOOM; i++) {
        if (!booms[i].active) continue;
        int r = booms[i].age_ms / 30 + 1;
        display_draw_rect(booms[i].x - r, booms[i].y - r,
                          r * 2, r * 2, false, true);
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
    snprintf(buf, sizeof(buf), "Wave: %d", wave);
    display_draw_text_centered(32, buf, true);
    snprintf(buf, sizeof(buf), "High: %lu", (unsigned long)score_get("space"));
    display_draw_text_centered(42, buf, true);
    display_draw_text_centered(54, "OK:Retry  BACK:Menu", true);
}

void game_space_run(void)
{
    space_reset();
    uint32_t last = esp_timer_get_time();

    while (1) {
        input_event_t ev;
        while (input_get_event(&ev, 0)) {
            if (ev.type == INPUT_EVENT_PRESS) {
                if (ev.key == INPUT_KEY_BACK) return;
                if (ev.key == INPUT_KEY_OK && over) {
                    space_reset();
                    last = esp_timer_get_time();
                }
            }
        }

        uint32_t now = esp_timer_get_time();
        float dt = (float)(now - last) / 1000000.0f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;

        uint32_t now_ms = now / 1000;

        if (!over) {
            update(dt, now_ms);
        }
        for (int i = 0; i < MAX_BOOM; i++) {
            if (booms[i].active) {
                booms[i].age_ms += (int)(dt * 1000);
                if (booms[i].age_ms > 300) booms[i].active = false;
            }
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