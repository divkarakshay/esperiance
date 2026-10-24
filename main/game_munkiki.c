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

#define GW          12
#define GH          5
#define CELL        10
#define OX          4
#define OY          8

#define LEVEL_COUNT 3

typedef struct {
    int x, y;               /* player start cell */
    const char *map[GH];    /* GW chars per row */
    int notes;              /* notes to collect */
} level_t;

static const level_t levels[LEVEL_COUNT] = {
    {
        1, 3,
        {
            "############",
            "#..B..n..n.#",
            "#n.........#",
            "#P.........#",
            "############",
        },
        3,
    },
    {
        1, 3,
        {
            "############",
            "#..n.B.B.n.#",
            "#..........#",
            "#P..n.B..n.#",
            "############",
        },
        4,
    },
    {
        1, 3,
        {
            "############",
            "#n.BB..B..n#",
            "#..B....B..#",
            "#n.P....B.n#",
            "############",
        },
        4,
    },
};

static char map[GH][GW + 1];    /* runtime copy (trailing NUL) */
static int px, py;
static int level_idx;
static int notes_left;
static uint32_t notes_total;
static uint32_t score;
static bool level_done;
static bool won;

static char tile_at(int x, int y)
{
    if (x < 0 || x >= GW || y < 0 || y >= GH) return '#';
    return map[y][x];
}

static void start_level(int idx)
{
    level_idx = idx;
    const level_t *l = &levels[idx];
    memcpy(map, l->map, GH * (GW + 1));
    px = l->x;
    py = l->y;
    map[py][px] = '.';
    notes_left = l->notes;
    level_done = false;
}

static void munkiki_reset(void)
{
    notes_total = 0;
    score = 0;
    won = false;
    start_level(0);
}

static void move_player(int dx, int dy)
{
    int nx = px + dx, ny = py + dy;
    char t = tile_at(nx, ny);

    if (t == '#') return;

    if (t == 'B') {
        int bx = nx + dx, by = ny + dy;
        char bt = tile_at(bx, by);
        if (bt != '.' && bt != 'n') return;     /* cannot push */
        map[ny][nx] = '.';
        map[by][bx] = bt == 'n' ? 'n' : 'B';
    }

    px = nx;
    py = ny;

    if (t == 'n') {
        map[py][px] = '.';
        notes_left--;
        notes_total++;
        score += 100;
        buzzer_tone(1100, 40);
        if (notes_left <= 0) {
            level_done = true;
            score += 200;
            buzzer_tone(660, 60);
            buzzer_tone(880, 60);
            buzzer_tone(1175, 80);
        }
    } else {
        buzzer_tone(900, 15);
    }
}

static void render(void)
{
    display_clear();

    for (int y = 0; y < GH; y++) {
        for (int x = 0; x < GW; x++) {
            int xp = OX + x * CELL;
            int yp = OY + y * CELL;
            char t = map[y][x];
            if (t == '#') {
                display_draw_rect(xp, yp, CELL, CELL, true, true);
            } else if (t == 'B') {
                display_draw_rect(xp, yp, CELL, CELL, true, true);
                display_draw_rect(xp + 2, yp + 2, CELL - 4, CELL - 4, true, false);
            } else if (t == 'n') {
                /* musical note */
                display_draw_rect(xp + 4, yp + 2, 2, 5, true, true);
                display_draw_rect(xp + 2, yp + 5, 5, 2, true, true);
                display_draw_rect(xp + 2, yp + 3, 2, 3, true, true);
            } else if (x == px && y == py) {
                /* player: monkey blob */
                display_draw_rect(xp + 2, yp + 1, 6, 8, true, true);
                display_draw_rect(xp + 3, yp + 3, 2, 2, true, false);
                display_draw_rect(xp + 1, yp - 1, 2, 3, true, true);
                display_draw_rect(xp + 7, yp - 1, 2, 3, true, true);
            }
        }
    }

    char buf[24];
    snprintf(buf, sizeof(buf), "LV %d", level_idx + 1);
    display_draw_text(2, 0, buf, true);
    snprintf(buf, sizeof(buf), "SC %lu", (unsigned long)score);
    display_draw_text(48, 0, buf, true);
    snprintf(buf, sizeof(buf), "Notes %d", notes_left);
    display_draw_text(84, 0, buf, true);
}

static void render_win(void)
{
    display_clear();
    display_draw_rect(0, 0, DISPLAY_WIDTH, 12, true, true);
    display_draw_text_centered(3, "CASTLE CLEARED!", false);

    char buf[24];
    snprintf(buf, sizeof(buf), "Notes: %lu", (unsigned long)notes_total);
    display_draw_text_centered(22, buf, true);
    snprintf(buf, sizeof(buf), "Score: %lu", (unsigned long)score);
    display_draw_text_centered(32, buf, true);
    snprintf(buf, sizeof(buf), "High: %lu", (unsigned long)score_get("munkiki"));
    display_draw_text_centered(42, buf, true);

    display_draw_text_centered(54, "OK:Retry  BACK:Menu", true);
}

void game_munkiki_run(void)
{
    munkiki_reset();

    while (1) {
        input_event_t ev;
        while (input_get_event(&ev, 0)) {
            if (ev.type != INPUT_EVENT_PRESS) continue;
            switch (ev.key) {
            case INPUT_KEY_UP:
                if (!level_done && !won) move_player(0, -1);
                break;
            case INPUT_KEY_DOWN:
                if (!level_done && !won) move_player(0, 1);
                break;
            case INPUT_KEY_LEFT:
                if (!level_done && !won) move_player(-1, 0);
                break;
            case INPUT_KEY_RIGHT:
                if (!level_done && !won) move_player(1, 0);
                break;
            case INPUT_KEY_SHIFT:
            case INPUT_KEY_OK:
                if (won) {
                    munkiki_reset();
                } else if (level_done) {
                    if (level_idx + 1 < LEVEL_COUNT) {
                        start_level(level_idx + 1);
                    } else {
                        won = true;
                        score_submit("munkiki", score);
                    }
                } else {
                    start_level(level_idx);     /* restart level */
                    buzzer_tone(500, 30);
                }
                break;
            case INPUT_KEY_BACK:
                return;
            default:
                break;
            }
        }

        if (won) {
            render_win();
        } else {
            render();
            if (level_done) {
                char buf[24];
                snprintf(buf, sizeof(buf), "OK:Next Level");
                display_draw_text_centered(57, buf, true);
            }
        }
        display_show();

        vTaskDelay(pdMS_TO_TICKS(16));
    }
}
