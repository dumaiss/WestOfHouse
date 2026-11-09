#include "kos.h"
#include "kos/gfx.h"
#include "kos/timer.h"
#include "gfx_border.h"
#include "render.h"
#include "layout.h"

#define BG 0x01u
static const struct gfx_text_style narrative = {COLOR_TEXT, BG, 0, 0};
static const struct gfx_text_style amber = {COLOR_COMMAND, BG, 0, 0};
static const struct gfx_text_style hint = {0xB5u, BG, 0, 0};
static const struct gfx_text_style overlay = {COLOR_TEXT, BG, 0, 0};
static const struct gfx_text_style menu_normal = {COLOR_TEXT, 0x25u, 0, 0};
static const struct gfx_text_style menu_selected = {BG, COLOR_COMMAND, 0, 0};
static const uint8_t row_y[PANEL_ROWS] = {146,154,162,170,178,186,194};
/* Keep the SDK's pinned 6x8 letters (including bottom-row descenders).
 * Its two CP850 arrow slots contain placeholders; define those two only. */
static const uint8_t glyphs[2][8] = {
    {0,4,0x0E,0x1F,0x1F,0,0,0},
    {0,0,0,0x1F,0x1F,0x0E,4,0}
};
static const uint8_t glyph_codes[2] = {0x1E,0x1F};
typedef struct { uint16_t calls; uint32_t total, maximum; } perf_t;
static perf_t metrics[PERF_COUNT];
static int timer_handle;
static uint8_t opened, scene_loaded, overlay_valid;
static uint16_t last_room, last_state;
static uint8_t upload_buffer[256];

static uint32_t start(void)
{
    uint32_t now = 0;
    if (timer_handle > 0) (void)timer_now(timer_handle, &now);
    return now;
}
static void stop(uint8_t op, uint32_t before)
{
    uint32_t elapsed;
    if (timer_handle < 1) return;
    elapsed = start() - before;
    ++metrics[op].calls;
    metrics[op].total += elapsed;
    if (elapsed > metrics[op].maximum) metrics[op].maximum = elapsed;
}

int render_open(void)
{
    uint32_t before;
    uint8_t i;
    timer_handle = timer_open();
    before = start();
    if (gfx_open() < 0) return -1;
    opened = 1;
    if (gfx_set_mode(GFX_MODE_256x212_256) < 0 || gfx_clear(BG) < 0) return -2;
    for (i = 0; i < 2u; ++i)
        if (gfx_font_set_glyph(GFX_FONT_6X8, glyph_codes[i], glyphs[i]) < 0) return -3;
    if (gfx_fill_rect(0,SCENE_HEIGHT,256,1,0x4D,GFX_OP_COPY) < 0) return -4;
    stop(PERF_SETUP, before);
    return 0;
}

/* Future scene lookup replaces only this operation. The fixed scene is
 * streamed once from EXE blobs, then the clean VRAM page serves menu restores. */
int render_scene(uint16_t room, uint16_t state)
{
    uint8_t part;
    uint16_t offset, size, chunk;
    uint32_t before;
    int handle;
    char text[24];
    if (!scene_loaded) {
        before = start();
        if (gfx_upload_begin(0,0,256,SCENE_HEIGHT) < 0) return -1;
        for (part = 1; part <= 2u; ++part) {
            handle = blob(part);
            size = part == 1u ? 32768u : SCENE_TAIL_BYTES;
            if (handle < 1) { (void)gfx_upload_end(); return -2; }
            for (offset = 0; offset < size; offset += chunk) {
                chunk = size - offset;
                if (chunk > sizeof(upload_buffer)) chunk = sizeof(upload_buffer);
                if (xread((unsigned int)handle, offset, upload_buffer, chunk) != (int)chunk ||
                    gfx_upload_write(upload_buffer, chunk) != (int)chunk) {
                    (void)gfx_upload_end(); return -3;
                }
            }
        }
        if (gfx_upload_end() < 0 || gfx_copy_rect(0,0,0,256,256,SCENE_HEIGHT) < 0 || gfx_wait() < 0) return -4;
        scene_loaded = 1;
        stop(PERF_SCENE, before);
    }
    if (overlay_valid && room == last_room && state == last_state) return 0;
    if (gfx_fill_rect(2,2,138,18,BG,GFX_OP_COPY) < 0) return -5;
    snprintf(text,sizeof(text),"HERE:      %u",(unsigned)room);
    if (gfx_text(2,3,text,&overlay) < 0) return -5;
    snprintf(text,sizeof(text),"WOH-STATE: %u",(unsigned)state);
    if (gfx_text(2,11,text,&overlay) < 0) return -5;
    /* Cache the overlay too, so a generic menu may cover this region. */
    if (gfx_copy_rect(2,2,2,258,138,18) < 0) return -5;
    last_room = room; last_state = state; overlay_valid = 1;
    return gfx_display_enable(1);
}

