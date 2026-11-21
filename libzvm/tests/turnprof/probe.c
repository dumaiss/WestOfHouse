/* Turn profiler, Z80 side: the real VM and story cache run Zork turns in
 * the simulator.  Story and XMEM transfers trap to the host (turnprof.c),
 * which serves them in zero T-states, so the profile is CPU only. */
#include "kos.h"
#include "zvm.h"
#include "story_cache.h"

volatile uint8_t host_op;
volatile uint16_t host_ptr, host_len;
volatile uint32_t host_off;
volatile int16_t host_result;
volatile uint8_t turn_id;
/* djb2 over the turn's narrative output: the host prints it per turn. */
volatile uint32_t output_hash;
void host_trap(void);
void turn_begin(void);
void turn_end(void);
void probe_fail(void);

static story_cache_t cache;
static zvm_t vm;
static uint8_t header[64];

static const char *const commands[] = {
	"look", "inventory", "north", "east", "open window", "enter house",
	"west", "east", "west", "up", "down", "east", "out", 0
};

static int host(uint8_t op, uint32_t offset, void *buffer, uint16_t length)
{
	host_op = op;
	host_off = offset;
	host_ptr = (uint16_t)buffer;
	host_len = length;
	host_trap();
	return host_result;
}

static int story_read_at(void *context, uint32_t offset, void *out, uint16_t length)
{
	(void)context;
	return host(1u, offset, out, length);
}

int xread(unsigned int handle, unsigned int offset, void *buffer, unsigned int length)
{
	(void)handle;
	return host(2u, offset, buffer, (uint16_t)length);
}

int xwrite(unsigned int handle, unsigned int offset, const void *buffer, unsigned int length)
{
	(void)handle;
	return host(3u, offset, (void *)buffer, (uint16_t)length);
}

static zvm_error_t output(void *user, uint8_t channel, uint8_t byte)
{
	(void)user;
	if (channel == ZVM_OUTPUT_NARRATIVE)
		output_hash = (output_hash << 5) + output_hash + byte;
	return ZVM_OK;
}

void probe_main(void)
{
	uint16_t dynamic, checksum;
	uint32_t end;
	zvm_status_t status;
	uint8_t i;

	cache.reader.read_at = story_read_at;
	cache.reader.context = 0;
	(void)host(0u, 0u, 0, 0u);         /* the host answers the size */
	cache.reader.size = host_off;
	if (story_read_at(0, 0u, header, 64u) != 64) { probe_fail(); return; }
	dynamic = ((uint16_t)header[14] << 8) | header[15];
	end = ((uint32_t)header[26] << 9) | ((uint32_t)header[27] << 1);
	if (story_cache_init(&cache, &cache.reader, dynamic, 1u) != ZVM_OK ||
	    story_cache_prepare(&cache, end, &checksum) != ZVM_OK ||
	    zvm_init_memory(&vm, story_cache_read, story_cache_write, &cache,
			    cache.reader.size) != ZVM_OK ||
	    zvm_set_story_checksum(&vm, checksum) != ZVM_OK ||
	    zvm_set_output_callback(&vm, output, 0) != ZVM_OK
#ifndef TURNPROF_NO_PAGE
	    || zvm_set_page_access(&vm, story_cache_page, &cache.generation) != ZVM_OK
#endif
	    ) {
		probe_fail();
		return;
	}
	for (i = 0;; ++i) {
		turn_id = i;
		output_hash = 5381ul;
		turn_begin();
		do
			status = zvm_run(&vm, 50u);
		while (status == ZVM_YIELDED || status == ZVM_READY);
		turn_end();
		if (status != ZVM_NEED_INPUT || !commands[i])
			break;
		if (zvm_submit_input(&vm, commands[i]) != ZVM_OK) {
			probe_fail();
			return;
		}
	}
}
