/* Additive libgfx backend member, compiled into a local copy of libgfx.lib.
 * The application uses gfx_border.h only. Backend symbols/VRAM ownership are
 * the installed SDK's pointer contract (page 1 lines 496..511); no fixed
 * machine port or banking state is used. Slot 0 is white interior, slot 1
 * dark-blue silhouette, so the VDP's priority supplies the outline. */
#include "kos.h"
#include "kos/gfx.h"
#include "gfx_border.h"

extern uint8_t gfx_pointer_flags;
void gfx_v9958_io_set_vram(uint8_t r14, uint16_t offset);
void gfx_v9958_io_stream_vram(const uint8_t *src, uint16_t length);

/* 12 visible columns, 16 rows. Pattern order is left half then right. */
static const uint8_t fill[32] = {
    0,0,0x40,0x60,0x70,0x78,0x7C,0x7E,0x7F,0x78,0x58,0x0C,0x0C,0x06,0x06,0,
    0,0,0,0,0,0,0,0,0,0x80,0,0,0,0,0,0
};
static const uint8_t silhouette[32] = {
    0x80,0xC0,0xE0,0xF0,0xF8,0xFC,0xFE,0xFF,0xFF,0xFF,0xFC,0xDE,0x9E,0x0F,0x0F,0x06,
    0,0,0,0,0,0,0,0,0x80,0xF0,0,0,0,0,0,0
};
static const uint8_t outline_colors[16] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};

int gfx_pointer_bordered(int x, int y)
{
    uint8_t first = !(gfx_pointer_flags & 1u);
    uint8_t sat[5];
    int result;
    if (first && gfx_pointer_shape(fill, 15u) < 0) return -KOS_EINVAL;
    result = gfx_pointer_show(x, y);
    if (result < 0) return result;
    (void)gfx_wait();
    if (first) {
        gfx_v9958_io_set_vram(7u, 0x3820u);
        gfx_v9958_io_stream_vram(silhouette, 32u);
        gfx_v9958_io_set_vram(7u, 0x3010u);
        gfx_v9958_io_stream_vram(outline_colors, 16u);
    }
    sat[0] = (uint8_t)(y - 1);
    sat[1] = (uint8_t)x;
    sat[2] = 4u;
    sat[3] = 0;
    sat[4] = 0xD8u;
    gfx_v9958_io_set_vram(7u, 0x3204u);
    gfx_v9958_io_stream_vram(sat, sizeof(sat));
    return 0;
}
