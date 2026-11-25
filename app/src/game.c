#include "kos.h"
#include "game.h"
#include "woh_bindings.h"
game_t game;
typedef char preload_scratch_fits_vm[(sizeof(zvm_t) >= 256u) ? 1 : -1];
static int game_load(game_t *g, panel_t *panel);

const char *game_load_error(int result)
{
    switch (result) {
    case -1: return "cannot open story file";
    case -2: return "cannot read story header";
    case -3: return "story does not match the compiled global bindings";
    case -4: return "invalid story memory geometry";
    case -7: return "story page preload failed";
    case -8: return "ZVM initialization failed";
    default: return "ZVM output callback initialization failed";
    }
}

int game_open(game_t *g, const char *path, panel_t *panel)
{
    memset(g, 0, sizeof(*g));
    g->file = open(path, KFS_O_READ);
    if (g->file < 1) return -1;
    g->source.handle = (unsigned int)g->file;
    if (kos_reader_from_handle(&g->reader, &g->source) < 0) return -2;
    return game_load(g, panel);
}

int game_open_reader(game_t *g, const kos_reader_t *reader, panel_t *panel)
{
    memset(g, 0, sizeof(*g));
    g->reader = *reader;
    return game_load(g, panel);
}

static int game_load(game_t *g, panel_t *panel)
{
    uint8_t header[64], i;
    uint16_t dynamic, checksum;
    uint32_t end;
    if (kos_reader_read_full(&g->reader, 0, header, 64) != 64) return -2;
    for (i = 0; i < 64u; ++i) if (header[i] != woh_story_header[i]) return -3;
    dynamic = ((uint16_t)header[14] << 8) | header[15];
    end = ((uint32_t)header[26] << 9) | ((uint32_t)header[27] << 1);
    if (!dynamic || dynamic > KOS_XMEM_MAX_OBJECT || end < 64ul ||
        end > g->reader.size || dynamic > end) return -4;
    /* VM storage is scratch only until zvm_init_memory initializes it. */
    if (mapped_story_init(&g->mapped, &g->reader, dynamic, end, &checksum,
                          (uint8_t *)&g->vm) < 0) {
        memset(&g->vm, 0, sizeof(g->vm));
        return -7;
    }
    if (zvm_init_memory(&g->vm, mapped_story_read, mapped_story_write, &g->mapped,
                        g->reader.size) != ZVM_OK) return -8;
    g->initialized = 1;
    if (zvm_set_page_access(&g->vm, mapped_story_page, &g->mapped.held_slot) != ZVM_OK)
        return -8;
    if (zvm_set_story_checksum(&g->vm, checksum) != ZVM_OK ||
        zvm_set_output_callback(&g->vm, game_output, panel) != ZVM_OK) return -9;
    return 0;
}

int game_observe(game_t *g)
{
    if (zvm_global(&g->vm, WOH_HERE_INDEX, &g->here) != ZVM_OK) return -1;
    return zvm_global(&g->vm, WOH_STATE_INDEX, &g->state) == ZVM_OK ? 0 : -1;
}

/* Small cooperative slices let the hardware pointer move during slow turns. */
zvm_status_t game_step(game_t *g) { return mapped_story_run(&g->mapped, &g->vm, 50u); }

void game_close(game_t *g)
{
    if (g->initialized) { zvm_release(&g->vm); g->initialized = 0; }
    mapped_story_close(&g->mapped);
    if (g->file > 0) { close((unsigned int)g->file); g->file = 0; }
}
