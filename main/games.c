#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "games.h"

static const char *TAG = "storage";

const game_def_t games[] = {
    { "Snake",      "snake",    game_snake_run },
    { "Pong",       "pong",     game_pong_run },
    { "Breakout",   "breakout", game_breakout_run },
    { "Space Imp",  "space",    game_space_run },
    { "Pairs",      "pairs",    game_pairs_run },
    { "Bantumi",    "bantumi",  game_bantumi_run },
    { "4 in a Row", "connect4", game_c4_run },
    { "Minesweeper","mines",    game_mines_run },
    { "Raindrops",  "raindrops",game_raindrops_run },
    { "Bounce",     "bounce",   game_bounce_run },
    { "Snake II",   "snake2",   game_snake2_run },
    { "Tic-Tac-Toe","tictactoe",game_tictactoe_run },
    { "Tetris",     "tetris",   game_tetris_run },
    { "Munkiki","munkiki",game_munkiki_run },
    { "Bloc Bloc","blocbloc",game_blocbloc_run },
    { "Flan",       "flan",     game_flan_run },
};
const int game_count = (int)(sizeof(games) / sizeof(games[0]));

void storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "nvs ready");
}

uint32_t score_get(const char *key)
{
    nvs_handle_t h;
    uint32_t val = 0;
    if (nvs_open("scores", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, key, &val);
        nvs_close(h);
    }
    return val;
}

bool score_submit(const char *key, uint32_t score)
{
    if (score <= score_get(key)) {
        return false;
    }
    nvs_handle_t h;
    if (nvs_open("scores", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, key, score);
        nvs_commit(h);
        nvs_close(h);
    }
    return true;
}

uint32_t settings_get(const char *key, uint32_t def)
{
    nvs_handle_t h;
    uint32_t val = def;
    if (nvs_open("settings", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, key, &val);
        nvs_close(h);
    }
    return val;
}

void settings_set(const char *key, uint32_t val)
{
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, key, val);
        nvs_commit(h);
        nvs_close(h);
    }
}