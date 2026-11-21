/* KonaOS test-host story backend: immutable reader plus writable XMEM.
 * Separate cache partitions handle pages straddling the static-memory boundary. */
#include "story_cache.h"
#include "kos.h"

/* Cache-slot sentinel only. A page tag of 255 is a valid story address tag. */
#define NO_PAGE 255u
#if defined(WOH_PROFILE) && WOH_PROFILE
#include "kos/timer.h"
/* Diagnostic build only: story-page I/O time in timer counts (25.6 us). */
uint32_t woh_prof_io_static, woh_prof_io_dynamic;
uint32_t woh_prof_static_misses, woh_prof_dyn_misses, woh_prof_flushes;
int woh_prof_timer;
uint32_t woh_prof_now(void)
{
	uint32_t t = 0;
	if (woh_prof_timer > 0)
		(void)timer_now(woh_prof_timer, &t);
	return t;
}
#define IO_START(v) uint32_t v = woh_prof_now()
#define IO_ADD(total, v) (total += woh_prof_now() - (v))
#else
#define IO_START(v) ((void)0)
#define IO_ADD(total, v) ((void)0)
#endif
#if defined(__SDCC) && defined(ZVM_CACHE_ASM) && ZVM_CACHE_ASM
#define story_cache_read story_cache_read_slow
#endif
#if defined(ZVM_PROFILE) && ZVM_PROFILE
#define COUNT(c, field, n)                                                                         \
	do {                                                                                       \
		if ((c)->profile_enabled)                                                          \
			(c)->stats.field += (n);                                                   \
	} while (0)
#define COUNT_KIND(c, dynamic, dfield, sfield)                                                     \
	do {                                                                                       \
		if (dynamic) {                                                                     \
			COUNT(c, dfield, 1);                                                       \
		} else {                                                                           \
			COUNT(c, sfield, 1);                                                       \
		}                                                                                  \
	} while (0)
#else
#define COUNT(c, field, n) ((void)0)
#define COUNT_KIND(c, dynamic, dfield, sfield) ((void)0)
#endif

#if defined(__SDCC) && defined(ZVM_CACHE_ASM) && ZVM_CACHE_ASM && defined(ZVM_PROFILE) &&          \
    ZVM_PROFILE
void story_cache_hit(story_cache_t *c, uint32_t address)
{
	if (address < c->dynamic_size) {
		COUNT(c, dyn_hits, 1);
	} else {
		COUNT(c, static_hits, 1);
	}
}
#endif

static uint16_t dynamic_length(const story_cache_t *c, uint8_t tag)
{
	uint16_t remaining = (uint16_t)(c->dynamic_size - ((uint16_t)tag << 9));
	return remaining > STORY_CACHE_PAGE_SIZE ? STORY_CACHE_PAGE_SIZE : remaining;
}

static int dynamic_write(story_cache_t *c, uint16_t offset, const void *src, uint16_t n)
{
	int r;
	IO_START(t0);
	COUNT(c, xwrite_calls, 1);
	COUNT(c, xwrite_bytes, n);
	r = xwrite(c->dynamic_handle, offset, src, n);
	IO_ADD(woh_prof_io_dynamic, t0);
#if defined(WOH_PROFILE) && WOH_PROFILE
	++woh_prof_flushes;
#endif
	return r;
}

static zvm_error_t flush_page(story_cache_t *c, uint8_t index)
{
	story_cache_page_t *p = &c->pages[index];
	uint16_t n;
	if (!p->valid || !p->dirty)
		return ZVM_OK;
	n = dynamic_length(c, p->tag);
	if (dynamic_write(c, (uint16_t)p->tag << 9,
			  c->bytes + (uint16_t)index * STORY_CACHE_PAGE_SIZE, n) != (int)n)
		return ZVM_ESTATE;
	p->dirty = 0;
	return ZVM_OK;
}

/* Fully associative, with a hot-page shortcut and bounded round-robin victims.
 * Failed dirty writeback leaves the old page intact. After successful
 * writeback the victim is invalidated before filling: a partial failed read
 * can never publish corrupted bytes under either the old or new tag. */
