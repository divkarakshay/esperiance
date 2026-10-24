#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *name;      /* display name */
    const char *nvs_key;   /* key used for high score storage */
    void (*run)(void);     /* game main loop, returns when user backs out */
} game_def_t;

extern const game_def_t games[];
extern const int game_count;

/* Game entry points. */
void game_snake_run(void);
void game_pong_run(void);
void game_breakout_run(void);
void game_space_run(void);
void game_pairs_run(void);
void game_bantumi_run(void);
void game_c4_run(void);
void game_mines_run(void);
void game_raindrops_run(void);
void game_bounce_run(void);
void game_snake2_run(void);
void game_tictactoe_run(void);
void game_tetris_run(void);
void game_munkiki_run(void);
void game_blocbloc_run(void);
void game_flan_run(void);

#ifdef __cplusplus
extern "C" {
#endif

/* Init NVS flash (idempotent). Call once at boot. */
void storage_init(void);

/* High score helpers. score_submit stores the score if it beats the
 * stored one and returns true (a new record). */
uint32_t score_get(const char *key);
bool score_submit(const char *key, uint32_t score);

/* Generic settings storage. */
uint32_t settings_get(const char *key, uint32_t def);
void settings_set(const char *key, uint32_t val);

#ifdef __cplusplus
}
#endif