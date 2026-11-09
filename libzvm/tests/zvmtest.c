/* KonaOS test application: synthetic fixtures, story loading, console adapter
 * and benchmark reporting. All platform I/O stays outside the VM core. */
#include "zvm.h"
#include "zvm_profile.h"
#include "kos/timer.h"
#include "kos.h"
#include "kos/reader.h"
#include "story_cache.h"
#include "stack_probe.h"
#include "state_report.h"
#include <diag.h>
#include "klibc/stdio.h"

/* Adapter aggregates require ZVM_PROFILE=1 and benchmark mode. Counts describe
 * requested transfers; failures are reported by the existing adapter paths. */
static zvm_t vm;
static story_cache_t play_story;
static kos_handle_source_t play_source;
static uint8_t benchmarking, discard_output;
static int bench_timer;
static uint32_t bench_start;
static uint8_t bench_valid;
static uint32_t output_count, output_hash;
static uint16_t final_room, final_score, final_moves;

static void bench_resources(void)
{
	struct kos_xmem_info info;
	struct kos_bank_info banks;
	if (xinfo(play_story.dynamic_handle, &info) == 0)
		printf("\rresource: xmem_requested=%u allocated=%u flags=%u type=%u\r\n",
		       (unsigned)info.requested, (unsigned)info.allocated, (unsigned)info.flags,
		       (unsigned)info.type);
	printf("\rresource: cache_bytes=%u adapter_bytes=%u\r\n",
	       (unsigned)sizeof(play_story.bytes), (unsigned)sizeof(play_story));
	if (diag_bank_info(&banks) == 2)
		printf("\rresource: process_bank=%u valid_bank_mask=%u\r\n", (unsigned)banks.bank,
		       (unsigned)banks.mask);
}
#if defined(ZVM_PROFILE) && ZVM_PROFILE
static struct {
	uint32_t reader_calls, reader_bytes, fread_calls, fread_bytes;
} adapter;

#define HOST_COUNT(field, n)                                                                       \
	do {                                                                                       \
		if (benchmarking)                                                                  \
			adapter.field += (n);                                                      \
	} while (0)
static kos_read_at_fn original_read_at;

static int measured_read_at(void *context, uint32_t a, void *b, uint16_t n)
{
	HOST_COUNT(reader_calls, 1);
	HOST_COUNT(reader_bytes, n);
	return original_read_at(context, a, b, n);
}
#else
#define HOST_COUNT(field, n) ((void)0)
#endif
static void bench_begin(void)
{
	bench_valid = (uint8_t)(bench_timer >= 0 && timer_now(bench_timer, &bench_start) == 0);
}
#if defined(ZVM_PROFILE) && ZVM_PROFILE
static void bench_counter(const char *phase, uint8_t turn, const char *key, uint32_t value)
{
	printf("\rbench: mode=%s phase=%s turn=%u %s=%lu\r\n",
	       discard_output ? "discard" : "normal", phase, (unsigned)turn, key,
	       (unsigned long)value);
}

static void bench_total(const char *key, uint32_t value)
{
	printf("\rbench: mode=%s final %s=%lu\r\n", discard_output ? "discard" : "normal", key,
	       (unsigned long)value);
}

static void profile_total(const char *key, uint32_t value)
{
	printf("\rprofile: mode=%s %s=%lu\r\n", discard_output ? "discard" : "normal", key,
	       (unsigned long)value);
}
#endif
static void bench_end(const char *phase, uint8_t turn)
{
	uint32_t end = 0, counts = 0;
	uint8_t valid = bench_valid;
	if (bench_timer < 0 || timer_now(bench_timer, &end) < 0)
		valid = 0;
	if (valid) {
		counts = end - bench_start;
		if (!counts || counts > 167772159ul)
			valid = 0;
	}
	/* Finish narrative transport before any diagnostics; flush is included in
	 * the measured phase by callers, so no buffered narrative crosses phases. */
	if (valid)
		printf("\rbench: mode=%s phase=%s turn=%u counts=%lu us=%lu\r\n",
		       discard_output ? "discard" : "normal", phase, (unsigned)turn,
		       (unsigned long)counts, (unsigned long)timer_us(counts));
	else
		printf("\rbench: mode=%s phase=%s turn=%u unavailable\r\n",
		       discard_output ? "discard" : "normal", phase, (unsigned)turn);
#if defined(ZVM_PROFILE) && ZVM_PROFILE
#define SNAP(field) bench_counter(phase, turn, #field, play_story.stats.field)
	SNAP(xread_calls);
	SNAP(xread_bytes);
	SNAP(xwrite_calls);
	SNAP(xwrite_bytes);
#undef SNAP
#define SNAP(field) bench_counter(phase, turn, #field, adapter.field)
	SNAP(reader_calls);
	SNAP(reader_bytes);
	SNAP(fread_calls);
	SNAP(fread_bytes);
#undef SNAP
	bench_counter(phase, turn, "instructions", zvm_profile_get(&vm)->instructions);
#endif
}

