#include "game.h"
#include "probe.h"
/* The story emits its own trailing '>'; the graphical prompt owns that
 * character. Delay one byte, removing only a final standalone prompt. */
static uint8_t last, pending, line_start, last_at_start;

zvm_error_t game_output(void *user, uint8_t channel, uint8_t byte)
{
    panel_t *panel = user;
    if (channel != ZVM_OUTPUT_NARRATIVE) return ZVM_OK;
    ++woh_probe.output_bytes;
    woh_probe.output_hash = (woh_probe.output_hash << 5) + woh_probe.output_hash + byte;
    if (pending) panel_byte(panel, last);
    last = byte;
    last_at_start = line_start;
    line_start = byte == '\n';
    pending = 1;
    return ZVM_OK;
}

void game_output_finish(panel_t *panel)
{
    if (pending && !(last == '>' && last_at_start)) panel_byte(panel, last);
    pending = 0;
    line_start = 1;
}
