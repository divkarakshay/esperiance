#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buzzer.h"
#include "display.h"
#include "games.h"
#include "input.h"

#define CELL        8
#define GW          16
#define GH          7
#define OY          8

#define PW          5
#define PH          6

#define GRAV        420.0f
#define JUMP_V      120.0f
#define MAXV        260.0f
#define SPEED       55.0f

#define LEVEL_COUNT 3
#define START_LIVES 3

typedef struct {
    int sx, sy;             /* player start cell */
    const char *map[GH];    /* GW chars per row */
} level_t;

static const level_t levels[LEVEL_COUNT] = {
    {
        1, 5,
        {
            "################",
            "#..............#",
            "#..............#",
            "#..............#",
            "#..f......f....#",
            "#P.###....###.E#",
            "################",
        },
    },
    {
        1, 5,
        {
            "################",
            "#..............#",
            "#...f.....f....#",
            "#..............#",
            "#..f........f..#",
            "#P.###..###...E#",
            "########..######",
        },
    },
    {
        1, 5,
        {
            "################",
            "#..............#",
            "#...f..f....f..#",
            "#..............#",
            "#..###..###....#",
            "#P..f........E.#",
            "################",
        },
    },
};

static char map[GH][GW + 1];    /* runtime copy */
static float px, py, vx, vy;
static bool on_ground;
static int level_idx;
static int lives;
static int fruit_total;
static uint32_t score;
static bool level_done;
static bool game_over;
static bool won;
static bool paused;
static int start_px, start_py;

static bool solid_at(int gx, int gy)
{
    if (gx < 0 || gx >= GW) return true;
    if (gy < 0) return false;
    if (gy >= GH) return true;
    return map[gy][gx] == '#';
}

static bool is_supported(void)
{
    int cy = (int)((py + PH) / CELL);
    int x0 = (int)(px / CELL);
    int x1 = (int)((px + PW - 1) / CELL);
    return solid_at(x0, cy) || solid_at(x1, cy);
}

static void start_level(int idx)
{
    level_idx = idx;
    const level_t *l = &levels[idx];
    memcpy(map, l->map, GH * (GW + 1));
    start_px = l->sx * CELL;
    start_py = l->sy * CELL;
    level_done = false;
    px = (float)start_px;
    py = (float)start_py;
    vx = vy = 0.0f;
    on_ground = is_supported();
    paused = false;
}

static void flan_reset(void)
{
    score = 0;
    fruit_total = 0;
    lives = START_LIVES;
    game_over = false;
    won = false;
    start_level(0);
}

static void collect_center(void)
{
    int cx = (int)((px + PW / 2) / CELL);
    int cy = (int)((py + PH / 2) / CELL);
    if (cx < 0 || cx >= GW || cy < 0 || cy >= GH) return;

    char t = map[cy][cx];
    if (t == 'f') {
        map[cy][cx] = '.';
        fruit_total++;
        score += 100;
        buzzer_tone(1100, 40);
    } else if (t == 'E') {
        map[cy][cx] = '.';
        level_done = true;
        score += 300;
        buzzer_tone(660, 60);
        buzzer_tone(880, 60);
        buzzer_tone(1175, 80);
    }
}

static void die(void)
{
    lives--;
    buzzer_tone(300, 100);
    buzzer_tone(200, 120);
    if (lives <= 0) {
        game_over = true;
        score_submit("flan", score);
    } else {
        px = (float)start_px;
        py = (float)start_py;
        vx = vy = 0.0f;
        on_ground = is_supported();
    }
}

static void update(float dt)
{
    if (on_ground) vx = 0.0f;

    int jx, jy;
    input_get_joystick(&jx, &jy);
    if (input_is_pressed(INPUT_KEY_LEFT) || jx < 0) vx = -SPEED;
    else if (input_is_pressed(INPUT_KEY_RIGHT) || jx > 0) vx = SPEED;

    vy += GRAV * dt;
    if (vy > MAXV) vy = MAXV;

    /* horizontal */
    float nx = px + vx * dt;
    if (vx > 0) {
        int cx = (int)((nx + PW - 1) / CELL);
        int y0 = (int)(py / CELL), y1 = (int)((py + PH - 1) / CELL);
        if (solid_at(cx, y0) || solid_at(cx, y1)) {
            nx = cx * CELL - PW;
            vx = 0.0f;
        }
    } else if (vx < 0) {
        int cx = (int)(nx / CELL);
        int y0 = (int)(py / CELL), y1 = (int)((py + PH - 1) / CELL);
        if (solid_at(cx, y0) || solid_at(cx, y1)) {
            nx = (cx + 1) * CELL;
            vx = 0.0f;
        }
    }
    px = nx;

    /* vertical */
    float ny = py + vy * dt;
    if (vy > 0) {
        int cy = (int)((ny + PH - 1) / CELL);
        int x0 = (int)(px / CELL), x1 = (int)((px + PW - 1) / CELL);
        if (solid_at(x0, cy) || solid_at(x1, cy)) {
            ny = cy * CELL - PH;
            vy = 0.0f;
        }
    } else if (vy < 0) {
        int cy = (int)(ny / CELL);
        int x0 = (int)(px / CELL), x1 = (int)((px + PW - 1) / CELL);
        if (solid_at(x0, cy) || solid_at(x1, cy)) {
            ny = (cy + 1) * CELL;
            vy = 0.0f;
        }
    }
    py = ny;

    on_ground = is_supported();
    collect_center();

    if (py > DISPLAY_HEIGHT) die();
}