static void bench_report(void)
{
#if defined(ZVM_PROFILE) && ZVM_PROFILE
	const zvm_profile_t *p = zvm_profile_get(&vm);
#endif
	printf("\rbench: mode=%s final room=%u score=%u moves=%u\r\n",
	       discard_output ? "discard" : "normal", (unsigned)final_room, (unsigned)final_score,
	       (unsigned)final_moves);
	printf("\rbench: mode=%s final output_bytes=%lu hash=%lu\r\n",
	       discard_output ? "discard" : "normal", (unsigned long)output_count,
	       (unsigned long)output_hash);
#if defined(ZVM_STACK_PROBE) && ZVM_STACK_PROBE
	printf("\rbench: mode=%s final written_stack_lower_bound=%u guard_ok=%u\r\n",
	       discard_output ? "discard" : "normal", stack_probe_written(),
	       (unsigned)stack_probe_guard_ok());
#endif
#if defined(ZVM_PROFILE) && ZVM_PROFILE
#define TOTAL(field) bench_total(#field, play_story.stats.field)
	TOTAL(dyn_hits);
	TOTAL(dyn_misses);
	TOTAL(dyn_evictions);
	TOTAL(static_hits);
	TOTAL(static_misses);
	TOTAL(static_evictions);
#undef TOTAL
#define REPORT(field) profile_total(#field, p->field)
	REPORT(instructions);
	REPORT(op_0op);
	REPORT(op_1op);
	REPORT(op_2op);
	REPORT(op_var);
	REPORT(calls);
	REPORT(returns);
	REPORT(branches);
	REPORT(branches_taken);
	REPORT(yields);
	REPORT(decoder_fetches);
	REPORT(byte_reads);
	REPORT(word_reads);
	REPORT(byte_writes);
	REPORT(word_writes);
	REPORT(text_chars);
	REPORT(abbreviations);
	REPORT(dictionary_lookups);
	REPORT(dictionary_comparisons);
	REPORT(output_calls);
	REPORT(output_bytes);
#undef REPORT
#endif
	console_flush();
}

static uint8_t story[1024];
static uint8_t initial_story[1024];

typedef struct {
	uint8_t bytes[1024];
} paged_story_t;

static paged_story_t paged_story;

static char play_line[82];
static woh_state_report_t state_reporting;
static char binding_path[96];
static uint8_t binding_bytes[WOH_BINDINGS_SIZE];
static uint8_t binding_header[WOH_STORY_HEADER_SIZE];

/* A story and its same-stem .WOH file are deployed together. Absence supports
 * uninstrumented stories; a present but invalid file is a load error. */
static int load_state_bindings(const char *path)
{
	uint8_t n = 0, dot = 255u, used = 0, extra;
	int handle, got;
	state_reporting.enabled = 0;
	while (path[n]) {
		if (n >= sizeof(binding_path) - 5u)
			return -1;
		binding_path[n] = path[n];
		if (path[n] == '/')
			dot = 255u;
		else if (path[n] == '.')
			dot = n;
		++n;
	}
	if (dot != 255u)
		n = dot;
	binding_path[n++] = '.';
	binding_path[n++] = 'W';
	binding_path[n++] = 'O';
	binding_path[n++] = 'H';
	binding_path[n] = 0;
	handle = open(binding_path, KFS_O_READ);
	if (handle < 0)
		return 0;
	while (used < WOH_BINDINGS_SIZE) {
		got = read((unsigned int)handle, binding_bytes + used, WOH_BINDINGS_SIZE - used);
		if (got <= 0) {
			close((unsigned int)handle);
			return -1;
		}
		used += (uint8_t)got;
	}
	got = read((unsigned int)handle, &extra, 1);
	close((unsigned int)handle);
	if (got != 0 || kos_reader_read_full(&play_story.reader, 0, binding_header,
					    WOH_STORY_HEADER_SIZE) != WOH_STORY_HEADER_SIZE ||
	    !woh_state_report_init(&state_reporting, binding_bytes, used, binding_header))
		return -1;
	return 1;
}