static zvm_error_t find_page(story_cache_t *c, uint32_t address, uint8_t *found)
{
	uint8_t dynamic = (uint8_t)(address < c->dynamic_size), tag = (uint8_t)(address >> 9);
	uint8_t first = dynamic ? 0 : DYN_CACHE_PAGES;
	uint8_t count = dynamic ? DYN_CACHE_PAGES : STATIC_CACHE_PAGES;
	uint8_t *hot = dynamic ? &c->dyn_hot : &c->static_hot;
	uint8_t *next = dynamic ? &c->dyn_next : &c->static_next;
	uint8_t index = *hot, i;
	uint16_t n;
	uint32_t start;
	int got;
	if (index != NO_PAGE && c->pages[index].valid && c->pages[index].tag == tag)
		goto hit;
	for (i = 0; i < count; ++i) {
		index = (uint8_t)(first + i);
		if (c->pages[index].valid && c->pages[index].tag == tag)
			goto hit;
	}
	COUNT_KIND(c, dynamic, dyn_misses, static_misses);
	/* Consume unused entries before selecting the next replacement victim. */
	for (i = 0; i < count; ++i)
		if (!c->pages[first + i].valid)
			break;
	index = (uint8_t)(first + (i < count ? i : *next));
	if (c->pages[index].valid) {
		if (dynamic) {
			if (flush_page(c, index) != ZVM_OK)
				return ZVM_ESTATE;
			COUNT(c, dyn_evictions, 1);
		} else {
			COUNT(c, static_evictions, 1);
		}
	}
	c->pages[index].valid = 0;
	++c->generation;
	if (*hot == index)
		*hot = NO_PAGE;
	start = (uint32_t)tag << 9;
	{
	IO_START(t0);
	if (dynamic) {
		n = dynamic_length(c, tag);
		COUNT(c, xread_calls, 1);
		COUNT(c, xread_bytes, n);
		got = xread(c->dynamic_handle, (uint16_t)start,
			    c->bytes + (uint16_t)index * STORY_CACHE_PAGE_SIZE, n);
	} else {
		n = (uint16_t)(c->reader.size - start > STORY_CACHE_PAGE_SIZE
				   ? STORY_CACHE_PAGE_SIZE
				   : c->reader.size - start);
		got = 0;
#if STORY_L2_PAGES
		if (c->l2_count)
			got = story_cache_l2_get(c, tag, c->bytes + (uint16_t)index * STORY_CACHE_PAGE_SIZE, n);
#endif
		if (got != (int)n) {
			got = kos_reader_read_full(&c->reader, start,
						   c->bytes + (uint16_t)index * STORY_CACHE_PAGE_SIZE, n);
#if STORY_L2_PAGES
			if (got == (int)n && c->l2_count)
				story_cache_l2_put(c, tag, c->bytes + (uint16_t)index * STORY_CACHE_PAGE_SIZE, n);
#endif
		}
	}
#if defined(WOH_PROFILE) && WOH_PROFILE
	if (dynamic) {
		IO_ADD(woh_prof_io_dynamic, t0);
		++woh_prof_dyn_misses;
	} else {
		IO_ADD(woh_prof_io_static, t0);
		++woh_prof_static_misses;
	}
#endif
	}
	if (got != (int)n)
		return dynamic ? ZVM_ESTATE : ZVM_EBOUNDS;
	c->pages[index].tag = tag;
	c->pages[index].dirty = 0;
	c->pages[index].valid = 1;
	*next = (uint8_t)(index - first + 1u);
	if (*next == count)
		*next = 0;
	*hot = index;
	*found = index;
	return ZVM_OK;
hit:
	COUNT_KIND(c, dynamic, dyn_hits, static_hits);
	*hot = index;
	*found = index;
	return ZVM_OK;
}

uint8_t *story_cache_page(void *user, uint32_t address, uint8_t write)
{
	story_cache_t *c = (story_cache_t *)user;
	uint8_t index;
	if (address >= (write ? (uint32_t)c->dynamic_size : c->reader.size))
		return 0;
	if ((c->dynamic_size & 511u) && (uint8_t)(address >> 9) == (uint8_t)(c->dynamic_size >> 9))
		return 0;
	if (find_page(c, address, &index) != ZVM_OK)
		return 0;
	if (write)
		c->pages[index].dirty = 1;
	return c->bytes + (uint16_t)index * STORY_CACHE_PAGE_SIZE;
}

zvm_error_t story_cache_read(void *user, uint32_t address, uint8_t *value)
{
	story_cache_t *c = (story_cache_t *)user;
	uint8_t index;
	zvm_error_t e;
	if (!value || address >= c->reader.size)
		return ZVM_EBOUNDS;
	e = find_page(c, address, &index);
	if (e)
		return e;
	*value = c->bytes[(uint16_t)index * STORY_CACHE_PAGE_SIZE + (uint16_t)(address & 511ul)];
	return ZVM_OK;
}

zvm_error_t story_cache_write(void *user, uint32_t address, uint8_t value)
{
	story_cache_t *c = (story_cache_t *)user;
	uint8_t index;
	zvm_error_t e;
	if (address >= c->dynamic_size)
		return ZVM_EREADONLY;
	e = find_page(c, address, &index);
	if (e)
		return e;
	c->bytes[(uint16_t)index * STORY_CACHE_PAGE_SIZE + (uint16_t)(address & 511ul)] = value;
	c->pages[index].dirty = 1;
	return ZVM_OK;
}

zvm_error_t story_cache_flush(story_cache_t *c)
{
#if DYN_CACHE_PAGES == 1
	return flush_page(c, 0);
#else
	uint8_t i;
	for (i = 0; i < DYN_CACHE_PAGES; ++i)
		if (flush_page(c, i) != ZVM_OK)
			return ZVM_ESTATE;
	return ZVM_OK;
#endif
}
