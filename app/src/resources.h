#ifndef WOH_RESOURCES_H
#define WOH_RESOURCES_H
#include "kos/reader.h"
#include "menu.h"
/* One sealed KRES backing file; story and scene descriptors have independent
 * logical cursors. No mapped pointers or additional XMEM objects are used. */
int resources_open(const char *path);
void resources_close(void);
int resources_story(kos_reader_t *reader);
/* 1: mapped scene, 0: no artwork for pair, negative: bad/missing payload. */
int resources_scene(uint16_t room, uint16_t state);
/* Title is the existing full-screen YJK KIMG resource. */
int resources_title(void);
int resources_pixels(void *buffer, uint16_t size);
int resources_menu(menu_t *menu, uint16_t x, uint16_t y);
#endif
