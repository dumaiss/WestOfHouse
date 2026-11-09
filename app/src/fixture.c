#include "menu.h"
/* G0 test hotspot: the fixed artwork's visible mailbox, always
 * bound to MAILBOX. This never claims to describe the current Zork room. */
uint8_t fixture_hit(uint16_t x, uint16_t y)
{
    return x >= 73u && x <= 103u && y >= 88u && y <= 136u;
}
void fixture_menu(menu_t *m)
{
    menu_open(m, 90u, 55u, "MAILBOX", "EXAMINE", "OPEN", "CLOSE");
}
