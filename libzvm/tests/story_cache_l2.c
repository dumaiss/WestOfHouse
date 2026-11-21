/* KonaOS test-host story backend: the second level.  Static pages kept in
 * one XMEM object, so a static miss copies from banked memory instead of
 * reading the source.  Only misses come here: this member need not be
 * resident.  A clock picks the entry a new page replaces. */
#include "story_cache.h"
#include "kos.h"

#if STORY_L2_PAGES
zvm_error_t story_cache_attach_l2(story_cache_t *c, unsigned int handle, uint8_t pages)
{
	if (!c || !handle || !pages || pages > STORY_L2_PAGES)
		return ZVM_EINVAL;
	c->l2_handle = handle;
	c->l2_count = pages;
	c->l2_hand = 0;
	memset(c->l2_tag, 255, sizeof(c->l2_tag));
	memset(c->l2_used, 0, sizeof(c->l2_used));
	return ZVM_OK;
}

int story_cache_l2_get(story_cache_t *c, uint8_t tag, uint8_t *dst, uint16_t n)
{
	uint8_t i;
	for (i = 0; i < c->l2_count; ++i)
		if (c->l2_tag[i] == tag) {
			c->l2_used[i] = 1;
			return xread(c->l2_handle, (uint16_t)i * STORY_CACHE_PAGE_SIZE, dst, n);
		}
	return 0;
}

void story_cache_l2_put(story_cache_t *c, uint8_t tag, const uint8_t *src, uint16_t n)
{
	uint8_t i;
	for (;;) {
		i = c->l2_hand;
		if (++c->l2_hand == c->l2_count)
			c->l2_hand = 0;
		if (!c->l2_used[i])
			break;
		c->l2_used[i] = 0;
	}
	/* A failed copy leaves the entry empty: the source still has the page. */
	c->l2_tag[i] = 255u;
	if (xwrite(c->l2_handle, (uint16_t)i * STORY_CACHE_PAGE_SIZE, src, n) == (int)n)
		c->l2_tag[i] = tag;
}
#else
typedef char story_cache_l2_unused;
#endif