static zvm_error_t play_output(void *user, uint8_t channel, uint8_t byte)
{
	(void)user;
	(void)channel;
	if (benchmarking) {
		++output_count;
		output_hash = ((output_hash << 5) + output_hash) ^ byte;
		if (discard_output)
			return ZVM_OK;
	}
	if (byte == '\n' && putchar('\r') < 0)
		return ZVM_ESTATE;
	return putchar(byte) < 0 ? ZVM_ESTATE : ZVM_OK;
}

static zvm_error_t play_status(void *user, uint16_t room, uint16_t value1, uint16_t value2,
			       uint8_t time_mode)
{
	(void)user;
	if (benchmarking) {
		final_room = room;
		final_score = value1;
		final_moves = value2;
		if (discard_output)
			return ZVM_OK;
	}
	if (time_mode)
		printf(" | %u:%02u\r\n", (unsigned)value1, (unsigned)value2);
	else
		printf(" | %u moves %u\r\n", (unsigned)value1, (unsigned)value2);
	return console_flush() < 0 ? ZVM_ESTATE : ZVM_OK;
}

static int play_cleanup(int file)
{
	int failed = 0;
	if (story_cache_flush(&play_story) != ZVM_OK) {
		printf("\rzvmtest: dynamic flush failed\r\n");
		failed = 1;
	}
	if (play_story.dynamic_handle) {
		xfree(play_story.dynamic_handle);
		play_story.dynamic_handle = 0;
	}
	if (file >= 0)
		close((unsigned int)file);
	return failed;
}

