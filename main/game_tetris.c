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

#define BOARD_W    10
#define BOARD_H    16
#define CELL       4
#define HUD_X      44

#define GRAVITY0   700          /* ms per row at level 1 */

typedef struct {
    int type;
    int rot;
    int x, y;
} piece_t;

static const int8_t shapes[7][4][4] = {
    { { 0, 0, 0, 0 }, { 1, 1, 1, 1 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } }, /* I */
    { { 0, 0, 0, 0 }, { 0, 1, 1, 0 }, { 0, 1, 1, 0 }, { 0, 0, 0, 0 } }, /* O */
    { { 0, 0, 0, 0 }, { 0, 1, 0, 0 }, { 1, 1, 1, 0 }, { 0, 0, 0, 0 } }, /* T */
    { { 0, 0, 0, 0 }, { 0, 1, 1, 0 }, { 1, 1, 0, 0 }, { 0, 0, 0, 0 } }, /* S */
    { { 0, 0, 0, 0 }, { 1, 1, 0, 0 }, { 0, 1, 1, 0 }, { 0, 0, 0, 0 } }, /* Z */
    { { 0, 0, 0, 0 }, { 1, 0, 0, 0 }, { 1, 1, 1, 0 }, { 0, 0, 0, 0 } }, /* J */
    { { 0, 0, 0, 0 }, { 0, 0, 1, 0 }, { 1, 1, 1, 0 }, { 0, 0, 0, 0 } }, /* L */
};

static uint8_t board[BOARD_H][BOARD_W];
static piece_t cur;
static int next_type;
static uint32_t score;
static int lines;
static int level;
static bool over;
static uint32_t fall_acc_ms;

static int piece_cell(int type, int rot, int r, int c)
{
    int sr = r, sc = c;
    for (int k = 0; k < rot; k++) {
        int nr = sc;
        int nc = 3 - sr;
        sr = nr;
        sc = nc;
    }
    return shapes[type][sr][sc];
}

static bool fits(const piece_t *p)
{
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (!piece_cell(p->type, p->rot, r, c)) continue;
            int bx = p->x + c;
            int by = p->y + r;
            if (bx < 0 || bx >= BOARD_W) return false;
            if (by >= BOARD_H) return false;
            if (by >= 0 && board[by][bx]) return false;
        }
    }
    return true;
}

static void spawn_piece(void)
{
    cur.type = next_type;
    cur.rot = 0;
    cur.x = 3;
    cur.y = 0;
    next_type = (int)(esp_random() % 7);
    if (!fits(&cur)) {
        over = true;
        score_submit("tetris", score);
        buzzer_tone(200, 200);
    }
}

static void tetris_reset(void)
{
    memset(board, 0, sizeof(board));
    score = 0;
    lines = 0;
    level = 1;
    over = false;
    fall_acc_ms = 0;
    next_type = (int)(esp_random() % 7);
    spawn_piece();
}

static void lock_piece(void)
{
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (!piece_cell(cur.type, cur.rot, r, c)) continue;
            int bx = cur.x + c;
            int by = cur.y + r;
            if (by < 0) continue;
            board[by][bx] = 1;
        }
    }

    /* clear full rows */
    int cleared = 0;
    for (int y = BOARD_H - 1; y >= 0; y--) {
        bool full = true;
        for (int x = 0; x < BOARD_W; x++) {
            if (!board[y][x]) { full = false; break; }
        }
        if (full) {
            memmove(&board[1], &board[0], y * sizeof(board[0]));
            memset(&board[0], 0, sizeof(board[0]));
            y++;
            cleared++;
        }
    }

    if (cleared > 0) {
        static const uint32_t points[5] = { 0, 100, 300, 500, 800 };
        score += points[cleared] * (uint32_t)level;
        lines += cleared;
        int new_level = lines / 10 + 1;
        if (new_level > level) {
            level = new_level;
            buzzer_tone(660, 60);
            buzzer_tone(880, 60);
        } else {
            buzzer_tone(600, 40);
        }
    }

    spawn_piece();
}

static void hard_drop(void)
{
    while (1) {
        piece_t n = cur;
        n.y++;
        if (!fits(&n)) break;
        cur = n;
    }
    lock_piece();
}

static void try_move(int dx)
{
    piece_t n = cur;
    n.x += dx;
    if (fits(&n)) cur = n;
}

static void try_rotate(void)
{
    static const int kicks[5] = { 0, -1, 1, -2, 2 };
    piece_t n = cur;
    n.rot = (n.rot + 1) % 4;
    for (int i = 0; i < 5; i++) {
        n.x = cur.x + kicks[i];
        if (fits(&n)) {
            cur = n;
            return;
        }
    }
}

static int gravity_ms(void)
{
    int g = GRAVITY0 - (level - 1) * 60;
    if (g < 120) g = 120;
    if (input_is_pressed(INPUT_KEY_DOWN)) g = 50;
    return g;
}

