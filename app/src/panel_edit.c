#include "panel.h"

/* Navigation and command editing run outside VM output callbacks. */
const panel_page_t *panel_visible(const panel_t *p)
{
    if (!p->count) return 0;
    return &p->pages[(p->first + p->view) % PANEL_PAGES];
}

uint8_t panel_page_up(panel_t *p)
{
    if (!p->view) return 0;
    --p->view; return 1;
}

uint8_t panel_page_down(panel_t *p)
{
    if (p->view + 1u >= p->count) return 0;
    ++p->view; return 1;
}

void panel_latest(panel_t *p) { if (p->count) p->view = p->count - 1u; }

uint8_t panel_insert(panel_t *p, uint8_t byte)
{
    uint8_t i;
    if (byte < 32u || byte == 127u || p->command_length == COMMAND_MAX) return 0;
    for (i = p->command_length; i > p->cursor; --i) p->command[i] = p->command[i - 1u];
    p->command[p->cursor++] = (char)byte;
    p->command[++p->command_length] = 0;
    return 1;
}

uint8_t panel_backspace(panel_t *p)
{
    if (!p->cursor) return 0;
    --p->cursor;
    return panel_delete(p);
}

uint8_t panel_delete(panel_t *p)
{
    uint8_t i;
    if (p->cursor == p->command_length) return 0;
    for (i = p->cursor; i < p->command_length; ++i) p->command[i] = p->command[i + 1u];
    --p->command_length;
    return 1;
}

uint8_t panel_prompt_cells(const panel_t *p)
{
    uint8_t used = p->command_length + 1u;
    if (p->hint && used <= 24u) return 24u;
    return used <= 36u ? 36u : 40u;
}