static int play_zork(const char *path, uint8_t automated)
{
	int file;
	uint8_t header[4];
	uint16_t dynamic, checksum = 0;
	uint32_t story_end;
	int result;
	static const char commands[10][16] = {
	    "look",  "open mailbox", "take leaflet",	"read leaflet",	 "inventory",
	    "score", "look",	     "examine mailbox", "close mailbox", "open mailbox"};
	uint8_t command_index = 0;
#if defined(ZVM_STACK_PROBE) && ZVM_STACK_PROBE
	if (benchmarking)
		stack_probe_mark();
#endif
	if (benchmarking) {
		zvm_release(&vm);
#if defined(ZVM_PROFILE) && ZVM_PROFILE
		memset(&adapter, 0, sizeof(adapter));
#endif
		output_count = 0;
		output_hash = 5381ul;
		final_room = final_score = final_moves = 0;
		bench_timer = timer_open();
		printf("\rbench: mode=%s profile=%u\r\n", discard_output ? "discard" : "normal",
#if defined(ZVM_PROFILE) && ZVM_PROFILE
		       1u
#else
		       0u
#endif
		);
#if !defined(ZVM_PROFILE) || !ZVM_PROFILE
		printf("\rbench: mode=%s adapter_counters=unavailable\r\n",
		       discard_output ? "discard" : "normal");
#endif
		console_flush();
		bench_begin();
		bench_end("timer-pair", 0);
		console_flush();
		bench_begin();
	}
	if (!benchmarking)
		printf("zvmtest: opening story source\n");
	file = open(path, KFS_O_READ);
	if (file < 0) {
		printf("zvmtest: cannot open %s\n", path);
		return 1;
	}
	play_source.handle = (unsigned int)file;
	if (kos_reader_from_handle(&play_story.reader, &play_source) < 0) {
		close((unsigned int)file);
		return 1;
	}
	if (load_state_bindings(path) < 0) {
		printf("zvmtest: invalid story state bindings\n");
		close((unsigned int)file);
		return 1;
	}
#if defined(ZVM_PROFILE) && ZVM_PROFILE
	if (benchmarking) {
		original_read_at = play_story.reader.read_at;
		play_story.reader.read_at = measured_read_at;
	}
#endif
	if (play_story.reader.size > 131070ul ||
	    kos_reader_read_full(&play_story.reader, 14ul, header, 2) != 2 ||
	    kos_reader_read_full(&play_story.reader, 0x1aul, header + 2, 2) != 2) {
		printf("zvmtest: invalid story source\n");
		close((unsigned int)file);
		return 1;
	}
	dynamic = (uint16_t)(((uint16_t)header[0] << 8) | header[1]);
	story_end = ((uint32_t)header[2] << 9) | ((uint32_t)header[3] << 1);
	if (story_end > play_story.reader.size || story_end < 64ul) {
		printf("zvmtest: invalid declared story size\n");
		close((unsigned int)file);
		return 1;
	}
	if (!benchmarking)
		printf("zvmtest: story %lu bytes, dynamic %u\n",
		       (unsigned long)play_story.reader.size, (unsigned)dynamic);
	if (!dynamic || dynamic > KOS_XMEM_MAX_OBJECT || dynamic > play_story.reader.size) {
		printf("zvmtest: dynamic memory %u exceeds XMEM object limit\n", (unsigned)dynamic);
		close((unsigned int)file);
		return 1;
	}
	if (!benchmarking)
		printf("zvmtest: allocating %u bytes of XMEM\n", (unsigned)dynamic);
	result = xalloc(dynamic, KOS_XMEM_FLAG_NONE);
	if (!benchmarking)
		printf("zvmtest: XMEM allocation result %d\n", result);
	if (result < 1) {
		printf("zvmtest: XMEM allocation failed %d\n", result);
		close((unsigned int)file);
		return 1;
	}
	if (story_cache_init(&play_story, &play_story.reader, dynamic, (unsigned int)result) !=
	    ZVM_OK) {
		xfree((unsigned int)result);
		close((unsigned int)file);
		return 1;
	}
#if defined(ZVM_PROFILE) && ZVM_PROFILE
	play_story.profile_enabled = benchmarking;
#endif
	if (story_cache_prepare(&play_story, story_end, &checksum) != ZVM_OK) {
		printf("zvmtest: cannot prepare story memory/checksum\n");
		play_cleanup(file);
		return 1;
	}
	if (zvm_init_memory(&vm, story_cache_read, story_cache_write, &play_story,
			    play_story.reader.size) != ZVM_OK) {
		printf("zvmtest: story init error %u\n", (unsigned)zvm_error(&vm));
		play_cleanup(file);
		return 1;
	}
	zvm_set_story_checksum(&vm, checksum);
	if (!benchmarking) {
		struct kos_xmem_info info;
		struct kos_bank_info banks;
		if (xinfo(play_story.dynamic_handle, &info) == 0)
			printf("zvmtest: XMEM requested %u allocated %u flags %u type %u\n",
			       (unsigned)info.requested, (unsigned)info.allocated,
			       (unsigned)info.flags, (unsigned)info.type);
		if (diag_bank_info(&banks) == 2)
			printf("zvmtest: application bank %u valid-bank-mask 0x%02x\n",
			       (unsigned)banks.bank, (unsigned)banks.mask);
	}
	zvm_set_output_callback(&vm, play_output, 0);
	zvm_set_status_callback(&vm, play_status, 0);
	if (!benchmarking)
		printf("\rZork I: %s\r\n", path);
	if (benchmarking) {
		console_flush();
		bench_end("load", 0);
		bench_resources();
		console_flush();
		bench_begin();
		bench_end("timer-pair", 0);
		console_flush();
		bench_begin();
	}
	for (;;) {
		zvm_status_t status = zvm_run(&vm, 1200u);
		if (status == ZVM_YIELDED)
			continue;
		if (status == ZVM_NEED_INPUT) {
			if (benchmarking) {
				console_flush();
				bench_end(command_index ? "input" : "startup", command_index);
			}
			if (state_reporting.enabled) {
				uint16_t here, state;
				if (woh_state_report_read(&vm, &state_reporting, &here, &state) != ZVM_OK) {
					printf("zvmtest: state observation failed\n");
					play_cleanup(file);
					return 1;
				}
				printf("\r\nHERE:      %u\r\nWOH-STATE: %u\r\n",
				       (unsigned)here, (unsigned)state);
			}
			if (automated) {
				uint8_t i;
				if (command_index >= (benchmarking ? 10u : 7u)) {
					if (benchmarking) {
						bench_report();
						if (bench_timer >= 0) {
							unbind((unsigned char)bench_timer);
							bench_timer = -1;
						}
					}
					printf("\r\nzvmtest: scripted Zork sequence completed\r\n");
					return play_cleanup(file);
				}
				for (i = 0;
				     i < sizeof(play_line) - 1u && commands[command_index][i]; ++i)
					play_line[i] = commands[command_index][i];
				play_line[i] = 0;
				++command_index;
				printf("\r\n> %s\r\n", play_line);
				result = (int)i;
			} else {
				console_flush();
				result = console_readline(play_line, sizeof(play_line));
			}
			if (benchmarking) {
				console_flush();
				bench_begin();
			}
			if (result < 0 || zvm_submit_input(&vm, play_line) != ZVM_OK) {
				printf("zvmtest: input error\n");
				play_cleanup(file);
				return 1;
			}
			continue;
		}
		if (status == ZVM_FINISHED) {
			printf("\r\nzvmtest: story finished\r\n");
			return play_cleanup(file);
		}
		printf("\nzvmtest: VM error %u at %lu (instruction %lu opcode %u after %lu)\n",
		       (unsigned)zvm_error(&vm), (unsigned long)zvm_program_counter(&vm),
		       (unsigned long)zvm_fault_address(&vm), (unsigned)zvm_fault_opcode(&vm),
		       (unsigned long)zvm_previous_address(&vm));
		play_cleanup(file);
		return 1;
	}
}

