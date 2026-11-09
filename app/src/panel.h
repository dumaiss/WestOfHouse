#ifndef WOH_PANEL_H
#define WOH_PANEL_H
#include "kos/types.h"

#define PANEL_COLS 42u
#define PANEL_ROWS 7u
#define PANEL_PAGES 8u
#define COMMAND_MAX 80u
#define COLOR_TEXT 0xFEu
#define COLOR_COMMAND 0xBCu

typedef struct {
    char text[PANEL_ROWS][PANEL_COLS + 1u];
    uint8_t color[PANEL_ROWS];
    uint8_t rows;
    uint8_t continued;
} panel_page_t;

/* Wrapped pages, not a framebuffer. Eviction always removes a whole page. */
typedef struct {
    panel_page_t pages[PANEL_PAGES];
    uint8_t first, count, view, row, color;
    uint8_t response_start, response_pages, hint;
    char pending[PANEL_COLS + 2u];
    uint8_t length, newline;
    char command[COMMAND_MAX + 1u];
    uint8_t command_length, cursor;
} panel_t;

void panel_init(panel_t *p);
void panel_begin(panel_t *p, const char *echo);
void panel_byte(panel_t *p, uint8_t byte);
void panel_finish(panel_t *p);
const panel_page_t *panel_visible(const panel_t *p);
uint8_t panel_page_up(panel_t *p);
uint8_t panel_page_down(panel_t *p);
void panel_latest(panel_t *p);
/* Printable insertion / editing operate on the prompt only. */
uint8_t panel_insert(panel_t *p, uint8_t byte);
uint8_t panel_backspace(panel_t *p);
uint8_t panel_delete(panel_t *p);
uint8_t panel_prompt_cells(const panel_t *p);
#endif
