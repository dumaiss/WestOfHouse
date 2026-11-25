/* Mapping model: each token selects distinct storage at the same aperture.
 * It models callback-visible mapping and restoration, not CPU timings/IRQs. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "mapped_story.h"
static unsigned char pages[4][8192], aperture[8192];
static unsigned char active = 3;
static unsigned int maps, frees;
unsigned char aperture_open(unsigned char token)
{
    unsigned char previous = active;
    assert(token < 4);
    memcpy(pages[active], aperture, sizeof(aperture));
    memcpy(aperture, pages[token], sizeof(aperture));
    active = token;
    ++maps;
    return previous;
}
void aperture_close(unsigned char previous) { (void)aperture_open(previous); }
int xfree(unsigned int handle) { assert(handle == 1 || handle == 2); ++frees; return 0; }
static mapped_story_t *running;
static zvm_status_t next_status;
zvm_status_t zvm_run(zvm_t *vm, uint16_t budget)
{
    unsigned char value;
    unsigned int before = maps;
    (void)vm;
    if (!budget) return ZVM_YIELDED;
    for (unsigned int i = 0; i < 100; ++i)
        assert(mapped_story_read(running, 8191, &value) == ZVM_OK && value == 0x91);
    assert(maps == before + 1);
    for (unsigned int i = 0; i < 100; ++i)
        assert(mapped_story_read(running, 8192, &value) == ZVM_OK && value == 0xCC);
    assert(maps == before + 2);
    assert(mapped_story_run(running, vm, 1) == ZVM_FAULT); /* refuse reentry */
    return next_status;
}
int main(void)
{
    mapped_story_t m = {0};
    kos_reader_t reader = {0};
    unsigned char v;
    unsigned int before;
    assert((uintptr_t)aperture <= UINT32_MAX);
    reader.size = 8195;
    m.reader = &reader;
    m.count = 2;
    m.dynamic_size = 8193;
    m.handles[0] = 1; m.handles[1] = 2;
    m.tokens[0] = 1; m.tokens[1] = 2;
    m.addresses[0] = m.addresses[1] = (unsigned int)(uintptr_t)aperture;
    m.lengths[0] = 8192; m.lengths[1] = 3;
    pages[1][8191] = 0x91; pages[2][0] = 0xA2; pages[2][2] = 0xB3;
    aperture[99] = 0x55;
    for (unsigned int i = 0; i < 100; ++i) {
        assert(mapped_story_read(&m, 8191, &v) == ZVM_OK && v == 0x91);
        assert(mapped_story_read(&m, 8192, &v) == ZVM_OK && v == 0xA2);
        assert(active == 3 && aperture[99] == 0x55);
    }
    assert(m.remaps == 200 && maps == 400);
    assert(mapped_story_write(&m, 8192, 0xCC) == ZVM_OK);
    assert(pages[2][0] == 0xCC && active == 3);
    /* A destination in the caller's aperture must be written after restore. */
    assert(mapped_story_read(&m, 8194, &aperture[99]) == ZVM_OK);
    assert(aperture[99] == 0xB3 && pages[2][2] == 0xB3 && active == 3);
    before = maps;
    assert(mapped_story_read(&m, 8195, &v) == ZVM_EBOUNDS);
    assert(mapped_story_write(&m, 8193, 0) == ZVM_EREADONLY);
    m.lengths[1] = 1;
    assert(mapped_story_read(&m, 8194, &v) == ZVM_EBOUNDS);
    assert(maps == before);
    m.lengths[1] = 3;
    running = &m;
    for (unsigned int k = 0; k < 5; ++k) {
        static const zvm_status_t outcomes[] = {ZVM_YIELDED, ZVM_NEED_INPUT, ZVM_FINISHED, ZVM_FAULT, ZVM_READY};
        next_status = outcomes[k];
        before = maps;
        assert(mapped_story_run(&m, 0, 1) == next_status);
        assert(active == 3 && !m.active && !m.held);
        assert(maps == before + 3); /* two regions, one caller restore */
        assert(aperture[99] == 0xB3);
    }
    before = maps;
    assert(mapped_story_run(&m, 0, 0) == ZVM_YIELDED && maps == before);
    mapped_story_close(&m);
    assert(frees == 2 && !m.count && !m.valid);
    puts("mapped story scopes: PASS (alternating regions, boundary, caller mapping, immediate writes, bounds, cleanup)");
}
