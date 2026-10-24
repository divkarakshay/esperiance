#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buzzer.h"
#include "display.h"
#include "games.h"
#include "input.h"

#define BW 8
#define BH 6
#define CELL_W (DISPLAY_WIDTH / BW)    /* 16 */
#define CELL_H (DISPLAY_HEIGHT / BH)   /* 10 */

#define EMPTY 0
#define PLAYER 1
#define AI 2

static int8_t board[BW][BH];
static int cursor_x, cursor_y;
static bool over;
static int p_discs, a_discs;

static const int DIRS[8][2] = {
    { -1, -1 }, { 0, -1 }, { 1, -1 },
    { -1, 0 },             { 1, 0 },
    { -1, 1 },  { 0, 1 },  { 1, 1 },
};

static void bantumi_reset(void)
{
    memset(board, EMPTY, sizeof(board));
    board[3][2] = PLAYER;
    board[4][3] = PLAYER;
    board[3][3] = AI;
    board[4][2] = AI;
    cursor_x = 3;
    cursor_y = 2;
    over = false;
    p_discs = a_discs = 0;
}

/* Number of discs flipped by placing `who` at (x,y). 0 = invalid move. */
static int flips_for(int8_t b[BW][BH], int x, int y, int who)
{
    if (b[x][y] != EMPTY) return 0;
    int total = 0;
    for (int d = 0; d < 8; d++) {
        int dx = DIRS[d][0], dy = DIRS[d][1];
        int cx = x + dx, cy = y + dy;
        int n = 0;
        while (cx >= 0 && cx < BW && cy >= 0 && cy < BH &&
               b[cx][cy] == (int8_t)(who == PLAYER ? AI : PLAYER)) {
            cx += dx;
            cy += dy;
            n++;
        }
        if (n > 0 && cx >= 0 && cx < BW && cy >= 0 && cy < BH &&
            b[cx][cy] == who) {
            total += n;
        }
    }
    return total;
}

static void place(int8_t b[BW][BH], int x, int y, int who)
{
    b[x][y] = (int8_t)who;
    for (int d = 0; d < 8; d++) {
        int dx = DIRS[d][0], dy = DIRS[d][1];
        int cx = x + dx, cy = y + dy;
        int n = 0;
        while (cx >= 0 && cx < BW && cy >= 0 && cy < BH &&
               b[cx][cy] == (int8_t)(who == PLAYER ? AI : PLAYER)) {
            cx += dx;
            cy += dy;
            n++;
        }
        if (n > 0 && cx >= 0 && cx < BW && cy >= 0 && cy < BH &&
            b[cx][cy] == who) {
            cx = x + dx;
            cy = y + dy;
            while (cx >= 0 && cx < BW && cy >= 0 && cy < BH &&
                   b[cx][cy] == (int8_t)(who == PLAYER ? AI : PLAYER)) {
                b[cx][cy] = (int8_t)who;
                cx += dx;
                cy += dy;
            }
        }
    }
}

static bool has_move(int8_t b[BW][BH], int who)
{
    for (int x = 0; x < BW; x++) {
        for (int y = 0; y < BH; y++) {
            if (flips_for(b, x, y, who) > 0) return true;
        }
    }
    return false;
}

static int count_discs(int8_t b[BW][BH], int who)
{
    int n = 0;
    for (int x = 0; x < BW; x++) {
        for (int y = 0; y < BH; y++) {
            if (b[x][y] == who) n++;
        }
    }
    return n;
}

/* ---------- AI: alpha-beta minimax ---------- */

#define DEPTH 4

static int eval_board(int8_t b[BW][BH])
{
    int s = 0;
    for (int x = 0; x < BW; x++) {
        for (int y = 0; y < BH; y++) {
            if (b[x][y] == AI) {
                s += 1;
                if ((x == 0 || x == BW - 1) && (y == 0 || y == BH - 1)) {
                    s += 3;
                }
            } else if (b[x][y] == PLAYER) {
                s -= 1;
                if ((x == 0 || x == BW - 1) && (y == 0 || y == BH - 1)) {
                    s -= 3;
                }
            }
        }
    }
    /* mobility bonus */
    if (has_move(b, AI)) s += 2;
    if (!has_move(b, PLAYER)) s += 4;
    return s;
}

static int minimax(int8_t b[BW][BH], int depth, int alpha, int beta,
                   int who)
{
    if (depth == 0) {
        return eval_board(b);
    }
    int best = (who == AI) ? -100000 : 100000;

    /* iterate cells row-major; alpha-beta keeps this fast enough */
    for (int x = 0; x < BW; x++) {
        for (int y = 0; y < BH; y++) {
        if (flips_for(b, x, y, who) == 0) continue;

        int8_t copy[BW][BH];
        memcpy(copy, b, sizeof(copy));
        place(b, x, y, who);
        int val;
        if (who == AI) {
            val = minimax(b, depth - 1, alpha, beta, PLAYER);
            if (val > best) best = val;
            if (val > alpha) alpha = val;
        } else {
            val = minimax(b, depth - 1, alpha, beta, AI);
            if (val < best) best = val;
            if (val < beta) beta = val;
        }
        memcpy(b, copy, sizeof(copy));   /* undo */

        if (alpha >= beta) return best;
        }
    }

    if (best == 100000 || best == -100000) {
        /* no moves: pass */
        return eval_board(b);
    }
    return best;
}

