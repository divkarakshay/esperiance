#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buzzer.h"
#include "display.h"
#include "games.h"
#include "input.h"

#define CELL_W      36
#define CELL_H      36
#define OX          (DISPLAY_WIDTH / 2 - (3 * CELL_W) / 2)
#define OY          14

#define EMPTY 0
#define X_MARK 1
#define O_MARK 2

static int8_t board[3][3];
static int cur_x, cur_y;
static int streak;
static uint32_t best_streak;
static int round_result;        /* 0 none, 1 player win, 2 ai win, 3 draw */
static bool over;

static const char *result_text(void)
{
    switch (round_result) {
    case 1: return "YOU WIN!";
    case 2: return "YOU LOSE";
    case 3: return "DRAW";
    default: return "";
    }
}

static void reset_round(void)
{
    memset(board, 0, sizeof(board));
    cur_x = 1;
    cur_y = 1;
    round_result = 0;
    over = false;
}

static void ttto_reset(void)
{
    streak = 0;
    best_streak = score_get("tictactoe");
    reset_round();
}

static int winner(void)
{
    /* rows and columns */
    for (int i = 0; i < 3; i++) {
        if (board[i][0] != EMPTY && board[i][0] == board[i][1] &&
            board[i][1] == board[i][2]) return board[i][0];
        if (board[0][i] != EMPTY && board[0][i] == board[1][i] &&
            board[1][i] == board[2][i]) return board[0][i];
    }
    if (board[0][0] != EMPTY && board[0][0] == board[1][1] &&
        board[1][1] == board[2][2]) return board[0][0];
    if (board[0][2] != EMPTY && board[0][2] == board[1][1] &&
        board[1][1] == board[2][0]) return board[0][2];
    return EMPTY;
}

static bool board_full(void)
{
    for (int y = 0; y < 3; y++) {
        for (int x = 0; x < 3; x++) {
            if (board[y][x] == EMPTY) return false;
        }
    }
    return true;
}

/* minimax: returns score from the AI's (O) point of view */
static int minimax(int depth, bool ai_turn)
{
    int w = winner();
    if (w == O_MARK) return 10 - depth;
    if (w == X_MARK) return depth - 10;
    if (board_full()) return 0;

    int best = ai_turn ? -100 : 100;
    for (int y = 0; y < 3; y++) {
        for (int x = 0; x < 3; x++) {
            if (board[y][x] != EMPTY) continue;
            board[y][x] = ai_turn ? O_MARK : X_MARK;
            int s = minimax(depth + 1, !ai_turn);
            board[y][x] = EMPTY;
            if (ai_turn) {
                if (s > best) best = s;
            } else {
                if (s < best) best = s;
            }
        }
    }
    return best;
}

static void ai_move(void)
{
    int best_x = -1, best_y = -1, best = -100;
    for (int y = 0; y < 3; y++) {
        for (int x = 0; x < 3; x++) {
            if (board[y][x] != EMPTY) continue;
            board[y][x] = O_MARK;
            int s = minimax(1, false);
            board[y][x] = EMPTY;
            if (s > best) {
                best = s;
                best_x = x;
                best_y = y;
            }
        }
    }
    board[best_y][best_x] = O_MARK;

    int w = winner();
    if (w == O_MARK) {
        round_result = 2;
        over = true;
        streak = 0;
        buzzer_tone(200, 150);
    } else if (board_full()) {
        round_result = 3;
        over = true;
        streak = 0;
        buzzer_tone(500, 60);
    }
}

static void player_move(void)
{
    board[cur_y][cur_x] = X_MARK;
    buzzer_tone(1200, 20);

    int w = winner();
    if (w == X_MARK) {
        round_result = 1;
        over = true;
        streak++;
        if ((uint32_t)streak > best_streak) {
            best_streak = (uint32_t)streak;
            score_submit("tictactoe", best_streak);
        }
        buzzer_tone(880, 60);
        buzzer_tone(1175, 60);
        return;
    }
    if (board_full()) {
        round_result = 3;
        over = true;
        streak = 0;
        buzzer_tone(500, 60);
        return;
    }
    ai_move();
}

static void render(void)
{
    display_clear();

    /* HUD */
    char buf[24];
    snprintf(buf, sizeof(buf), "Streak %d", streak);
    display_draw_text(2, 1, buf, true);
    snprintf(buf, sizeof(buf), "Best %lu", (unsigned long)best_streak);
    display_draw_text(76, 1, buf, true);

    /* grid lines */
    display_draw_line(OX + CELL_W, OY, OX + CELL_W, OY + 3 * CELL_H, true);
    display_draw_line(OX + 2 * CELL_W, OY, OX + 2 * CELL_W, OY + 3 * CELL_H, true);
    display_draw_line(OX, OY + CELL_H, OX + 3 * CELL_W, OY + CELL_H, true);
    display_draw_line(OX, OY + 2 * CELL_H, OX + 3 * CELL_W, OY + 2 * CELL_H, true);

    for (int y = 0; y < 3; y++) {
        for (int x = 0; x < 3; x++) {
            int cx = OX + x * CELL_W;
            int cy = OY + y * CELL_H;
            if (board[y][x] == X_MARK) {
                display_draw_line(cx + 4, cy + 4, cx + CELL_W - 4,
                                  cy + CELL_H - 4, true);
                display_draw_line(cx + CELL_W - 4, cy + 4, cx + 4,
                                  cy + CELL_H - 4, true);
            } else if (board[y][x] == O_MARK) {
                display_draw_circle(cx + CELL_W / 2, cy + CELL_H / 2,
                                    CELL_H / 2 - 5, true);
            }
        }
    }

    /* cursor */
    if (!over) {
        display_draw_rect(OX + cur_x * CELL_W - 1, OY + cur_y * CELL_H - 1,
                          CELL_W + 2, CELL_H + 2, false, true);
        display_draw_rect(OX + cur_x * CELL_W - 2, OY + cur_y * CELL_H - 2,
                          CELL_W + 4, CELL_H + 4, false, false);
    }

    snprintf(buf, sizeof(buf), "You X  AI O");
    display_draw_text_centered(59, buf, true);
}

static void render_result(void)
{
    display_clear();
    display_draw_rect(0, 0, DISPLAY_WIDTH, 12, true, true);
    display_draw_text_centered(3, result_text(), false);

    char buf[24];
    snprintf(buf, sizeof(buf), "Streak: %d", streak);
    display_draw_text_centered(22, buf, true);
    snprintf(buf, sizeof(buf), "Best: %lu", (unsigned long)best_streak);
    display_draw_text_centered(32, buf, true);

    display_draw_text_centered(46, "OK:Play Again  BACK:Menu", true);
}

void game_tictactoe_run(void)
{
    ttto_reset();

    while (1) {
        input_event_t ev;
        while (input_get_event(&ev, 0)) {
            if (ev.type != INPUT_EVENT_PRESS) continue;
            switch (ev.key) {
            case INPUT_KEY_UP:
                if (!over && cur_y > 0) cur_y--;
                break;
            case INPUT_KEY_DOWN:
                if (!over && cur_y < 2) cur_y++;
                break;
            case INPUT_KEY_LEFT:
                if (!over && cur_x > 0) cur_x--;
                break;
            case INPUT_KEY_RIGHT:
                if (!over && cur_x < 2) cur_x++;
                break;
            case INPUT_KEY_OK:
                if (over) {
                    reset_round();
                } else if (board[cur_y][cur_x] == EMPTY) {
                    player_move();
                }
                break;
            case INPUT_KEY_BACK:
                return;
            default:
                break;
            }
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
