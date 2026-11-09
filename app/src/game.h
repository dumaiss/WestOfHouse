#ifndef WOH_GAME_H
#define WOH_GAME_H
#include "zvm.h"
#include "story_cache.h"
#include "panel.h"
typedef struct {
    zvm_t vm;
    story_cache_t cache;
    kos_handle_source_t source;
    int file;
    uint8_t initialized;
    uint16_t here, state;
} game_t;
int game_open(game_t *game, const char *path, panel_t *panel);
const char *game_load_error(int result);
void game_close(game_t *game);
zvm_status_t game_step(game_t *game);
int game_observe(game_t *game);
/* Kept in its own module: taking this address must not pin the controller. */
zvm_error_t game_output(void *panel, uint8_t channel, uint8_t byte);
void game_output_finish(panel_t *panel);
#endif