static bool ai_move(void)
{
    int best_x = -1, best_y = -1;
    int best_val = -100000;

    for (int x = 0; x < BW; x++) {
        for (int y = 0; y < BH; y++) {
            if (flips_for(board, x, y, AI) == 0) continue;
            int8_t copy[BW][BH];
            memcpy(copy, board, sizeof(copy));
            place(board, x, y, AI);
            int val = minimax(board, DEPTH - 1, -100000, 100000, PLAYER);
            memcpy(board, copy, sizeof(copy));
            if (val > best_val) {
                best_val = val;
                best_x = x;
                best_y = y;
            }
        }
    }

    if (best_x < 0) return false;
    place(board, best_x, best_y, AI);
    return true;
}

static void finish_check(void)
{
    if (count_discs(board, EMPTY) == 0 ||
        (!has_move(board, PLAYER) && !has_move(board, AI))) {
        over = true;
        p_discs = count_discs(board, PLAYER);
        a_discs = count_discs(board, AI);
        score_submit("bantumi", (uint32_t)p_discs);
    }
}

static void render(void)
{
    display_clear();

    /* grid */
    for (int x = 1; x < BW; x++) {
        display_draw_line(x * CELL_W, 0, x * CELL_W, DISPLAY_HEIGHT, true);
    }
    for (int y = 1; y < BH; y++) {
        display_draw_line(0, y * CELL_H, DISPLAY_WIDTH, y * CELL_H, true);
    }

    /* discs */
    for (int x = 0; x < BW; x++) {
        for (int y = 0; y < BH; y++) {
            if (board[x][y] == EMPTY) continue;
            int cx = x * CELL_W + CELL_W / 2;
            int cy = y * CELL_H + CELL_H / 2;
            if (board[x][y] == PLAYER) {
                for (int r = 4; r >= 1; r--) {
                    display_draw_circle(cx, cy, r, true);
                }
            } else {
                display_draw_circle(cx, cy, 4, true);
            }
        }
    }

    /* cursor */
    if (!over) {
        int cx = cursor_x * CELL_W;
        int cy = cursor_y * CELL_H;
        display_draw_rect(cx, cy, CELL_W, CELL_H, false, true);
        display_draw_rect(cx + 1, cy + 1, CELL_W - 2, CELL_H - 2, false, true);
    }

    /* HUD */
    char buf[24];
    snprintf(buf, sizeof(buf), "YOU %d", count_discs(board, PLAYER));
    display_draw_text(2, 1, buf, true);
    snprintf(buf, sizeof(buf), "AI %d", count_discs(board, AI));
    display_draw_text(90, 1, buf, true);
    display_draw_text(2, 55, "OK:Place", true);
    display_draw_text(90, 55, "BACK:Exit", true);
}

static void render_win(void)
{
    display_clear();
    display_draw_rect(0, 0, DISPLAY_WIDTH, 12, true, true);
    if (p_discs == a_discs) {
        display_draw_text_centered(3, "DRAW", false);
    } else {
        display_draw_text_centered(3, p_discs > a_discs ? "YOU WIN!" : "AI WINS",
                                   false);
    }

    char buf[24];
    snprintf(buf, sizeof(buf), "You %d - %d AI", p_discs, a_discs);
    display_draw_text_centered(22, buf, true);
    snprintf(buf, sizeof(buf), "High: %lu", (unsigned long)score_get("bantumi"));
    display_draw_text_centered(32, buf, true);
    display_draw_text_centered(46, "OK:Retry  BACK:Menu", true);
}

void game_bantumi_run(void)
{
    bantumi_reset();
    bool player_turn = true;
    bool showing_pass = false;
    uint32_t pass_until = 0;
    uint32_t think_until = 0;
    bool thinking = false;

    while (1) {
        input_event_t ev;
        while (input_get_event(&ev, 0)) {
            if (ev.type != INPUT_EVENT_PRESS) continue;
            switch (ev.key) {
            case INPUT_KEY_UP:
                if (cursor_y > 0) cursor_y--;
                buzzer_tone(1100, 12);
                break;
            case INPUT_KEY_DOWN:
                if (cursor_y < BH - 1) cursor_y++;
                buzzer_tone(1100, 12);
                break;
            case INPUT_KEY_LEFT:
                if (cursor_x > 0) cursor_x--;
                buzzer_tone(1100, 12);
                break;
            case INPUT_KEY_RIGHT:
                if (cursor_x < BW - 1) cursor_x++;
                buzzer_tone(1100, 12);
                break;
            case INPUT_KEY_OK:
                if (over) {
                    bantumi_reset();
                    player_turn = true;
                    thinking = false;
                } else if (player_turn && !thinking &&
                           flips_for(board, cursor_x, cursor_y, PLAYER) > 0) {
                    place(board, cursor_x, cursor_y, PLAYER);
                    buzzer_tone(1400, 30);
                    player_turn = false;
                    thinking = true;
                    think_until = esp_timer_get_time() + 350000;
                }
                break;
            case INPUT_KEY_BACK:
                return;
            default:
                break;
            }
        }

        if (over) {
            render_win();
            display_show();
            vTaskDelay(pdMS_TO_TICKS(16));
            continue;
        }

        /* AI thinking */
        if (thinking && esp_timer_get_time() >= think_until) {
            thinking = false;
            if (!ai_move()) {
                showing_pass = true;
                pass_until = esp_timer_get_time() + 800000;
            }
            player_turn = true;
            if (!has_move(board, PLAYER)) {
                showing_pass = true;
                pass_until = esp_timer_get_time() + 800000;
                player_turn = false;
                thinking = true;
                think_until = esp_timer_get_time() + 300000;
            }
            finish_check();
        }
        if (showing_pass && esp_timer_get_time() >= pass_until) {
            showing_pass = false;
        }

        render();
        if (thinking) {
            display_draw_text_centered(40, "AI thinking...", true);
        }
        if (showing_pass) {
            display_draw_text_centered(40, "No move - pass", true);
        }
        display_show();

        vTaskDelay(pdMS_TO_TICKS(16));
    }
}