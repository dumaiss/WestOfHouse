#ifndef WOH_PROBE_H
#define WOH_PROBE_H
#include "kos/types.h"
/* Read-only acceptance telemetry. No input injection, VM writes or shortcuts.
 * The emulator reads this symbol through the existing bank-aware plugin. */
typedef struct {
    uint8_t magic[4];
    uint8_t waiting;
    uint16_t epoch, here, state;
    uint8_t view, pages, command_length, cursor, menu, mouse;
    uint16_t commands;
    uint32_t output_bytes, output_hash;
    uint16_t mouse_x, mouse_y;
} woh_probe_t;
extern woh_probe_t woh_probe;
#endif