static void draw_flan(void)
{
    float t = esp_timer_get_time() / 1000000.0f;
    int w = PW, h = PH;
    if (!on_ground) {
        w = PW + 2;
        h = PH - 3;
    } else {
        int d = (int)(2.0f * sinf(t * 6.0f));
        w = PW + d;
        h = PH - d;
    }
    if (w < 3) w = 3;
    if (h < 3) h = 3;

    int cx = (int)px + PW / 2;
    int cy = (int)py + PH / 2;
    int rx = w / 2, ry = h / 2;

    for (int yy = -ry; yy <= ry; yy++) {
        int hw = (int)(rx * sqrtf(1.0f - (float)(yy * yy) / (float)(ry * ry)));
        display_draw_rect(cx - hw, cy + yy, hw * 2 + 1, 1, true, true);
    }
    display_draw_rect(cx - 2, cy - 2, 2, 2, true, false);
    display_draw_rect(cx + 1, cy - 2, 2, 2, true, false);
    display_draw_rect(cx - 1, cy + 1, 3, 1, true, false);
}

static void render(void)
{
    display_clear();

    for (int y = 0; y < GH; y++) {
        for (int x = 0; x < GW; x++) {
            char t = map[y][x];
            if (t == '#') {
                display_draw_rect(x * CELL, OY + y * CELL, CELL, CELL, true, true);
            } else if (t == 'f') {
                /* fruit: little berry */
                display_draw_circle(x * CELL + 4, OY + y * CELL + 4, 2, true);
                display_draw_rect(x * CELL + 3, OY + y * CELL + 1, 2, 2, true, true);
            } else if (t == 'E') {
                /* exit door */
                display_draw_rect(x * CELL + 2, OY + y * CELL, 4, CELL, true, false);
                display_draw_rect(x * CELL + 3, OY + y * CELL + 2, 2, 2, true, true);
            }
        }
    }

    draw_flan();

    char buf[24];
    snprintf(buf, sizeof(buf), "LV %d", level_idx + 1);
    display_draw_text(2, 0, buf, true);
    snprintf(buf, sizeof(buf), "SC %lu", (unsigned long)score);
    display_draw_text(44, 0, buf, true);
    snprintf(buf, sizeof(buf), "Lives %d", lives);
    display_draw_text(88, 0, buf, true);
}

static void render_over(void)
{
    display_clear();
    display_draw_rect(0, 0, DISPLAY_WIDTH, 12, true, true);
    display_draw_text_centered(3, won ? "FLAN ESCAPED!" : "FLAN FLOPPED!", false);

    char buf[24];
    snprintf(buf, sizeof(buf), "Fruit: %d", fruit_total);
    display_draw_text_centered(22, buf, true);
    snprintf(buf, sizeof(buf), "Score: %lu", (unsigned long)score);
    display_draw_text_centered(32, buf, true);
    snprintf(buf, sizeof(buf), "High: %lu", (unsigned long)score_get("flan"));
    display_draw_text_centered(42, buf, true);

    display_draw_text_centered(54, "OK:Retry  BACK:Menu", true);
}

void game_flan_run(void)
{
    flan_reset();

    while (1) {
        input_event_t ev;
        while (input_get_event(&ev, 0)) {
            if (ev.type != INPUT_EVENT_PRESS) continue;
            switch (ev.key) {
            case INPUT_KEY_UP:
            case INPUT_KEY_OK:
                if (game_over || won) {
                    flan_reset();
                } else if (level_done) {
                    if (level_idx + 1 < LEVEL_COUNT) {
                        start_level(level_idx + 1);
                    } else {
                        won = true;
                        score_submit("flan", score);
                    }
                } else if (on_ground) {
                    vy = -JUMP_V;
                    on_ground = false;
                    buzzer_tone(700, 20);
                }
                break;
            case INPUT_KEY_SHIFT:
                if (paused) {
                    paused = false;
                } else if (!game_over && !won && !level_done) {
                    paused = true;
                } else {
                    start_level(level_idx);
                }
                break;
            case INPUT_KEY_BACK:
                return;
            default:
                break;
            }
        }

        if (!game_over && !won && !level_done && !paused) {
            update(0.016f);
        }

        if (game_over || won) {
            render_over();
        } else {
            render();
            if (level_done) {
                char buf[24];
                snprintf(buf, sizeof(buf), "OK:Next Level");
                display_draw_text_centered(57, buf, true);
            } else if (paused) {
                display_draw_text_centered(57, "Paused (SHIFT)", true);
            }
        }
        display_show();

        vTaskDelay(pdMS_TO_TICKS(16));
    }
}
