/* CPU-side cache probe. SDK services are bounded in-process stand-ins;
 * these measurements contain no OS syscall, IPC, media or banking cost. */
#include "kos.h"
#include "story_cache.h"

#define DYNAMIC_SIZE 11282u
#define SOURCE_SIZE 131070ul
#define CASES 8u
#define ITERATIONS 128u
extern void bench_begin(void);
extern void bench_end(void);
extern void backend_begin(void);
extern void backend_end(void);
uint8_t bench_id;
uint16_t failures, checksums[CASES];
uint16_t read_calls[CASES], write_calls[CASES], source_calls[CASES];
uint16_t read_bytes[CASES], write_bytes[CASES], source_bytes[CASES];
static uint16_t reads, writes, sources, rbytes, wbytes, sbytes;
static uint8_t backing[DYNAMIC_SIZE];
static story_cache_t cache;
static uint8_t abi_value;
static uint16_t abi_sp;

/* Differential probes exercise both a cold miss and a hot hit. Preserve the
 * caller's IX and compare callee cleanup against the exact saved SP. */
static void abi_read(void) __naked
{
	// clang-format off
 __asm
  push ix
  ld (_abi_sp),sp
  ld ix,#0x6a39
  ld de,#_abi_value
  push de
  ld de,#1
  push de
  ld de,#0
  push de
  ld hl,#_cache
  call _story_cache_read
  or a
  jr nz,00180$
  push ix
  pop hl
  ld de,#0x6a39
  or a
  sbc hl,de
  jr nz,00180$
  ld hl,#0
  add hl,sp
  ld de,(_abi_sp)
  or a
  sbc hl,de
  jr z,00181$
00180$:
  ld hl,(_failures)
  inc hl
  ld (_failures),hl
00181$:
  pop ix
  ret
 __endasm;
	// clang-format on
}

static uint8_t pattern(uint32_t address)
{
	return (uint8_t)(address ^ (address >> 9));
}

int xread(unsigned int handle, unsigned int offset, void *dst, unsigned int n)
{
	backend_begin();
	++reads;
	rbytes += n;
	if (handle != 17u || offset > DYNAMIC_SIZE || n > DYNAMIC_SIZE - offset || n > 512u) {
		++failures;
		backend_end();
		return -KOS_EINVAL;
	}
	memcpy(dst, backing + offset, n);
	backend_end();
	return (int)n;
}

int xwrite(unsigned int handle, unsigned int offset, const void *src, unsigned int n)
{
	backend_begin();
	++writes;
	wbytes += n;
	if (handle != 17u || offset > DYNAMIC_SIZE || n > DYNAMIC_SIZE - offset || n > 512u) {
		++failures;
		backend_end();
		return -KOS_EINVAL;
	}
	memcpy(backing + offset, src, n);
	backend_end();
	return (int)n;
}

static int source_read(void *context, uint32_t offset, void *dst, uint16_t n)
{
	uint16_t i;
	uint8_t *out = (uint8_t *)dst;
	(void)context;
	backend_begin();
	++sources;
	sbytes += n;
	if (offset > SOURCE_SIZE || n > SOURCE_SIZE - offset || n > 512u) {
		++failures;
		backend_end();
		return -KOS_EINVAL;
	}
	for (i = 0; i < n; ++i)
		out[i] = pattern(offset + i);
	backend_end();
	return (int)n;
}

static uint8_t read_byte(uint32_t address)
{
	uint8_t value = 0;
	if (story_cache_read(&cache, address, &value) != ZVM_OK)
		++failures;
	return value;
}

static void reset(void)
{
	kos_reader_t reader;
	uint16_t i;
	reader.context = 0;
	reader.read_at = source_read;
	reader.size = SOURCE_SIZE;
	if (story_cache_init(&cache, &reader, DYNAMIC_SIZE, 17) != ZVM_OK)
		++failures;
	for (i = 0; i < DYNAMIC_SIZE; ++i)
		backing[i] = pattern(i);
}

static void begin(uint8_t id)
{
	bench_id = id;
	reads = writes = sources = rbytes = wbytes = sbytes = 0;
	bench_begin();
}

