#ifndef WOH_RENDER_H
#define WOH_RENDER_H
#include "panel.h"
#include "menu.h"
enum { PERF_SETUP, PERF_SCENE, PERF_PANEL, PERF_PAGE, PERF_PROMPT, PERF_MENU_OPEN, PERF_MENU_CLOSE, PERF_COUNT };
int render_open(void);
void render_close(void);
int render_scene(uint16_t room, uint16_t state);
int render_panel(const panel_t *panel, uint8_t transition);
int render_prompt(const panel_t *panel, uint8_t active);
int render_menu(const menu_t *menu);
int render_menu_close(const menu_t *menu);
int render_pointer(int x, int y, uint8_t visible);
void render_performance(void);
#endif
