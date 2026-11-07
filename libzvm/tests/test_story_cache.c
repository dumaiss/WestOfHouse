/* Native cache-only tests. Link story_cache.c and the SDK reader_full.c;
 * xread/xwrite below are the only replacement SDK services. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "story_cache.h"

#define SOURCE_SIZE 131070ul
#define DYNAMIC_SIZE 11282u
static uint8_t source[SOURCE_SIZE], original[SOURCE_SIZE];
static uint8_t backing[32768], expected[32768];
static story_cache_t cache;
static uint32_t random_state = 1;
static unsigned int reads, writes, source_reads, max_read, max_write;
static int fail_read, fail_write, fail_source, short_source;

int xread(unsigned int handle, unsigned int offset, void *dst, unsigned int n)
{
	assert(handle == 17u && offset + n <= DYNAMIC_SIZE && n <= 512u);
	++reads;
	if (fail_read) {
		fail_read = 0;
		memset(dst, 0xe5, n / 2u);
		return -5;
	}
	memcpy(dst, backing + offset, n);
	return (int)n;
}

int xwrite(unsigned int handle, unsigned int offset, const void *src, unsigned int n)
{
	assert(handle == 17u && offset + n <= DYNAMIC_SIZE && n <= 4096u);
	++writes;
	if (n > max_write)
		max_write = n;
	if (fail_write) {
		fail_write = 0;
		memcpy(backing + offset, src, n / 2u);
		return -5;
	}
	memcpy(backing + offset, src, n);
	return (int)n;
}

static int read_at(void *context, uint32_t offset, void *dst, uint16_t n)
{
	(void)context;
	assert(offset + n <= SOURCE_SIZE);
	++source_reads;
	if (n > max_read)
		max_read = n;
	if (fail_source) {
		fail_source = 0;
		memset(dst, 0xe7, n / 2u);
		return -5;
	}
	if (short_source && n > 73u)
		n = 73u;
	memcpy(dst, source + offset, n);
	return (int)n;
}

static void reset(void)
{
	kos_reader_t reader;
	uint16_t sum, reference = 0;
	uint32_t i;
	reads = writes = source_reads = max_read = max_write = 0;
	fail_read = fail_write = fail_source = 0;
	short_source = 1;
	for (i = 0; i < SOURCE_SIZE; ++i)
		source[i] = (uint8_t)(i ^ (i >> 7) ^ (i >> 12));
	memcpy(original, source, sizeof(source));
	memset(backing, 0xcc, sizeof(backing));
	reader.context = 0;
	reader.read_at = read_at;
	reader.size = SOURCE_SIZE;
	assert(story_cache_init(&cache, &reader, DYNAMIC_SIZE, 17) == ZVM_OK);
#if defined(ZVM_PROFILE) && ZVM_PROFILE
	cache.profile_enabled = 1;
#endif
	assert(story_cache_prepare(&cache, SOURCE_SIZE, &sum) == ZVM_OK);
	for (i = 64; i < SOURCE_SIZE; ++i)
		reference = (uint16_t)(reference + source[i]);
	assert(sum == reference);
	assert(!memcmp(backing, source, DYNAMIC_SIZE));
	memcpy(expected, source, DYNAMIC_SIZE);
	assert(max_read == sizeof(cache.bytes) && max_write == sizeof(cache.bytes));
	assert(backing[DYNAMIC_SIZE] == 0xcc);
	short_source = 0;
}

static uint8_t read_byte(uint32_t at)
{
	uint8_t value = 0;
	assert(story_cache_read(&cache, at, &value) == ZVM_OK);
	return value;
}

static uint32_t random_next(void)
{
	random_state = random_state * 1664525ul + 1013904223ul;
	return random_state;
}

static void boundaries(void)
{
	uint8_t value = 0;
	uint32_t points[] = {0,	    511,   512,	  10752, DYNAMIC_SIZE - 1, DYNAMIC_SIZE, 11775,
			     11776, 65535, 65536, 65537, SOURCE_SIZE - 1};
	unsigned int i;
	uint16_t sum;
	reset();
	for (i = 0; i < sizeof(points) / sizeof(points[0]); ++i)
		assert(read_byte(points[i]) == source[points[i]]);
	assert(story_cache_read(&cache, SOURCE_SIZE, &value) == ZVM_EBOUNDS);
	assert(story_cache_read(&cache, 0xfffffffful, &value) == ZVM_EBOUNDS);
	assert(story_cache_write(&cache, DYNAMIC_SIZE, 7) == ZVM_EREADONLY);
	assert(story_cache_write(&cache, 65536ul, 7) == ZVM_EREADONLY);
	assert(story_cache_write(&cache, DYNAMIC_SIZE - 1, 0x93) == ZVM_OK);
	assert(read_byte(DYNAMIC_SIZE - 1) == 0x93);
	assert(read_byte(DYNAMIC_SIZE) == source[DYNAMIC_SIZE]);
	assert(story_cache_flush(&cache) == ZVM_OK);
	assert(backing[DYNAMIC_SIZE - 1] == 0x93 && backing[DYNAMIC_SIZE] == 0xcc);
	assert(!memcmp(source, original, sizeof(source)));
	assert(story_cache_prepare(&cache, SOURCE_SIZE, &sum) == ZVM_ESTATE);
}

static void associative(void)
{
	unsigned int i, before;
	reset();
	for (i = 0; i < DYN_CACHE_PAGES; ++i)
		assert(read_byte((uint32_t)i * 512) == source[i * 512]);
	before = reads;
	for (i = 0; i < 80; ++i)
		read_byte((uint32_t)(i % DYN_CACHE_PAGES) * 512);
	assert(reads == before);
	for (i = 0; i < STATIC_CACHE_PAGES; ++i)
		read_byte(65536ul + (uint32_t)i * 512);
	before = source_reads;
	for (i = 0; i < 80; ++i)
		read_byte(65536ul + (uint32_t)(i % STATIC_CACHE_PAGES) * 512);
	assert(source_reads == before);
}

static void randomized(void)
{
	unsigned int i;
	uint32_t address;
	uint8_t value;
	reset();
	for (i = 0; i < 50000u; ++i) {
		address = random_next() % SOURCE_SIZE;
		if ((i & 3u) == 0) {
			address %= DYNAMIC_SIZE;
			value = (uint8_t)(random_next() >> 16);
			assert(story_cache_write(&cache, address, value) == ZVM_OK);
			expected[address] = value;
		} else
			assert(read_byte(address) ==
			       (address < DYNAMIC_SIZE ? expected[address] : source[address]));
		if (i % 1009u == 0) {
			assert(story_cache_flush(&cache) == ZVM_OK);
			assert(!memcmp(backing, expected, DYNAMIC_SIZE));
		}
	}
	assert(story_cache_flush(&cache) == ZVM_OK);
	assert(!memcmp(backing, expected, DYNAMIC_SIZE));
	assert(!memcmp(source, original, sizeof(source)));
	assert(backing[DYNAMIC_SIZE] == 0xcc);
}

static void failures(void)
{
	uint8_t value = 0;
	unsigned int i, before;
	reset();
	assert(story_cache_write(&cache, 0, 0xa7) == ZVM_OK);
	for (i = 1; i < DYN_CACHE_PAGES; ++i)
		read_byte((uint32_t)i * 512);
	fail_write = 1;
	assert(story_cache_read(&cache, (uint32_t)DYN_CACHE_PAGES * 512, &value) == ZVM_ESTATE);
	assert(read_byte(0) == 0xa7); /* failed replacement retained dirty victim */
	fail_write = 1;
	assert(story_cache_flush(&cache) == ZVM_ESTATE);
	assert(read_byte(0) == 0xa7);
	assert(story_cache_flush(&cache) == ZVM_OK);
	assert(backing[0] == 0xa7);
	fail_read = 1;
	assert(story_cache_read(&cache, (uint32_t)DYN_CACHE_PAGES * 512, &value) == ZVM_ESTATE);
	before = reads;
	assert(read_byte(0) == 0xa7);
	assert(reads == before + 1u);
	assert(read_byte((uint32_t)DYN_CACHE_PAGES * 512) == source[DYN_CACHE_PAGES * 512]);
	/* A failed fill after successful dirty writeback must retain the data in
	 * backing, and the damaged in-progress buffer must not remain valid. */
	reset();
	assert(story_cache_write(&cache, 0, 0x91) == ZVM_OK);
	for (i = 1; i < DYN_CACHE_PAGES; ++i)
		read_byte((uint32_t)i * 512);
	fail_read = 1;
	assert(story_cache_read(&cache, (uint32_t)DYN_CACHE_PAGES * 512, &value) == ZVM_ESTATE);
	assert(backing[0] == 0x91 && read_byte(0) == 0x91);
	reset();
	for (i = 0; i < STATIC_CACHE_PAGES; ++i)
		read_byte(65536ul + (uint32_t)i * 512);
	fail_source = 1;
	assert(story_cache_read(&cache, 65536ul + (uint32_t)STATIC_CACHE_PAGES * 512, &value) ==
	       ZVM_EBOUNDS);
	before = source_reads;
	assert(read_byte(65536ul) == source[65536]);
	assert(source_reads == before + 1u);
	assert(read_byte(65536ul + (uint32_t)STATIC_CACHE_PAGES * 512) ==
	       source[65536ul + (uint32_t)STATIC_CACHE_PAGES * 512]);
	assert(!memcmp(source, original, sizeof(source)));
}

int main(void)
{
	boundaries();
	associative();
	randomized();
	failures();
	printf("story_cache: PASS dynamic=%u static=%u storage=%lu bytes\n", DYN_CACHE_PAGES,
	       STATIC_CACHE_PAGES, (unsigned long)sizeof(cache.bytes));
	return 0;
}
