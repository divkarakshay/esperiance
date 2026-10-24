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

#define COLS        10
#define ROWS        6
#define CW          12
#define CH          8
#define OX          4
#define OY          10
#define MINES       14

typedef struct {
    bool mine;
    bool revealed;
    bool flagged;
    uint8_t adj;                /* adjacent mine count */
} cell_t;

static cell_t grid[ROWS][COLS];
static int cur_x, cur_y;
static bool started;            /* mines placed on first reveal */
static bool over;
static bool won;
static uint32_t flags;
static uint32_t revealed_count;
static uint32_t score;

static void beep_move(void)
{
    buzzer_tone(1200, 20);
}

static void mines_reset(void)
{
    memset(grid, 0, sizeof(grid));
    cur_x = COLS / 2;
    cur_y = ROWS / 2;
    started = false;
    over = false;
    won = false;
    flags = 0;
    revealed_count = 0;
    score = 0;
}

static int count_adj(int x, int y)
{
    int n = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) continue;
            if (grid[ny][nx].mine) n++;
        }
    }
    return n;
}

/* First reveal is always safe: keep that cell and its neighbours mine-free. */
static void place_mines(int sx, int sy)
{
    int placed = 0;
    while (placed < MINES) {
        int x = (int)(esp_random() % COLS);
        int y = (int)(esp_random() % ROWS);
        if (grid[y][x].mine) continue;
        if (x >= sx - 1 && x <= sx + 1 && y >= sy - 1 && y <= sy + 1) continue;
        grid[y][x].mine = true;
        placed++;
    }
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            grid[y][x].adj = (uint8_t)count_adj(x, y);
        }
    }
}

static void reveal_region(int x, int y)
{
    if (x < 0 || x >= COLS || y < 0 || y >= ROWS) return;
    cell_t *c = &grid[y][x];
    if (c->revealed || c->flagged || c->mine) return;
    c->revealed = true;
    revealed_count++;
    if (c->adj == 0) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                reveal_region(x + dx, y + dy);
            }
        }
    }
}

static bool all_clear(void)
{
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            if (!grid[y][x].mine && !grid[y][x].revealed) return false;
        }
    }
    return true;
}

static void reveal_cell(int x, int y)
{
    cell_t *c = &grid[y][x];
    if (c->flagged) return;

    if (!started) {
        place_mines(x, y);
        started = true;
    }

    if (c->mine) {
        /* reveal everything, mark the clicked mine */
        for (int yy = 0; yy < ROWS; yy++) {
            for (int xx = 0; xx < COLS; xx++) {
                if (grid[yy][xx].mine) grid[yy][xx].revealed = true;
            }
        }
        grid[y][x].flagged = true;      /* highlight the fatal cell */
        over = true;
        won = false;
        score = revealed_count * 10;
        score_submit("mines", score);
        buzzer_tone(200, 200);
        return;
    }

    reveal_region(x, y);

    if (all_clear()) {
        for (int yy = 0; yy < ROWS; yy++) {
            for (int xx = 0; xx < COLS; xx++) {
                if (grid[yy][xx].mine) grid[yy][xx].revealed = true;
            }
        }
        over = true;
        won = true;
        score = revealed_count * 10 + 500;
        score_submit("mines", score);
        buzzer_tone(880, 60);
        buzzer_tone(1175, 60);
    }
}

static void toggle_flag(int x, int y)
{
    cell_t *c = &grid[y][x];
    if (c->revealed) return;
    c->flagged = !c->flagged;
    flags += c->flagged ? 1 : -1;
    buzzer_tone(500, 20);
}

static void render(void)
{
    display_clear();

    /* HUD */
    char buf[24];
    snprintf(buf, sizeof(buf), "MINES %lu", (unsigned long)(MINES - flags));
    display_draw_text(2, 0, buf, true);
    snprintf(buf, sizeof(buf), "OPEN %lu", (unsigned long)revealed_count);
    display_draw_text(76, 0, buf, true);

    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            int px = OX + x * CW;
            int py = OY + y * CH;
            cell_t *c = &grid[y][x];

            if (c->revealed) {
                if (c->mine) {
                    display_draw_rect(px, py, CW, CH, true, true);
                    if (!(x == cur_x && y == cur_y)) {
                        display_draw_rect(px + 4, py + 2, 2, 2, true, false);
                        display_draw_rect(px + 6, py + 4, 2, 2, true, false);
                    }
                } else {
                    display_draw_rect(px, py, CW, CH, false, true);
                    if (c->adj > 0) {
                        char d = (char)('0' + c->adj);
                        display_draw_char(px + 4, py + 1, d, true);
                    } else {
                        display_draw_rect(px + 5, py + 3, 2, 2, true, true);
                    }
                }
            } else {
                display_draw_rect(px, py, CW, CH, true, true);
                display_draw_rect(px + 1, py + 1, CW - 2, CH - 2, true, false);
                if (c->flagged) {
                    /* small flag */
                    display_draw_rect(px + 3, py + 1, 1, CH - 2, true, true);
                    display_draw_rect(px + 3, py + 1, 4, 1, true, true);
                    display_draw_rect(px + 6, py + 2, 1, 1, true, true);
                }
            }

            /* cursor highlight */
            if (x == cur_x && y == cur_y) {
                display_draw_rect(px - 1, py - 1, CW + 2, CH + 2, false, true);
                display_draw_rect(px - 2, py - 2, CW + 4, CH + 4, false, false);
            }
        }
    }
}

static void render_gameover(void)
{
    display_clear();
    display_draw_rect(0, 0, DISPLAY_WIDTH, 12, true, true);
    display_draw_text_centered(3, won ? "YOU WIN!" : "BOOM!", false);

    char buf[24];
    snprintf(buf, sizeof(buf), "Score: %lu", (unsigned long)score);
    display_draw_text_centered(22, buf, true);
    snprintf(buf, sizeof(buf), "High: %lu", (unsigned long)score_get("mines"));
    display_draw_text_centered(32, buf, true);

    display_draw_text_centered(46, "OK:Retry  BACK:Menu", true);
}

void game_mines_run(void)
{
    mines_reset();
    bool show_gameover = false;

    while (1) {
        input_event_t ev;
        while (input_get_event(&ev, 0)) {
            if (ev.type != INPUT_EVENT_PRESS) continue;
            switch (ev.key) {
            case INPUT_KEY_UP:
                if (!over) { cur_y--; if (cur_y < 0) cur_y = 0; beep_move(); }
                break;
            case INPUT_KEY_DOWN:
                if (!over) { cur_y++; if (cur_y >= ROWS) cur_y = ROWS - 1; beep_move(); }
                break;
            case INPUT_KEY_LEFT:
                if (!over) { cur_x--; if (cur_x < 0) cur_x = 0; beep_move(); }
                break;
            case INPUT_KEY_RIGHT:
                if (!over) { cur_x++; if (cur_x >= COLS) cur_x = COLS - 1; beep_move(); }
                break;
            case INPUT_KEY_OK:
                if (!over) {
                    reveal_cell(cur_x, cur_y);
                    if (over) show_gameover = true;
                } else if (show_gameover) {
                    mines_reset();
                    show_gameover = false;
                }
                break;
            case INPUT_KEY_SHIFT:
                if (!over) toggle_flag(cur_x, cur_y);
                break;
            case INPUT_KEY_BACK:
                return;
            default:
                break;
            }
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
