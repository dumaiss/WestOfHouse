/* KonaOS test-host story backend: setup.  Kept apart from story_cache.c,
 * whose read and write paths a placed program keeps resident; these run
 * once, before the VM reads. */
#include "story_cache.h"
#include "kos.h"

#define NO_PAGE 255u
#if defined(ZVM_PROFILE) && ZVM_PROFILE
#define COUNT(c, field, n)                                                                         \
	do {                                                                                       \
		if ((c)->profile_enabled)                                                          \
			(c)->stats.field += (n);                                                   \
	} while (0)
#else
#define COUNT(c, field, n) ((void)0)
#endif

static int dynamic_write(story_cache_t *c, uint16_t offset, const void *src, uint16_t n)
{
	COUNT(c, xwrite_calls, 1);
	COUNT(c, xwrite_bytes, n);
	return xwrite(c->dynamic_handle, offset, src, n);
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
#if STORY_L2_PAGES
	c->l2_handle = 0;
	c->l2_count = 0;
#endif
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
#if STORY_L2_PAGES
	uint8_t l2;
#endif
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
	/* Read the original source rather than dynamic memory changed by the VM:
	 * whole pages from the start, so the static ones can fill the second
	 * level. The checksum covers [64, story_end). */
#if STORY_L2_PAGES
	l2 = 0;
#endif
	for (at = 0; at < c->reader.size && (at < story_end
#if STORY_L2_PAGES
	     || l2 < c->l2_count
#endif
	     ); at += n) {
		n = (uint16_t)(c->reader.size - at > capacity ? capacity : c->reader.size - at);
		if (kos_reader_read_full(&c->reader, at, c->bytes, n) != (int)n)
			return ZVM_EBOUNDS;
		for (i = 0; i < n; ++i)
			if (at + i >= 64ul && at + i < story_end)
				sum = (uint16_t)(sum + c->bytes[i]);
#if STORY_L2_PAGES
		/* Pages from the first one holding static bytes. */
		for (i = 0; i < n && l2 < c->l2_count; i += STORY_CACHE_PAGE_SIZE) {
			uint16_t len = (uint16_t)(n - i);
			if (len > STORY_CACHE_PAGE_SIZE)
				len = STORY_CACHE_PAGE_SIZE;
			if (at + i + len <= c->dynamic_size)
				continue;
			if (xwrite(c->l2_handle, (uint16_t)l2 * STORY_CACHE_PAGE_SIZE, c->bytes + i, len) != (int)len)
				break;
			c->l2_tag[l2++] = (uint8_t)((at + i) >> 9);
		}
#endif
	}
	*checksum = sum;
	return ZVM_OK;
}
