#ifndef WOH_MENU_H
#define WOH_MENU_H
#include "kos/types.h"
#define MENU_ITEMS 3u
#define MENU_WIDTH 54u
#define MENU_RESTORE_WIDTH 56u
typedef struct {
    uint8_t open, selected, count;
    uint16_t x, y;
    const char *noun;
    const char *labels[MENU_ITEMS];
    char action_labels[MENU_ITEMS][9];
    char commands[MENU_ITEMS][81];
} menu_t;
void menu_open(menu_t *m, uint16_t x, uint16_t y, const char *noun,
               const char *a, const char *b, const char *c);
uint8_t menu_hover(menu_t *m, uint16_t x, uint16_t y);
int menu_hit(const menu_t *m, uint16_t x, uint16_t y);
void menu_command(const menu_t *m, uint8_t item, char *out);
#endif
