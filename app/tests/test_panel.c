#include "panel.h"
#include "menu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static panel_t p;
static void text(const char *s) { while (*s) panel_byte(&p,(uint8_t)*s++); }
static void begin(void) { panel_init(&p); panel_begin(&p,0); }
static void wrapping(void)
{
    const panel_page_t *page;
    begin();
    text("one\ntwo\nthree\nfour\nfive\nsix\n123456789012345678901234567890123456789012");
    panel_finish(&p);
    page = panel_visible(&p);
    assert(p.count == 1 && !page->continued);
    assert(strlen(page->text[PANEL_ROWS - 1u]) == 42);
    begin();
    text("one\ntwo\nthree\nfour\nfive\nsix\n123456789012345678901234567890123456789012\nnext");
    panel_finish(&p);
    page = panel_visible(&p);
    assert(p.count == 2 && p.view == 0 && p.hint);
    assert(page->continued && strlen(page->text[PANEL_ROWS - 1u]) == 36);
    assert(panel_page_down(&p));
    page = panel_visible(&p);
    assert(!strcmp(page->text[0],"789012"));
    assert(!strcmp(page->text[1],"next"));
    assert(!panel_page_down(&p)); assert(panel_page_up(&p));
    begin(); text("  indentation  stays\n\nparagraph"); panel_finish(&p);
    page = panel_visible(&p);
    assert(!strcmp(page->text[0],"  indentation  stays"));
    assert(!strcmp(page->text[1],""));
    assert(!strcmp(page->text[2],"paragraph"));
    begin(); text("12345678901234567890123456789012345678 word end"); panel_finish(&p);
    page = panel_visible(&p);
    assert(!strcmp(page->text[0],"12345678901234567890123456789012345678"));
    assert(!strcmp(page->text[1],"word end"));
}
static void history(void)
{
    unsigned int i;
    char s[20];
    begin(); text("old"); panel_finish(&p);
    panel_begin(&p,"look"); text("reply"); panel_finish(&p);
    assert(panel_visible(&p)->color[0] == COLOR_COMMAND);
    assert(!strcmp(panel_visible(&p)->text[0],"> look"));
    assert(panel_visible(&p)->color[1] == COLOR_TEXT);
    assert(panel_page_up(&p)); assert(!strcmp(panel_visible(&p)->text[0],"old"));
    for (i = 0; i < 30; ++i) {
        snprintf(s,sizeof(s),"response %u",i);
        panel_begin(&p,0); text(s); panel_finish(&p);
    }
    assert(p.count == PANEL_PAGES && p.view == PANEL_PAGES - 1);
    assert(!strcmp(panel_visible(&p)->text[0],"response 29"));
    while (panel_page_up(&p)) {}
    assert(!strcmp(panel_visible(&p)->text[0],"response 22"));
    panel_begin(&p,0);
    for (i = 0; i < 1000; ++i) text("a line of text\n");
    panel_finish(&p);
    assert(p.count == PANEL_PAGES && p.view == 0 && p.hint);
}
static void editing(void)
{
    unsigned int i;
    begin();
    assert(panel_insert(&p,'a')); assert(panel_insert(&p,'c'));
    p.cursor = 1; assert(panel_insert(&p,'b')); assert(!strcmp(p.command,"abc"));
    assert(panel_backspace(&p)); assert(!strcmp(p.command,"ac"));
    assert(panel_delete(&p)); assert(!strcmp(p.command,"a"));
    p.cursor = 0; assert(!panel_backspace(&p));
    p.cursor = p.command_length;
    for (i = 1; i < COMMAND_MAX; ++i) assert(panel_insert(&p,'x'));
    assert(!panel_insert(&p,'x') && p.command_length == COMMAND_MAX);
    p.hint = 1; p.command_length = 23; assert(panel_prompt_cells(&p) == 24);
    p.command_length = 24; assert(panel_prompt_cells(&p) == 36);
    p.command_length = 35; assert(panel_prompt_cells(&p) == 36);
    p.command_length = 36; assert(panel_prompt_cells(&p) == 40);
}
static void menus(void)
{
    menu_t m;
    char command[81];
    memset(&m,0,sizeof(m)); fixture_menu(&m);
    assert(fixture_hit(88,110) && !fixture_hit(10,10));
    assert(menu_hover(&m,m.x+4,m.y+3));
    assert(m.selected == 0 && !menu_hover(&m,m.x+4,m.y+3));
    assert(menu_hit(&m,m.x+4,m.y+12) == 1);
    assert(menu_hit(&m,m.x,m.y+12) == -1);
    menu_command(&m,1,command); assert(!strcmp(command,"OPEN MAILBOX"));
    menu_command(&m,255,command); assert(!command[0]);
    menu_open(&m,255,158,"X","A","B","C");
    assert(m.x == 204 && m.y == 107);
}
int main(void)
{
    wrapping(); history(); editing(); menus();
    printf("panel/menu tests passed; history model %lu bytes\n",(unsigned long)sizeof(panel_t));
    return 0;
}
