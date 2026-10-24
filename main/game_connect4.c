#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buzzer.h"
#include "display.h"
#include "games.h"
#include "input.h"

#define CW 7
#define CH 6
#define COLS_X 16                  /* 128 / 7, integer cell width */
#define ROWS_Y 9                   /* playfield 6 rows * 9 + top area */

#define EMPTY 0
#define PLAYER 1
#define AI 2

static int8_t b[CW][CH];           /* row 0 = bottom */
static int cursor_col;
static bool player_turn;
static bool over;
static bool player_won;
static bool thinking;
static uint32_t think_until;
static int wins;                   /* session wins */

static void c4_reset(void)
{
    memset(b, EMPTY, sizeof(b));
    cursor_col = 3;
    player_turn = true;
    over = false;
    thinking = false;
}

/* ---------- board helpers (board passed by pointer for minimax) ---------- */

static bool col_full(int8_t brd[CW][CH], int col)
{
    return brd[col][CH - 1] != EMPTY;
}

static void drop(int8_t brd[CW][CH], int col, int who)
{
    int row = 0;
    while (row < CH && brd[col][row] != EMPTY) row++;
    if (row < CH) brd[col][row] = (int8_t)who;
}

static void undrop(int8_t brd[CW][CH], int col)
{
    for (int row = CH - 1; row >= 0; row--) {
        if (brd[col][row] != EMPTY) {
            brd[col][row] = EMPTY;
            return;
        }
    }
}

static int count_in_window(int8_t brd[CW][CH], int x, int y, int dx, int dy,
                           int who)
{
    int n = 0;
    for (int i = 0; i < 4; i++) {
        if (brd[x + i * dx][y + i * dy] == who) n++;
    }
    return n;
}

static bool check_win_at(int8_t brd[CW][CH], int col)
{
    int row = 0;
    while (row < CH && brd[col][row] != EMPTY) row++;
    row--;                              /* topmost disc = last placed */
    if (row < 0) return false;
    int who = brd[col][row];
    static const int dirs[4][2] = {
        { 1, 0 }, { 0, 1 }, { 1, 1 }, { 1, -1 },
    };
    for (int d = 0; d < 4; d++) {
        int dx = dirs[d][0], dy = dirs[d][1];
        int n = 1;
        for (int s = 1; s < 4; s++) {
            int x = col + dx * s, y = row + dy * s;
            if (x < 0 || x >= CW || y < 0 || y >= CH) break;
            if (brd[x][y] != who) break;
            n++;
        }
        for (int s = 1; s < 4; s++) {
            int x = col - dx * s, y = row - dy * s;
            if (x < 0 || x >= CW || y < 0 || y >= CH) break;
            if (brd[x][y] != who) break;
            n++;
        }
        if (n >= 4) return true;
    }
    return false;
}

static bool board_full(int8_t brd[CW][CH])
{
    for (int c = 0; c < CW; c++) {
        if (!col_full(brd, c)) return false;
    }
    return true;
}

/* ---------- AI: alpha-beta minimax ---------- */

#define DEPTH 6

static int eval_board(int8_t brd[CW][CH])
{
    int score = 0;
    /* evaluate all 4-in-a-row windows */
    for (int x = 0; x < CW; x++) {
        for (int y = 0; y < CH; y++) {
            static const int dirs[4][2] = {
                { 1, 0 }, { 0, 1 }, { 1, 1 }, { 1, -1 },
            };
            for (int d = 0; d < 4; d++) {
                int dx = dirs[d][0], dy = dirs[d][1];
                if (x + 3 * dx >= CW || x + 3 * dx < 0 ||
                    y + 3 * dy >= CH || y + 3 * dy < 0) {
                    continue;
                }
                int p = count_in_window(brd, x, y, dx, dy, PLAYER);
                int a = count_in_window(brd, x, y, dx, dy, AI);
                if (p > 0 && a == 0) score -= 1 << p;
                if (a > 0 && p == 0) score += 1 << a;
            }
        }
    }
    /* center column preference */
    for (int y = 0; y < CH; y++) {
        if (brd[3][y] == AI) score += 1;
        if (brd[3][y] == PLAYER) score -= 1;
    }
    return score;
}

static int minimax(int8_t brd[CW][CH], int depth, int alpha, int beta,
                   int who)
{
    if (depth == 0) {
        return eval_board(brd);
    }

    int best = (who == AI) ? -100000 : 100000;
    int order[CW] = { 3, 2, 4, 1, 5, 0, 6 };   /* center first */

    for (int i = 0; i < CW; i++) {
        int col = order[i];
        if (col_full(brd, col)) continue;

        drop(brd, col, who);
        int val;
        if (who == AI) {
            val = minimax(brd, depth - 1, alpha, beta, PLAYER);
            if (val > best) best = val;
            if (val > alpha) alpha = val;
        } else {
            val = minimax(brd, depth - 1, alpha, beta, AI);
            if (val < best) best = val;
            if (val < beta) beta = val;
        }
        undrop(brd, col);

        if (alpha >= beta) break;
    }

    if (best == 100000 || best == -100000) {
        return eval_board(brd);   /* no legal moves */
    }
    return best;
}

