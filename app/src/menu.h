#ifndef WOH_MENU_H
#define WOH_MENU_H
#include "kos/types.h"
#define MENU_ITEMS 3u
typedef struct {
    uint8_t open, selected;
    uint16_t x, y;
    const char *noun;
    const char *labels[MENU_ITEMS];
} menu_t;
void menu_open(menu_t *m, uint16_t x, uint16_t y, const char *noun,
               const char *a, const char *b, const char *c);
uint8_t menu_hover(menu_t *m, uint16_t x, uint16_t y);
int menu_hit(const menu_t *m, uint16_t x, uint16_t y);
void menu_command(const menu_t *m, uint8_t item, char *out);
/* Deliberately separate from the widget: fixed-image test fixture only. */
uint8_t fixture_hit(uint16_t x, uint16_t y);
void fixture_menu(menu_t *m);
#endif
