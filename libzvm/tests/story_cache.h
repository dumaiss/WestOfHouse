#ifndef ZVM_STORY_CACHE_H
#define ZVM_STORY_CACHE_H

#include "zvm.h"
#include "kos/reader.h"

#ifndef DYN_CACHE_PAGES
#define DYN_CACHE_PAGES 4
#endif
#ifndef STATIC_CACHE_PAGES
#define STATIC_CACHE_PAGES 4
#endif
#if (DYN_CACHE_PAGES != 1 && DYN_CACHE_PAGES != 4) ||                                              \
    (STATIC_CACHE_PAGES != 1 && STATIC_CACHE_PAGES != 4)
#error Story caches support one or four pages each
#endif
#define STORY_CACHE_PAGE_SIZE 512u
/* Second level: up to STORY_L2_PAGES static pages kept in one XMEM object
 * (at most 32 KiB) when the caller attaches one (story_cache_attach_l2). */
#ifndef STORY_L2_PAGES
#define STORY_L2_PAGES 64u
#endif
#if STORY_L2_PAGES > 64u
#error The second level is one XMEM object of at most 32 KiB
#endif
#define STORY_CACHE_PAGES (DYN_CACHE_PAGES + STATIC_CACHE_PAGES)

typedef struct {
	uint8_t tag, valid, dirty;
} story_cache_page_t;
#if defined(ZVM_PROFILE) && ZVM_PROFILE
typedef struct {
	uint32_t dyn_hits, dyn_misses, dyn_evictions, static_hits, static_misses, static_evictions;
	uint32_t xread_calls, xread_bytes, xwrite_calls, xwrite_bytes;
} story_cache_stats_t;
#endif
/* Test-application backend, not part of the public libzvm API.
 * Field order is an assembly ABI: see story_cache_z80.c and
 * docs/m35-cache-asm.md before changing this structure. */
typedef struct {
	kos_reader_t reader;
	uint16_t dynamic_size;
	unsigned int dynamic_handle;
	/* Kept contiguous: before VM initialization this is also startup scratch. */
	uint8_t bytes[STORY_CACHE_PAGES * STORY_CACHE_PAGE_SIZE];
	story_cache_page_t pages[STORY_CACHE_PAGES];
	uint8_t dyn_hot, static_hot, dyn_next, static_next;
	/* Changes whenever a page slot is refilled: the VM's page windows
	 * (zvm_set_page_access) are valid while it holds. */
	uint8_t generation;
#if STORY_L2_PAGES
	/* The second level (story_cache_l2.c): entry i holds page l2_tag[i]
	 * at offset i * 512; l2_used marks the entries the clock keeps. */
	unsigned int l2_handle;
	uint8_t l2_count, l2_hand;
	uint8_t l2_tag[STORY_L2_PAGES], l2_used[STORY_L2_PAGES];
#endif
#if defined(ZVM_PROFILE) && ZVM_PROFILE
	uint8_t profile_enabled;
	story_cache_stats_t stats;
#endif
} story_cache_t;

/* zvm_memory_page_fn over the cache: the resident slot holding `address`,
 * marked dirty for a write, or NULL for a static page written, for the page
 * straddling the dynamic/static boundary (two slots hold its two parts) and
 * on a failed fill. Pass &cache->generation. */
uint8_t *story_cache_page(void *user, uint32_t address, uint8_t write);
/* Borrows both reader and XMEM handle; never closes/frees either. */
zvm_error_t story_cache_init(story_cache_t *cache, const kos_reader_t *reader,
			     uint16_t dynamic_size, unsigned int dynamic_handle);
/* Lend the cache an XMEM object of `pages` * 512 bytes for static pages
 * (after init, before prepare; borrowed, never freed). Without one the
 * cache reads every static miss from the source. */
zvm_error_t story_cache_attach_l2(story_cache_t *cache, unsigned int handle, uint8_t pages);
/* Stage writable bytes and checksum the immutable source, using invalid cache
 * storage as scratch; static pages also fill the second level, in order,
 * while it has room. Call once before allowing any VM read or write. */
zvm_error_t story_cache_prepare(story_cache_t *cache, uint32_t story_end, uint16_t *checksum);
zvm_error_t story_cache_read(void *cache, uint32_t address, uint8_t *value);
zvm_error_t story_cache_write(void *cache, uint32_t address, uint8_t value);
#if STORY_L2_PAGES
/* The second level (story_cache_l2.c): copy page `tag` (n bytes) to dst:
 * n when held, 0 when not, or a negative error; put stores a page just read
 * from the source in the clock's victim entry. */
int story_cache_l2_get(story_cache_t *cache, uint8_t tag, uint8_t *dst, uint16_t n);
void story_cache_l2_put(story_cache_t *cache, uint8_t tag, const uint8_t *src, uint16_t n);
#endif
/* On error, the failed page and every unflushed dirty page remain available. */
zvm_error_t story_cache_flush(story_cache_t *cache);

#endif
