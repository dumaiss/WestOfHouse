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
#if defined(ZVM_PROFILE) && ZVM_PROFILE
	uint8_t profile_enabled;
	story_cache_stats_t stats;
#endif
} story_cache_t;

/* Borrows both reader and XMEM handle; never closes/frees either. */
zvm_error_t story_cache_init(story_cache_t *cache, const kos_reader_t *reader,
			     uint16_t dynamic_size, unsigned int dynamic_handle);
/* Stage writable bytes and checksum the immutable source, using invalid cache
 * storage as scratch. Call once before allowing any VM read or write. */
zvm_error_t story_cache_prepare(story_cache_t *cache, uint32_t story_end, uint16_t *checksum);
zvm_error_t story_cache_read(void *cache, uint32_t address, uint8_t *value);
zvm_error_t story_cache_write(void *cache, uint32_t address, uint8_t value);
/* On error, the failed page and every unflushed dirty page remain available. */
zvm_error_t story_cache_flush(story_cache_t *cache);

#endif