static void render_piece_preview(int type, int ox, int oy, int size)
{
    int minr = 4, maxr = -1, minc = 4, maxc = -1;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (!piece_cell(type, 0, r, c)) continue;
            if (r < minr) minr = r;
            if (r > maxr) maxr = r;
            if (c < minc) minc = c;
            if (c > maxc) maxc = c;
        }
    }
    int pw = (maxc - minc + 1) * size;
    int ph = (maxr - minr + 1) * size;
    int px = ox + (16 - pw) / 2;
    int py = oy + (16 - ph) / 2;
    for (int r = minr; r <= maxr; r++) {
        for (int c = minc; c <= maxc; c++) {
            if (piece_cell(type, 0, r, c)) {
                display_draw_rect(px + (c - minc) * size,
                                  py + (r - minr) * size, size, size,
                                  true, true);
            }
        }
    }
}

static void render(void)
{
    display_clear();

    /* board walls */
    display_draw_rect(0, 0, 1, DISPLAY_HEIGHT, true, true);
    display_draw_rect(BOARD_W * CELL, 0, 1, DISPLAY_HEIGHT, true, true);
    display_draw_rect(0, DISPLAY_HEIGHT - 1, BOARD_W * CELL, 1, true, true);

    /* locked cells */
    for (int y = 0; y < BOARD_H; y++) {
        for (int x = 0; x < BOARD_W; x++) {
            if (board[y][x]) {
                display_draw_rect(x * CELL, y * CELL, CELL, CELL, true, true);
            }
        }
    }

    /* active piece */
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (!piece_cell(cur.type, cur.rot, r, c)) continue;
            int px = (cur.x + c) * CELL;
            int py = (cur.y + r) * CELL;
            if (py < 0) continue;
            display_draw_rect(px, py, CELL, CELL, true, true);
            display_draw_rect(px + 1, py + 1, CELL - 2, CELL - 2, true, false);
        }
    }

    /* HUD */
    display_draw_text(HUD_X, 2, "NEXT", true);
    render_piece_preview(next_type, HUD_X, 12, CELL);

    display_draw_text(HUD_X, 31, "SC", true);
    char buf[24];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)score);
    display_draw_text(HUD_X, 38, buf, true);

    display_draw_text(HUD_X, 47, "LV", true);
    snprintf(buf, sizeof(buf), "%d", level);
    display_draw_text(HUD_X, 54, buf, true);
}

static void render_gameover(void)
{
    display_clear();
    display_draw_rect(0, 0, DISPLAY_WIDTH, 12, true, true);
    display_draw_text_centered(3, "GAME OVER", false);

    char buf[24];
    snprintf(buf, sizeof(buf), "Score: %lu", (unsigned long)score);
    display_draw_text_centered(22, buf, true);
    snprintf(buf, sizeof(buf), "High: %lu", (unsigned long)score_get("tetris"));
    display_draw_text_centered(32, buf, true);

    display_draw_text_centered(46, "OK:Retry  BACK:Menu", true);
}

void game_tetris_run(void)
{
    tetris_reset();
    uint32_t last = esp_timer_get_time();
    uint32_t move_cooldown = 0;

    while (1) {
        input_event_t ev;
        while (input_get_event(&ev, 0)) {
            if (ev.type != INPUT_EVENT_PRESS) continue;
            switch (ev.key) {
            case INPUT_KEY_UP:
            case INPUT_KEY_SHIFT:
                if (!over) try_rotate();
                break;
            case INPUT_KEY_OK:
                if (over) {
                    tetris_reset();
                    last = esp_timer_get_time();
                } else {
                    hard_drop();
                }
                break;
            case INPUT_KEY_BACK:
                return;
            default:
                break;
            }
        }

        uint32_t now = esp_timer_get_time();
        uint32_t dt_ms = (uint32_t)((now - last) / 1000);
        if (dt_ms > 50) dt_ms = 50;
        last = now;

        if (!over) {
            /* lateral movement with auto-repeat */
            bool left = input_is_pressed(INPUT_KEY_LEFT);
            bool right = input_is_pressed(INPUT_KEY_RIGHT);
            if (left != right) {
                if (move_cooldown == 0) {
                    try_move(left ? -1 : 1);
                    move_cooldown = 170;
                } else if (move_cooldown > dt_ms) {
                    move_cooldown -= dt_ms;
                } else {
                    move_cooldown = 0;
                }
            } else {
                move_cooldown = 0;
            }

            /* gravity */
            fall_acc_ms += dt_ms;
            while (fall_acc_ms >= (uint32_t)gravity_ms()) {
                fall_acc_ms -= (uint32_t)gravity_ms();
                piece_t n = cur;
                n.y++;
                if (!fits(&n)) {
                    lock_piece();
                    fall_acc_ms = 0;
                    break;
                }
                cur = n;
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