static int ai_move(void)          /* returns column played, or -1 */
{
    int best_col = -1;
    int best_val = -100000;
    int order[CW] = { 3, 2, 4, 1, 5, 0, 6 };

    for (int i = 0; i < CW; i++) {
        int col = order[i];
        if (col_full(b, col)) continue;

        drop(b, col, AI);
        if (check_win_at(b, col)) {
            best_col = col;       /* immediate win */
            undrop(b, col);
            break;
        }
        int val = minimax(b, DEPTH - 1, -100000, 100000, PLAYER);
        undrop(b, col);

        if (val > best_val) {
            best_val = val;
            best_col = col;
        }
    }

    if (best_col < 0) return -1;
    drop(b, best_col, AI);
    return best_col;
}

/* ---------- rendering ---------- */

static void render_disc(int col, int row, int who)
{
    int cx = col * COLS_X + COLS_X / 2;
    int cy = 3 + (CH - 1 - row) * ROWS_Y + ROWS_Y / 2;
    if (who == PLAYER) {
        for (int r = 4; r >= 1; r--) {
            display_draw_circle(cx, cy, r, true);
        }
    } else {
        display_draw_circle(cx, cy, 4, true);
    }
}

static void render(void)
{
    display_clear();

    /* column selector */
    int ax = cursor_col * COLS_X + COLS_X / 2 - 2;
    display_draw_rect(ax, 0, 4, 3, true, true);

    /* board */
    for (int col = 0; col < CW; col++) {
        for (int row = 0; row < CH; row++) {
            int x = col * COLS_X;
            int y = 3 + (CH - 1 - row) * ROWS_Y;
            display_draw_rect(x, y, COLS_X, ROWS_Y, false, true);
        }
    }
    for (int col = 0; col < CW; col++) {
        for (int row = 0; row < CH; row++) {
            if (b[col][row] != EMPTY) {
                render_disc(col, row, b[col][row]);
            }
        }
    }

    /* HUD */
    char buf[24];
    snprintf(buf, sizeof(buf), "W:%d", wins);
    display_draw_text(2, 58, buf, true);
    display_draw_text(100, 58, "BACK:Exit", true);
}

static void render_result(void)
{
    display_clear();
    display_draw_rect(0, 0, DISPLAY_WIDTH, 12, true, true);
    if (!player_won && !board_full(b)) {
        display_draw_text_centered(3, "YOU LOSE", false);
    } else if (!player_won) {
        display_draw_text_centered(3, "DRAW", false);
    } else {
        display_draw_text_centered(3, "YOU WIN!", false);
    }

    char buf[24];
    snprintf(buf, sizeof(buf), "Wins: %d", wins);
    display_draw_text_centered(22, buf, true);
    snprintf(buf, sizeof(buf), "Best: %lu", (unsigned long)score_get("connect4"));
    display_draw_text_centered(32, buf, true);
    display_draw_text_centered(46, "OK:Retry  BACK:Menu", true);
}

void game_c4_run(void)
{
    c4_reset();
    wins = 0;

    while (1) {
        input_event_t ev;
        while (input_get_event(&ev, 0)) {
            if (ev.type != INPUT_EVENT_PRESS || thinking) continue;
            switch (ev.key) {
            case INPUT_KEY_LEFT:
                cursor_col = (cursor_col + CW - 1) % CW;
                buzzer_tone(1100, 12);
                break;
            case INPUT_KEY_RIGHT:
                cursor_col = (cursor_col + 1) % CW;
                buzzer_tone(1100, 12);
                break;
            case INPUT_KEY_UP:
            case INPUT_KEY_OK:
                if (over) {
                    c4_reset();
                } else if (player_turn && !col_full(b, cursor_col)) {
                    drop(b, cursor_col, PLAYER);
                    buzzer_tone(1400, 30);
                    if (check_win_at(b, cursor_col)) {
                        over = true;
                        player_won = true;
                        wins++;
                        score_submit("connect4", (uint32_t)wins);
                        buzzer_tone(880, 70);
                        buzzer_tone(1175, 70);
                        buzzer_tone(1568, 90);
                    } else if (board_full(b)) {
                        over = true;
                        player_won = false;
                    } else {
                        player_turn = false;
                        thinking = true;
                        think_until = esp_timer_get_time() + 300000;
                    }
                }
                break;
            case INPUT_KEY_BACK:
                return;
            default:
                break;
            }
        }

        /* AI move */
        if (thinking && !over && esp_timer_get_time() >= think_until) {
            thinking = false;
            int c = ai_move();
            if (c < 0) {
                over = true;
                player_won = false;   /* AI has no moves */
            } else if (check_win_at(b, c)) {
                over = true;
                player_won = false;
                buzzer_tone(300, 150);
            }
            if (!over) {
                if (board_full(b)) {
                    over = true;
                    player_won = false;
                } else {
                    player_turn = true;
                }
            }
        }

        if (over) {
            render_result();
        } else {
            render();
            if (thinking) {
                display_draw_text_centered(40, "AI thinking...", true);
            }
        }
        display_show();

        vTaskDelay(pdMS_TO_TICKS(16));
    }
}