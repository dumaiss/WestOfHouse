#include "menu.h"
#ifdef __SDCC
#include "klibc/string.h"
#else
#include <string.h>
#endif
void menu_open(menu_t *m, uint16_t x, uint16_t y, const char *noun,
               const char *a, const char *b, const char *c)
{
    m->x = x > 204u ? 204u : x;
    m->y = y > 107u ? 107u : y;
    m->noun = noun;
    m->labels[0] = a; m->labels[1] = b; m->labels[2] = c;
    m->selected = 255u;
    m->open = 1;
}
int menu_hit(const menu_t *m, uint16_t x, uint16_t y)
{
    uint8_t item;
    if (!m->open || x <= m->x || x >= m->x + 49u || y < m->y + 2u || y >= m->y + 29u) return -1;
    item = (uint8_t)((y - m->y - 2u) / 9u);
    return item < MENU_ITEMS ? item : -1;
}
uint8_t menu_hover(menu_t *m, uint16_t x, uint16_t y)
{
    uint8_t selected = (uint8_t)menu_hit(m, x, y);
    if (selected == m->selected) return 0;
    m->selected = selected; return 1;
}
void menu_command(const menu_t *m, uint8_t item, char *out)
{
    const char *s;
    if (item >= MENU_ITEMS) { *out = 0; return; }
    s = m->labels[item]; while (*s) *out++ = *s++;
    *out++ = ' ';
    s = m->noun; while (*s) *out++ = *s++;
    *out = 0;
}
