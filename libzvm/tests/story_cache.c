/* KonaOS test-host story backend: immutable reader plus writable XMEM.
 * Separate cache partitions handle pages straddling the static-memory boundary. */
#include "story_cache.h"
#include "kos.h"

/* Cache-slot sentinel only. A page tag of 255 is a valid story address tag. */
#define NO_PAGE 255u
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
	COUNT(c, xwrite_calls, 1);
	COUNT(c, xwrite_bytes, n);
	return xwrite(c->dynamic_handle, offset, src, n);
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

zvm_error_t story_cache_init(story_cache_t *c, const kos_reader_t *reader, uint16_t dynamic_size,
			     unsigned int dynamic_handle)
{
	if (!c || !reader || !reader->read_at || !dynamic_size || dynamic_size > 32768u ||
	    dynamic_size > reader->size || reader->size > 131070ul || !dynamic_handle)
		return ZVM_EINVAL;
	/* Copy first so borrowing c->reader itself is also supported. */
	c->reader = *reader;
	c->dynamic_size = dynamic_size;
	c->dynamic_handle = dynamic_handle;
	memset(c->pages, 0, sizeof(c->pages));
	c->dyn_hot = c->static_hot = NO_PAGE;
	c->dyn_next = c->static_next = 0;
#if defined(ZVM_PROFILE) && ZVM_PROFILE
	c->profile_enabled = 0;
	memset(&c->stats, 0, sizeof(c->stats));
#endif
	return ZVM_OK;
}

zvm_error_t story_cache_prepare(story_cache_t *c, uint32_t story_end, uint16_t *checksum)
{
	uint32_t at;
	uint16_t n, i, sum = 0;
	uint16_t capacity = (uint16_t)sizeof(c->bytes);
	if (!checksum || story_end < 64ul || story_end > c->reader.size)
		return ZVM_EINVAL;
	/* Scratch must never overwrite a live (possibly dirty) page. */
	for (i = 0; i < STORY_CACHE_PAGES; ++i)
		if (c->pages[i].valid)
			return ZVM_ESTATE;
	for (at = 0; at < c->dynamic_size; at += n) {
		n = (uint16_t)(c->dynamic_size - at > capacity ? capacity : c->dynamic_size - at);
		if (kos_reader_read_full(&c->reader, at, c->bytes, n) != (int)n ||
		    dynamic_write(c, (uint16_t)at, c->bytes, n) != (int)n)
			return ZVM_ESTATE;
	}
	/* Read the original source rather than dynamic memory changed by the VM. */
	for (at = 64; at < story_end; at += n) {
		n = (uint16_t)(story_end - at > capacity ? capacity : story_end - at);
		if (kos_reader_read_full(&c->reader, at, c->bytes, n) != (int)n)
			return ZVM_EBOUNDS;
		for (i = 0; i < n; ++i)
			sum = (uint16_t)(sum + c->bytes[i]);
	}
	*checksum = sum;
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
	if (*hot == index)
		*hot = NO_PAGE;
	start = (uint32_t)tag << 9;
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
		got = kos_reader_read_full(&c->reader, start,
					   c->bytes + (uint16_t)index * STORY_CACHE_PAGE_SIZE, n);
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
