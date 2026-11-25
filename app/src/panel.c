#include "panel.h"
#ifdef __SDCC
#include "klibc/string.h"
#else
#include <string.h>
#endif

static panel_page_t *latest(panel_t *p)
{
    return &p->pages[(p->first + p->count - 1u) % PANEL_PAGES];
}

static void new_page(panel_t *p)
{
    panel_page_t *page;
    if (p->count == PANEL_PAGES) {
        p->first = (p->first + 1u) % PANEL_PAGES;
        if (p->response_start) --p->response_start;
        if (p->view) --p->view;
    } else ++p->count;
    page = latest(p);
    memset(page, 0, sizeof(*page));
    p->row = 0;
}

void panel_init(panel_t *p)
{
    memset(p, 0, sizeof(*p));
}

static void store(panel_t *p, uint8_t length, uint8_t consume)
{
    panel_page_t *page;
    uint8_t i;
    if (p->row == PANEL_ROWS) {
        latest(p)->continued = 1;
        new_page(p);
        if (p->response_pages <= PANEL_PAGES) ++p->response_pages;
    }
    page = latest(p);
    memcpy(page->text[p->row], p->pending, length);
    page->text[p->row][length] = 0;
    page->color[p->row] = p->color;
    page->rows = ++p->row;
    for (i = consume; i < p->length; ++i)
        p->pending[i - consume] = p->pending[i];
    p->length -= consume;
}

/* A row is delayed until the next text/newline establishes whether it is
 * continued. Thus a final page row can use all 42 cells, while a continued
 * page-ending row has 36 cells plus the separating space and [...]. */
static void wrap(panel_t *p, uint8_t forced, uint8_t final)
{
    uint8_t limit, cut, consume, i;
    do {
        limit = (p->row == PANEL_ROWS - 1u && !final) ? 36u : PANEL_COLS;
        if (p->length <= limit) {
            if (forced) store(p, p->length, p->length);
            return;
        }
        cut = limit;
        for (i = limit; i > 0u; --i) {
            if (p->pending[i] == ' ') { cut = i; break; }
        }
        consume = cut;
        if (p->pending[consume] == ' ') ++consume;
        store(p, cut, consume);
    } while (forced || p->length > PANEL_COLS);
}

void panel_begin(panel_t *p, const char *echo)
{
    new_page(p);
    p->response_start = p->count - 1u;
    p->response_pages = 1;
    p->length = p->newline = 0;
    p->color = COLOR_COMMAND;
    if (echo) {
        panel_byte(p, '>'); panel_byte(p, ' ');
        while (*echo) panel_byte(p, (uint8_t)*echo++);
        /* Commit the echo before changing the narrative's colour. */
        wrap(p, 1, 0);
    }
    p->color = COLOR_TEXT;
}

void panel_byte(panel_t *p, uint8_t byte)
{
    if (byte == '\r') byte = '\n';
    if (byte == '\n') {
        if (p->newline) wrap(p, 1, 0);
        p->newline = 1;
        return;
    }
    if (byte < 32u) return;
    if (p->newline) {
        wrap(p, 1, 0);
        p->newline = 0;
    }
    p->pending[p->length++] = (char)byte;
    if (p->length > PANEL_COLS) wrap(p, 0, 0);
}

void panel_finish(panel_t *p)
{
    if (p->length) wrap(p, 1, 1);
    p->newline = 0;
    p->view = p->response_start;
    p->hint = p->response_pages > 1u;
}
