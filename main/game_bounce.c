#include <stdio.h>
#include <math.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buzzer.h"
#include "display.h"
#include "games.h"
#include "input.h"

#define TILE        16
#define GRID_W      8
#define GRID_H      3
#define OY          10

#define BALL_W      6
#define BALL_H      6
#define ENEMY_W     5
#define ENEMY_H     5
#define ENEMY_SPEED 18.0f

#define LEVEL_COUNT 3

typedef struct {
    int x, y;               /* starting tile (col, row) for the ball */
    int dx, dy;             /* ball direction */
    const char *map[GRID_H];
    int enemy_count;
    struct { int x, y; int dx, dy; } enemies[2];
} level_t;

typedef struct {
    float x, y;
    int dx, dy;
} ball_t;

typedef struct {
    float x, y;
    int dx, dy;
    bool active;
} enemy_t;

static const level_t levels[LEVEL_COUNT] = {
    {
        1, 0, 1, 1,
        { "#......#", "#d.##.d#", "####...#" },
        0,
        { { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
    },
    {
        1, 0, 1, 1,
        { "#.S....#", "#d..d.e#", "#.S....#" },
        1,
        { { 6, 1, -1, 0 }, { 0, 0, 0, 0 } },
    },
    {
        2, 0, 1, 1,
        { "#d..e.d#", "#..##..#", "#S.d..S#" },
        1,
        { { 4, 0, 0, 1 }, { 0, 0, 0, 0 } },
    },
};

static int level_idx;
static char map[GRID_H][GRID_W];        /* runtime copy of current level */
static ball_t ball;
static enemy_t enemies[2];
static int lives;
static uint32_t score;
static uint32_t ball_speed;
static uint32_t reset_until_ms;         /* pause after death */
static bool over;
static bool level_complete;

static int tile_at(int tx, int ty)
{
    if (tx < 0 || tx >= GRID_W || ty < 0 || ty >= GRID_H) return '#';
    return map[ty][tx];
}

static void start_level(int idx);

static int count_diamonds(void)
{
    int n = 0;
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            if (map[y][x] == 'd') n++;
        }
    }
    return n;
}

static void place_ball(void)
{
    const level_t *l = &levels[level_idx];
    ball.x = l->x * TILE + (TILE - BALL_W) / 2.0f;
    ball.y = OY + l->y * TILE + (TILE - BALL_H) / 2.0f;
    ball.dx = l->dx;
    ball.dy = l->dy;
}

static void bounce_reset(void)
{
    level_idx = 0;
    lives = 3;
    score = 0;
    ball_speed = 55;
    over = false;
    level_complete = false;
    reset_until_ms = 0;
    for (int i = 0; i < 2; i++) enemies[i].active = false;
    start_level(0);
}

static void start_level(int idx)
{
    level_idx = idx;
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            map[y][x] = levels[idx].map[y][x];
        }
    }
    for (int i = 0; i < 2; i++) enemies[i].active = false;
    const level_t *l = &levels[level_idx];
    for (int i = 0; i < l->enemy_count; i++) {
        enemies[i].active = true;
        enemies[i].x = l->enemies[i].x * TILE + (TILE - ENEMY_W) / 2.0f;
        enemies[i].y = OY + l->enemies[i].y * TILE + (TILE - ENEMY_H) / 2.0f;
        enemies[i].dx = l->enemies[i].dx;
        enemies[i].dy = l->enemies[i].dy;
    }
    place_ball();
}

static bool ball_vs_rect(float rx, float ry, float rw, float rh)
{
    return ball.x < rx + rw && ball.x + BALL_W > rx &&
           ball.y < ry + rh && ball.y + BALL_H > ry;
}

static void hit_player(void);

static void collect_center(void)
{
    int tx = (int)((ball.x + BALL_W / 2.0f) / TILE);
    int ty = (int)((ball.y + BALL_H / 2.0f) / TILE);
    int c = tile_at(tx, ty);

    if (c == 'd') {
        map[ty][tx] = '.';
        score += 10;
        buzzer_tone(1000, 30);
        if (count_diamonds() == 0) {
            level_complete = true;
            score += 50;
            buzzer_tone(880, 60);
            buzzer_tone(1175, 60);
        }
    } else if (c == 'S') {
        hit_player();
    }
}