static void end(uint16_t sum)
{
	bench_end();
	checksums[bench_id] = sum;
	read_calls[bench_id] = reads;
	write_calls[bench_id] = writes;
	source_calls[bench_id] = sources;
	read_bytes[bench_id] = rbytes;
	write_bytes[bench_id] = wbytes;
	source_bytes[bench_id] = sbytes;
}

void probe_main(void)
{
	uint16_t i, sum;
	uint8_t page, value;
	uint32_t at;
	static const uint32_t high[3] = {65535ul, 65536ul, SOURCE_SIZE - 1ul};
	reset();
	read_byte(0);
	sum = 0;
	begin(0);
	for (i = 0; i < ITERATIONS; ++i)
		sum += read_byte(i);
	end(sum);

	reset();
	read_byte(0);
	sum = 0;
	begin(1);
	for (i = 0; i < ITERATIONS; ++i) {
		value = (uint8_t)(i ^ 0xa5u);
		if (story_cache_write(&cache, i, value) != ZVM_OK)
			++failures;
		sum += value;
	}
	end(sum);
	if (story_cache_flush(&cache) != ZVM_OK)
		++failures;
	for (i = 0; i < ITERATIONS; ++i)
		if (backing[i] != (uint8_t)(i ^ 0xa5u))
			++failures;

	reset();
	for (i = 0; i < 4u; ++i)
		read_byte((uint32_t)i << 9);
	sum = 0;
	page = 0;
	begin(2);
	for (i = 0; i < ITERATIONS; ++i) {
		sum += read_byte(((uint32_t)page << 9) + i);
		++page;
		if (page == 4u)
			page = 0;
	}
	end(sum);

	reset();
	sum = 0;
	page = 0;
	begin(3);
	for (i = 0; i < ITERATIONS; ++i) {
		sum += read_byte(((uint32_t)page << 9) + i);
		++page;
		if (page == 5u)
			page = 0;
	}
	end(sum);

	reset();
	sum = 0;
	page = 0;
	begin(4);
	for (i = 0; i < ITERATIONS; ++i) {
		at = ((uint32_t)page << 9) + 13ul;
		value = (uint8_t)(i ^ 0xa5u);
		if (story_cache_write(&cache, at, value) != ZVM_OK)
			++failures;
		sum += read_byte(at);
		++page;
		if (page == 5u)
			page = 0;
	}
	end(sum);
	if (story_cache_flush(&cache) != ZVM_OK)
		++failures;
	for (page = 0; page < 5u; ++page) {
		/* Last iteration for each of the five slots in a 128-operation round. */
		i = (uint16_t)(125u + page);
		if (i >= ITERATIONS)
			i -= 5u;
		if (backing[((uint16_t)page << 9) + 13u] != (uint8_t)(i ^ 0xa5u))
			++failures;
	}

	reset();
	sum = 0;
	begin(5);
	for (i = 0; i < ITERATIONS; ++i)
		sum += read_byte(DYNAMIC_SIZE - 1ul);
	end(sum);

	reset();
	sum = 0;
	begin(6);
	for (i = 0; i < ITERATIONS; ++i)
		sum += read_byte(SOURCE_SIZE - 1ul);
	end(sum);

	reset();
	for (i = 0; i < 3u; ++i)
		read_byte(high[i]);
	sum = 0;
	page = 0;
	begin(7);
	for (i = 0; i < ITERATIONS; ++i) {
		sum += read_byte(high[page]);
		++page;
		if (page == 3u)
			page = 0;
	}
	end(sum);

	/* Outside the timing phases: target ABI, bounds and mixed logical pages. */
	reset();
	abi_read();
	abi_read();
	if (abi_value != pattern(65536ul))
		++failures;
	if (story_cache_read(&cache, SOURCE_SIZE, &value) != ZVM_EBOUNDS)
		++failures;
	if (story_cache_read(&cache, 0xfffffffful, &value) != ZVM_EBOUNDS)
		++failures;
	if (story_cache_read(&cache, 0, 0) != ZVM_EBOUNDS)
		++failures;
	at = 0;
	for (i = 0; i < 256u; ++i) {
		uint32_t probe = at ^ 65536ul;
		if (probe >= SOURCE_SIZE)
			probe = SOURCE_SIZE - 1;
		if (read_byte(probe) != pattern(probe))
			++failures;
		at += 509ul;
		if (at >= SOURCE_SIZE)
			at -= SOURCE_SIZE;
	}
}