static void word(uint16_t a, uint16_t v)
{
	story[a] = (uint8_t)(v >> 8);
	story[(uint16_t)(a + 1u)] = (uint8_t)v;
}

static void fail(const char *where)
{
	printf("zvmtest: FAIL %s\r\n", where);
}

static int expect_pc(uint32_t pc)
{
	if (zvm_program_counter(&vm) != pc) {
		printf("zvmtest: pc expected %lu got %lu\r\n", (unsigned long)pc,
		       (unsigned long)zvm_program_counter(&vm));
		fail("pc");
		return 0;
	}
	return 1;
}

static zvm_error_t page_read(void *user, uint32_t address, uint8_t *value)
{
	paged_story_t *p = (paged_story_t *)user;
	if (address < sizeof(p->bytes)) {
		*value = p->bytes[address];
		return ZVM_OK;
	}
	if (address == 32768u) {
		*value = 0x8c;
		return ZVM_OK;
	}
	if (address == 32769u) {
		*value = 0x7f;
		return ZVM_OK;
	}
	if (address == 32770u) {
		*value = 0xff;
		return ZVM_OK;
	}
	if (address == 65536u) {
		*value = 0xba;
		return ZVM_OK;
	}
	if (address < 65538ul) {
		*value = 0;
		return ZVM_OK;
	}
	return ZVM_EBOUNDS;
}

static zvm_error_t page_write(void *user, uint32_t address, uint8_t value)
{
	paged_story_t *p = (paged_story_t *)user;
	if (address >= 800u || address >= sizeof(p->bytes))
		return ZVM_EREADONLY;
	p->bytes[address] = value;
	return ZVM_OK;
}