static void hit_player(void)
{
    lives--;
    buzzer_tone(200, 200);
    if (lives <= 0) {
        over = true;
        score_submit("bounce", score);
        return;
    }
    place_ball();
    reset_until_ms = 0;
    /* brief pause */
    reset_until_ms = 400;
}

static void move_ball(float dt)
{
    float dist = ball_speed * dt;
    while (dist > 0.0f) {
        float s = dist > 1.0f ? 1.0f : dist;
        dist -= s;

        float nx = ball.x + ball.dx * s;
        if (ball.dx > 0) {
            int tx = (int)((nx + BALL_W - 1) / TILE);
            int ty0 = (int)(ball.y / TILE);
            int ty1 = (int)((ball.y + BALL_H - 1) / TILE);
            bool hit = false;
            for (int ty = ty0; ty <= ty1; ty++) {
                if (tile_at(tx, ty) == '#') { hit = true; break; }
            }
            if (hit) { ball.dx = -ball.dx; nx = ball.x; }
        } else if (ball.dx < 0) {
            int tx = (int)(nx / TILE);
            int ty0 = (int)(ball.y / TILE);
            int ty1 = (int)((ball.y + BALL_H - 1) / TILE);
            bool hit = false;
            for (int ty = ty0; ty <= ty1; ty++) {
                if (tile_at(tx, ty) == '#') { hit = true; break; }
            }
            if (hit) { ball.dx = -ball.dx; nx = ball.x; }
        }
        ball.x = nx;

        float ny = ball.y + ball.dy * s;
        if (ball.dy > 0) {
            int ty = (int)((ny + BALL_H - 1) / TILE);
            int tx0 = (int)(ball.x / TILE);
            int tx1 = (int)((ball.x + BALL_W - 1) / TILE);
            bool hit = false;
            for (int tx = tx0; tx <= tx1; tx++) {
                if (tile_at(tx, ty) == '#') { hit = true; break; }
            }
            if (hit) { ball.dy = -ball.dy; ny = ball.y; }
        } else if (ball.dy < 0) {
            int ty = (int)(ny / TILE);
            int tx0 = (int)(ball.x / TILE);
            int tx1 = (int)((ball.x + BALL_W - 1) / TILE);
            bool hit = false;
            for (int tx = tx0; tx <= tx1; tx++) {
                if (tile_at(tx, ty) == '#') { hit = true; break; }
            }
            if (hit) { ball.dy = -ball.dy; ny = ball.y; }
        }
        ball.y = ny;

        collect_center();
    }
}

static void update_enemies(float dt)
{
    const level_t *l = &levels[level_idx];
    for (int i = 0; i < l->enemy_count && i < 2; i++) {
        if (!enemies[i].active) continue;
        float cx = enemies[i].x + ENEMY_W / 2.0f + enemies[i].dx * ENEMY_SPEED * dt;
        float cy = enemies[i].y + ENEMY_H / 2.0f + enemies[i].dy * ENEMY_SPEED * dt;
        int tx = (int)(cx / TILE);
        int ty = (int)(cy / TILE);
        if (tx < 0 || tx >= GRID_W || ty < 0 || ty >= GRID_H ||
            tile_at(tx, ty) == '#') {
            enemies[i].dx = -enemies[i].dx;
            enemies[i].dy = -enemies[i].dy;
        } else {
            enemies[i].x += enemies[i].dx * ENEMY_SPEED * dt;
            enemies[i].y += enemies[i].dy * ENEMY_SPEED * dt;
        }

        if (ball_vs_rect(enemies[i].x, enemies[i].y, ENEMY_W, ENEMY_H)) {
            hit_player();
        }
    }
}

static void update(float dt)
{
    if (reset_until_ms > 0) return;
    if (level_complete) return;

    move_ball(dt);
    update_enemies(dt);
}

static void draw_wall(int px, int py)
{
    display_draw_rect(px, py, TILE, TILE, true, true);
}