int render_panel(const panel_t *p, uint8_t transition)
{
    const panel_page_t *page = panel_visible(p);
    uint32_t before = start();
    uint8_t row, length;
    int result = 0;
    if (gfx_fill_rect(0,PANEL_TOP,256,PROMPT_TOP - PANEL_TOP,BG,GFX_OP_COPY) < 0) return -1;
    if (page) for (row = 0; row < page->rows; ++row) {
        result = gfx_text(2,row_y[row],page->text[row],
                         page->color[row] == COLOR_COMMAND ? &amber : &narrative);
        if (result < 0) return result;
        if (row == PANEL_ROWS - 1u && page->continued) {
            length = (uint8_t)strlen(page->text[row]);
            if (gfx_text((uint16_t)(2u + ((uint16_t)(length + 1u) << 2) +
                                    ((uint16_t)(length + 1u) << 1)),row_y[row],"[...]",&amber) < 0) return -1;
        }
    }
    (void)gfx_wait(); stop(transition ? PERF_PAGE : PERF_PANEL,before);
    return 0;
}

int render_prompt(const panel_t *p, uint8_t active)
{
    uint32_t before = start();
    uint8_t cells = panel_prompt_cells(p), offset = 0, amount;
    uint16_t cursor_x;
    char cursor[2];
    struct gfx_text_style inverse;
    if (gfx_fill_rect(0,202,256,10,BG,GFX_OP_COPY) < 0 || gfx_text(2,203,"> ",&amber) < 0) return -1;
    if (p->cursor >= cells) offset = p->cursor - cells + 1u;
    amount = p->command_length - offset;
    if (amount > cells) amount = cells;
    if (gfx_text_n(14,203,p->command + offset,amount,&amber) < 0) return -1;
    if (p->hint && cells == 24u && gfx_text(158,203,"(PgUp/PgDn)",&hint) < 0) return -1;
    if (cells != 40u) {
        if (p->view && gfx_text(230,203,"\036",&amber) < 0) return -1;
        if (p->view + 1u < p->count && gfx_text(242,203,"\037",&amber) < 0) return -1;
    }
    if (active) {
        cursor[0] = p->cursor < p->command_length ? p->command[p->cursor] : ' ';
        cursor[1] = 0;
        inverse.fg = BG; inverse.bg = COLOR_COMMAND; inverse.flags = inverse.font = 0;
        cursor_x = (uint16_t)(p->cursor - offset);
        cursor_x = 14u + (cursor_x << 2) + (cursor_x << 1);
        if (gfx_text(cursor_x,203,cursor,&inverse) < 0) return -1;
    }
    (void)gfx_wait(); stop(PERF_PROMPT,before); return 0;
}

int render_menu(const menu_t *m)
{
    uint8_t i;
    uint16_t y = m->y + 2u;
    uint32_t before = start();
    if (gfx_fill_rect(m->x+2u,m->y+2u,50,32,0,GFX_OP_COPY) < 0 ||
        gfx_fill_rect(m->x,m->y,50,32,0x25,GFX_OP_COPY) < 0 ||
        gfx_rect(m->x,m->y,50,32,COLOR_TEXT,GFX_OP_COPY) < 0) return -1;
    for (i = 0; i < MENU_ITEMS; ++i, y += 9u) {
        if (gfx_fill_rect(m->x+1u,y,48,9,i == m->selected ? COLOR_COMMAND : 0x25,GFX_OP_COPY) < 0 ||
            gfx_text(m->x+4u,y,m->labels[i],i == m->selected ? &menu_selected : &menu_normal) < 0) return -1;
    }
    if (gfx_line(m->x+8u,m->y+32u,m->x+8u,m->y+36u,COLOR_TEXT,GFX_OP_COPY) < 0 ||
        gfx_line(m->x+8u,m->y+36u,m->x+12u,m->y+32u,COLOR_TEXT,GFX_OP_COPY) < 0) return -1;
    (void)gfx_wait(); stop(PERF_MENU_OPEN,before); return 0;
}

int render_menu_close(const menu_t *m)
{
    uint32_t before = start();
    int result = gfx_copy_rect(m->x,m->y+256u,m->x,m->y,52,37);
    (void)gfx_wait(); stop(PERF_MENU_CLOSE,before); return result;
}
int render_pointer(int x, int y, uint8_t visible)
{
    return visible ? gfx_pointer_bordered(x,y) : gfx_pointer_hide();
}
void render_close(void)
{
    if (opened) { (void)gfx_pointer_hide(); gfx_close(); opened = 0; }
    if (timer_handle > 0) { close((unsigned int)timer_handle); timer_handle = 0; }
}
void render_performance(void)
{
    uint8_t i;
    for (i = 0; i < PERF_COUNT; ++i)
        printf("\rwoh-perf: op=%u calls=%u total_us=%lu max_us=%lu\r\n",(unsigned)i,
               (unsigned)metrics[i].calls,(unsigned long)timer_us(metrics[i].total),
               (unsigned long)timer_us(metrics[i].maximum));
}