int main(int argc, char **argv)
{
	/* KonaOS supplies only the argument tail: argv[0] is the first flag. */
	uint16_t value;
	uint8_t parent, sibling, child, set;
	if (argc > 0 && argv[0] && argv[0][0] == '-' &&
	    (argv[0][1] == 'b' || argv[0][1] == 'd' || argv[0][1] == 'B')) {
		const char *path = "/fs/ZORK1.Z3";
		int result;
		if (argc > 1)
			path = argv[1];
		benchmarking = 1;
		discard_output = (uint8_t)(argv[0][1] == 'd');
		result = play_zork(path, 1);
		if (!result && argv[0][1] == 'B') {
			discard_output = 1;
			result = play_zork(path, 1);
		}
		return result;
	}
	if (argc > 0 && argv[0] && argv[0][0] == '-' && (argv[0][1] == 'p' || argv[0][1] == 'a')) {
		if (argc > 1)
			return play_zork(argv[1], (uint8_t)(argv[0][1] == 'a'));
		return play_zork("/fs/ZORK1.Z3", (uint8_t)(argv[0][1] == 'a'));
	}
	memset(story, 0, sizeof(story));
	story[0] = 3;
	word(6, 64);	 /* Initial PC */
	word(10, 100);	 /* Object table */
	word(12, 320);	 /* Globals table */
	word(14, 800);	 /* Static memory */
	word(0x1a, 512); /* Story length, in words */

	/* Object 1 has attribute zero and one-byte property 1. */
	story[162] = 0x80;
	word(169, 300);
	story[300] = 0;
	story[301] = 1;
	story[302] = 0x33;
	story[303] = 0;

	/* Routine 120: one local initialized to 7, overridden by arg 0. */
	story[240] = 1;
	story[241] = 0;
	story[242] = 7;
	story[243] = 0xab;
	story[244] = 1; /* ret local 1 */
	/* Routine 130: rtrue. */
	story[260] = 0;
	story[261] = 0xb0;
	/* Routine 140 calls routine 130 and returns its result. */
	story[280] = 0;
	story[281] = 0x98;
	story[282] = 130;
	story[283] = 16;
	story[284] = 0xae;
	story[285] = 16;
	story[286] = 16;
	story[287] = 0xe8;
	story[288] = 0x7f;
	story[289] = 1;
	story[290] = 0xb8;

	/* Main bytecode: arithmetic, branch, call/return, properties, attrs,
	 * object links, and quit. */
	{
		static const uint8_t code[] = {
		    0x14, 2,	3,    16,		 /* add 2 3 -> global 16 */
		    0x90, 0,	0xc2,			 /* jz 0, branch true by 2 */
		    0xe0, 0x55, 120,  9,    8,	  7, 16, /* call routine 120 with 3 args */
		    0x98, 140,	16,			 /* call_1s routine 140 */
		    0x11, 1,	1,    16,		 /* get_prop */
		    0xe3, 0x57, 1,    1,    0x44,	 /* put_prop */
		    0x0a, 1,	0,    0xc2,		 /* test_attr, true */
		    0x0e, 1,	2,			 /* insert_obj 1 into 2 */
		    0x0c, 1,	0,			 /* clear_attr */
		    0x0b, 1,	1,			 /* set_attr */
		    0x0a, 1,	1,    0xc2,		 /* test_attr, true */
		    0x92, 2,	16,   0xc2,		 /* get_child -> global 16; branch */
		    0x9f, 130,	16,			 /* not 130 -> global 16 */
		    0xba				 /* quit */
		};
		memcpy(story + 64, code, sizeof(code));
	}
	memcpy(initial_story, story, sizeof(story));

	if (zvm_init(&vm, story, sizeof(story)) != ZVM_OK) {
		fail("init");
		return 1;
	}
	if (zvm_run(&vm, 1) != ZVM_YIELDED || !expect_pc(68)) {
		fail("budget arithmetic");
		return 1;
	}
	if (zvm_global(&vm, 0, &value) != ZVM_OK || value != 5) {
		fail("add global");
		return 1;
	}
	if (zvm_run(&vm, 1) != ZVM_YIELDED || !expect_pc(71)) {
		fail("conditional branch");
		return 1;
	}
	if (zvm_run(&vm, 1) != ZVM_YIELDED || !expect_pc(243)) {
		fail("routine call");
		return 1;
	}
	if (zvm_run(&vm, 1) != ZVM_YIELDED || !expect_pc(78)) {
		fail("routine return");
		return 1;
	}
	if (zvm_global(&vm, 0, &value) != ZVM_OK || value != 9) {
		printf("zvmtest: local arg value %u\r\n", (unsigned)value);
		fail("local arg");
		return 1;
	}
	if (zvm_run(&vm, 1) != ZVM_YIELDED || !expect_pc(281)) {
		fail("nested call entry");
		return 1;
	}
	if (zvm_run(&vm, 1) != ZVM_YIELDED || !expect_pc(261)) {
		fail("nested call");
		return 1;
	}
	if (zvm_run(&vm, 1) != ZVM_YIELDED || !expect_pc(284)) {
		fail("nested return");
		return 1;
	}
	{
		zvm_status_t status = zvm_run(&vm, 1);
		if (status != ZVM_YIELDED || !expect_pc(287)) {
			printf("zvmtest: load status %u error %u pc %lu\r\n", (unsigned)status,
			       (unsigned)zvm_error(&vm), (unsigned long)zvm_program_counter(&vm));
			fail("load global");
			return 1;
		}
	}
	if (zvm_run(&vm, 1) != ZVM_YIELDED || !expect_pc(290)) {
		fail("push return value");
		return 1;
	}
	if (zvm_run(&vm, 1) != ZVM_YIELDED || !expect_pc(81)) {
		fail("outer return");
		return 1;
	}
	if (zvm_global(&vm, 0, &value) != ZVM_OK || value != 1) {
		fail("rtrue result");
		return 1;
	}
	if (zvm_run(&vm, 1) != ZVM_YIELDED || !expect_pc(85)) {
		fail("get property");
		return 1;
	}
	if (zvm_global(&vm, 0, &value) != ZVM_OK || value != 0x33) {
		fail("property value");
		return 1;
	}
	{
		zvm_status_t status = zvm_run(&vm, 1);
		if (status != ZVM_YIELDED || !expect_pc(90)) {
			printf("zvmtest: put status %u error %u pc %lu\r\n", (unsigned)status,
			       (unsigned)zvm_error(&vm), (unsigned long)zvm_program_counter(&vm));
			fail("put property");
			return 1;
		}
	}
	if (zvm_read_byte(&vm, 302, (uint8_t *)&value) != ZVM_OK || (uint8_t)value != 0x44) {
		fail("property write");
		return 1;
	}
	if (zvm_run(&vm, 1) != ZVM_YIELDED || !expect_pc(94)) {
		fail("test attr");
		return 1;
	}
	if (zvm_run(&vm, 1) != ZVM_YIELDED || !expect_pc(97)) {
		fail("insert object");
		return 1;
	}
	if (zvm_object_links(&vm, 1, &parent, &sibling, &child) != ZVM_OK || parent != 2) {
		fail("object parent");
		return 1;
	}
	if (zvm_run(&vm, 1) != ZVM_YIELDED || !expect_pc(100)) {
		fail("clear attr");
		return 1;
	}
	if (zvm_run(&vm, 1) != ZVM_YIELDED || !expect_pc(103)) {
		fail("set attr");
		return 1;
	}
	if (zvm_object_attribute(&vm, 1, 1, &set) != ZVM_OK || !set) {
		fail("attribute state");
		return 1;
	}
	if (zvm_run(&vm, 1) != ZVM_YIELDED || !expect_pc(107)) {
		fail("test attr branch");
		return 1;
	}
	if (zvm_run(&vm, 1) != ZVM_YIELDED || !expect_pc(111)) {
		fail("get child");
		return 1;
	}
	if (zvm_global(&vm, 0, &value) != ZVM_OK || value != 1) {
		fail("child result");
		return 1;
	}
	if (zvm_run(&vm, 1) != ZVM_YIELDED || !expect_pc(114)) {
		fail("not");
		return 1;
	}
	if (zvm_global(&vm, 0, &value) != ZVM_OK || value != 0xff7du) {
		fail("not result");
		return 1;
	}
	if (zvm_run(&vm, 1) != ZVM_FINISHED) {
		fail("quit");
		return 1;
	}

	/* Reinitializing the identical story must produce identical VM-visible state. */
	memcpy(story, initial_story, sizeof(story));
	if (zvm_init(&vm, story, sizeof(story)) != ZVM_OK || zvm_run(&vm, 32) != ZVM_FINISHED) {
		fail("repeat execution");
		return 1;
	}
	if (zvm_global(&vm, 0, &value) != ZVM_OK || value != 0xff7du) {
		fail("repeat global");
		return 1;
	}
	if (zvm_read_byte(&vm, 302, &parent) != ZVM_OK || parent != 0x44u) {
		fail("repeat property");
		return 1;
	}
	if (zvm_object_links(&vm, 1, &parent, &sibling, &child) != ZVM_OK || parent != 2u) {
		fail("repeat object");
		return 1;
	}

	/* Stack variable zero is evaluated by popping operands left to right. */
	memset(story + 64, 0, sizeof(story) - 64u);
	{
		static const uint8_t stack_code[] = {0xe8, 0x7f, 10, 0xe8, 0x7f, 20,
						     0x75, 0,	 0,  16,   0xba};
		memcpy(story + 64, stack_code, sizeof(stack_code));
	}
	if (zvm_reset(&vm) != ZVM_OK) {
		fail("stack reset");
		return 1;
	}
	if (zvm_run(&vm, 3) != ZVM_YIELDED || zvm_program_counter(&vm) != 74u) {
		fail("stack operand execution");
		return 1;
	}
	if (zvm_global(&vm, 0, &value) != ZVM_OK || value != 10u) {
		fail("stack operand order");
		return 1;
	}
	if (zvm_run(&vm, 1) != ZVM_FINISHED) {
		fail("stack quit");
		return 1;
	}

	story[64] = 0xbe;
	if (zvm_reset(&vm) != ZVM_OK || zvm_run(&vm, 1) != ZVM_FAULT ||
	    zvm_error(&vm) != ZVM_EUNSUPPORTED) {
		fail("invalid opcode fault");
		return 1;
	}

	/* Fill the bounded evaluation stack with actual push instructions. */
	{
		uint16_t n;
		for (n = 0; n < 129u; ++n) {
			story[64u + n * 3u] = 0xe8;
			story[65u + n * 3u] = 0x7f;
			story[66u + n * 3u] = 1;
		}
	}
	if (zvm_reset(&vm) != ZVM_OK || zvm_run(&vm, 129) != ZVM_FAULT ||
	    zvm_error(&vm) != ZVM_ESTACK) {
		fail("stack overflow fault");
		return 1;
	}

	/* Recursive bytecode calls hit the bounded frame limit without C recursion. */
	story[64] = 0x98;
	story[65] = 120;
	story[66] = 16;
	story[240] = 0;
	story[241] = 0x98;
	story[242] = 120;
	story[243] = 16;
	if (zvm_reset(&vm) != ZVM_OK || zvm_run(&vm, 16) != ZVM_FAULT ||
	    zvm_error(&vm) != ZVM_EFRAME) {
		fail("frame overflow fault");
		return 1;
	}

	/* A decoded loadw whose word crosses story end reports a bounded fault. */
	story[64] = 0xcf;
	story[65] = 0x1f;
	story[66] = 3;
	story[67] = 0xff;
	story[68] = 0;
	story[69] = 16;
	if (zvm_reset(&vm) != ZVM_OK || zvm_run(&vm, 1) != ZVM_FAULT ||
	    zvm_error(&vm) != ZVM_EBOUNDS) {
		fail("story bounds fault");
		return 1;
	}

	/* Callback memory keeps the logical PC wider than the Z80 pointer space. */
	memset(&paged_story, 0, sizeof(paged_story));
	paged_story.bytes[0] = 3;
	paged_story.bytes[6] = 0;
	paged_story.bytes[7] = 64;
	paged_story.bytes[10] = 0;
	paged_story.bytes[11] = 100;
	paged_story.bytes[12] = 1;
	paged_story.bytes[13] = 64; /* globals 320 */
	paged_story.bytes[14] = 3;
	paged_story.bytes[15] = 32; /* static 800 */
	paged_story.bytes[0x1a] = 0x80;
	paged_story.bytes[0x1b] = 1;
	paged_story.bytes[64] = 0x8c;
	paged_story.bytes[65] = 0x7f;
	paged_story.bytes[66] = 0xbf; /* jump to byte 32768 */
	if (zvm_init_memory(&vm, page_read, page_write, &paged_story, 65538ul) != ZVM_OK) {
		fail("paged init");
		return 1;
	}
	if (zvm_run(&vm, 1) != ZVM_YIELDED || zvm_program_counter(&vm) != 32768ul) {
		fail("pc 32k boundary");
		return 1;
	}
	if (zvm_run(&vm, 1) != ZVM_YIELDED || zvm_program_counter(&vm) != 65536ul) {
		fail("pc 64k boundary");
		return 1;
	}
	if (zvm_run(&vm, 1) != ZVM_FINISHED) {
		fail("pc high quit");
		return 1;
	}
	if (zvm_program_counter(&vm) != 65537ul) {
		fail("pc high final");
		return 1;
	}
	printf("zvmtest: PASS {\"suite\":\"m2-core\",\"cases\":8,\"repeat\":true}\r\n");
	zvm_release(&vm);
	{
		int probe = open("/fs/ZORK1.Z3", KFS_O_READ);
		if (probe >= 0) {
			int auto_flag = open("/fs/ZORKAUTO.TXT", KFS_O_READ);
			uint8_t automated = (uint8_t)(auto_flag >= 0);
			if (auto_flag >= 0)
				close((unsigned int)auto_flag);
			close((unsigned int)probe);
			return play_zork("/fs/ZORK1.Z3", automated);
		}
	}
	return 0;
}