static void draw_diamond(int px, int py)
{
    display_draw_rect(px + 5, py + 2, 6, 2, true, true);
    display_draw_rect(px + 7, py + 4, 2, 2, true, true);
}

static void draw_spike(int px, int py)
{
    display_draw_rect(px + 7, py, 2, 2, true, true);
    display_draw_rect(px + 6, py + 2, 4, 2, true, true);
    display_draw_rect(px + 5, py + 4, 6, 2, true, true);
    display_draw_rect(px + 4, py + 6, 8, 2, true, true);
}

static void render(void)
{
    display_clear();

    /* HUD */
    char buf[24];
    snprintf(buf, sizeof(buf), "LV %d", level_idx + 1);
    display_draw_text(2, 0, buf, true);
    snprintf(buf, sizeof(buf), "SC %lu", (unsigned long)score);
    display_draw_text(48, 0, buf, true);
    for (int i = 0; i < lives; i++) {
        display_draw_rect(104 + i * 7, 1, 5, 5, true, true);
    }

    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            int c = tile_at(x, y);
            if (c == '#') {
                draw_wall(x * TILE, OY + y * TILE);
            } else if (c == 'd') {
                draw_diamond(x * TILE, OY + y * TILE);
            } else if (c == 'S') {
                draw_spike(x * TILE, OY + y * TILE);
            }
        }
    }

    /* enemies */
    const level_t *l = &levels[level_idx];
    for (int i = 0; i < l->enemy_count && i < 2; i++) {
        if (enemies[i].active) {
            display_draw_circle((int)(enemies[i].x + ENEMY_W / 2),
                                (int)(enemies[i].y + ENEMY_H / 2), 2, true);
            display_draw_circle((int)(enemies[i].x + ENEMY_W / 2),
                                (int)(enemies[i].y + ENEMY_H / 2), 1, false);
        }
    }

    /* ball */
    if (lives > 0) {
        display_draw_rect((int)ball.x, (int)ball.y, BALL_W, BALL_H, true, true);
        display_draw_rect((int)ball.x + 1, (int)ball.y + 1, BALL_W - 2,
                          BALL_H - 2, true, false);
    }
}

static void render_message(const char *msg, const char *sub)
{
    display_clear();
    display_draw_rect(0, 0, DISPLAY_WIDTH, 12, true, true);
    display_draw_text_centered(3, msg, false);
    if (sub) {
        display_draw_text_centered(22, sub, true);
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "Score: %lu", (unsigned long)score);
    display_draw_text_centered(32, buf, true);
    snprintf(buf, sizeof(buf), "High: %lu", (unsigned long)score_get("bounce"));
    display_draw_text_centered(42, buf, true);
    display_draw_text_centered(54, "OK:Retry  BACK:Menu", true);
}

void game_bounce_run(void)
{
    bounce_reset();
    bool show_over = false;
    uint32_t last = esp_timer_get_time();

    while (1) {
        input_event_t ev;
        while (input_get_event(&ev, 0)) {
            if (ev.type != INPUT_EVENT_PRESS) continue;
            if (ev.key == INPUT_KEY_BACK) return;
            if (ev.key == INPUT_KEY_OK) {
                if (show_over) {
                    bounce_reset();
                    show_over = false;
                    last = esp_timer_get_time();
                } else if (level_complete) {
                    int next = level_idx + 1;
                    if (next >= LEVEL_COUNT) next = 0;
                    start_level(next);
                    ball_speed += 4;
                    level_complete = false;
                    last = esp_timer_get_time();
                }
            }
        }

        uint32_t now = esp_timer_get_time();
        float dt = (float)(now - last) / 1000000.0f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;
        uint32_t now_ms = now / 1000;

        if (reset_until_ms > 0 && now_ms >= reset_until_ms) reset_until_ms = 0;

        if (!over && !show_over) {
            update(dt);
            if (over) show_over = true;
        }

        if (show_over) {
            render_message("GAME OVER", NULL);
        } else if (level_complete) {
            render_message("LEVEL CLEAR", "OK:Next Level");
        } else {
            render();
        }
        display_show();

        vTaskDelay(pdMS_TO_TICKS(16));
    }
}
