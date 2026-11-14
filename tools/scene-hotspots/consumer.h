/* WHT1 consumer. No file, KRES, graphics or VM dependencies.
 * The caller owns immutable bytes for the lifetime of each view. On KonaOS,
 * keep those bytes visible across calls (resident storage, no retained mapped
 * data pointers across aperture-changing operations). WOH uses resident caller-owned storage. */
#ifndef WOH_HOTSPOT_CONSUMER_H
#define WOH_HOTSPOT_CONSUMER_H
#ifdef __SDCC
#include "kos/types.h"
#else
#include <stdint.h>
#endif
#define WHS_MAX_BYTES 4096u
#define WHS_LABEL_CAPACITY 9u
#define WHS_COMMAND_CAPACITY 81u
typedef struct {
    const uint8_t *bytes;
    uint16_t size, room, state;
    uint8_t count, unbound;
} whs_table_t;
typedef struct {
    uint16_t width, action_offset;
    uint8_t id, x, y, height, action_count;
    const uint8_t *label;
    uint8_t label_length;
} whs_region_t;
/* 1 valid, 0 malformed (output cleared). Valid empty/unbound tables allowed. */
int whs_open(whs_table_t *out, const uint8_t *bytes, uint16_t size);
int whs_matches(const whs_table_t *table, uint16_t room, uint16_t state);
/* Half-open edges; smallest area wins, then serialized order. Unbound tables
 * never return runtime hits. Region enumeration still permits concept review. */
int whs_region(const whs_table_t *table, uint8_t index, whs_region_t *out);
int whs_hit(const whs_table_t *table, uint16_t x, uint16_t y, whs_region_t *out);
/* Returns 1 and copies NUL-terminated strings, or 0 for an invalid action/cap.
 * Commands are complete ordinary parser input, not verb/noun fragments. */
int whs_action(const whs_table_t *table, const whs_region_t *region, uint8_t index,
               char *label, uint8_t label_capacity,
               char *command, uint8_t command_capacity);
#endif
