/* SDCC-only synthetic workloads. Each phase runs real libzvm code on Z80.
 * Marker bodies are supplied by m35_sim.py and have no guest-side timer. */
#include "zvm_internal.h"

#define ITERATIONS 32u
volatile uint8_t bench_id;
volatile uint16_t failures;
volatile uint16_t checksums[9];
static zvm_t vm;
static uint8_t story[2048];
static uint16_t output_count, output_sum;
void bench_begin(void);
void bench_end(void);
#define CHECK(x)                                                                                   \
	do {                                                                                       \
		if (!(x))                                                                          \
			++failures;                                                                \
	} while (0)

static void word(uint16_t at, uint16_t value)
{
	story[at] = (uint8_t)(value >> 8);
	story[at + 1] = (uint8_t)value;
}

static zvm_error_t output(void *user, uint8_t channel, uint8_t byte)
{
	(void)user;
	CHECK(channel == ZVM_OUTPUT_NARRATIVE);
	++output_count;
	output_sum += byte;
	return ZVM_OK;
}

static void prepare(void)
{
	uint16_t n;
	for (n = 0; n < sizeof(story); ++n)
		story[n] = 0;
	story[0] = 3;
	word(6, 64);
	word(8, 1100);
	word(10, 128);
	word(12, 512);
	word(14, 1024);
	word(26, 1024);
	CHECK(zvm_init(&vm, story, sizeof(story)) == ZVM_OK);
}

void probe_main(void)
{
	uint8_t n, b;
	uint16_t v, hit;
	zvm_instruction_t ins;
	zvm_state_t *s = ZVM_STATE(&vm);
	failures = 0;
	output_count = 0;
	output_sum = 0;
	for (n = 0; n < 9; ++n)
		checksums[n] = 0;
	prepare();
	/* add 5 7 -> global 0; jz 0 branches over nop; quit. */
	story[64] = 0x14;
	story[65] = 5;
	story[66] = 7;
	story[67] = 16;
	story[68] = 0x90;
	story[69] = 0;
	story[70] = 0xc3;
	story[71] = 0xb4;
	story[72] = 0xba;
	bench_id = 0;
	bench_begin();
	for (n = 0; n < ITERATIONS; ++n) {
		CHECK(zvm_decode(s, 64, &ins) == ZVM_OK);
		CHECK(ins.opcode == 20 && ins.operand_count == 2 && ins.operands[0] == 5 &&
		      ins.operands[1] == 7 && ins.store_var == 16 && ins.next_pc == 68);
		checksums[0] += ins.operands[0] + ins.operands[1];
	}
	bench_end();
	bench_id = 1;
	bench_begin();
	for (n = 0; n < ITERATIONS; ++n) {
		CHECK(zvm_reset(&vm) == ZVM_OK);
		CHECK(zvm_run(&vm, 8) == ZVM_FINISHED);
		CHECK(zvm_global(&vm, 0, &v) == ZVM_OK && v == 12);
		CHECK(zvm_program_counter(&vm) == 73);
		checksums[1] += v;
	}
	bench_end();
	/* push 42; pull global 0; quit. */
	story[64] = 0xe8;
	story[65] = 0x7f;
	story[66] = 42;
	story[67] = 0xe9;
	story[68] = 0x7f;
	story[69] = 16;
	story[70] = 0xba;
	bench_id = 2;
	bench_begin();
	for (n = 0; n < ITERATIONS; ++n) {
		CHECK(zvm_reset(&vm) == ZVM_OK);
		CHECK(zvm_run(&vm, 8) == ZVM_FINISHED);
		CHECK(zvm_global(&vm, 0, &v) == ZVM_OK && v == 42);
		CHECK(s->sp == 0);
		checksums[2] += v;
	}
	bench_end();
	/* call packed 128 -> global 0; quit. Routine: no locals, rtrue. */
	story[64] = 0xe0;
	story[65] = 0x3f;
	story[66] = 0;
	story[67] = 128;
	story[68] = 16;
	story[69] = 0xba;
	story[256] = 0;
	story[257] = 0xb0;
	bench_id = 3;
	bench_begin();
	for (n = 0; n < ITERATIONS; ++n) {
		CHECK(zvm_reset(&vm) == ZVM_OK);
		CHECK(zvm_run(&vm, 8) == ZVM_FINISHED);
		CHECK(zvm_global(&vm, 0, &v) == ZVM_OK && v == 1);
		CHECK(s->frame_count == 1);
		checksums[3] += v;
	}
	bench_end();
	CHECK(zvm_reset(&vm) == ZVM_OK);
	s->frames[0].local_count = 1;
	bench_id = 4;
	bench_begin();
	for (n = 0; n < ITERATIONS; ++n) {
		CHECK(zvm_variable_write(s, 1, n, 0) == ZVM_OK);
		CHECK(zvm_variable_read(s, 1, &v, 0) == ZVM_OK && v == n);
		checksums[4] += v;
	}
	bench_end();
	CHECK(zvm_variable_read(s, 2, &v, 0) == ZVM_EINVAL);
	bench_id = 5;
	bench_begin();
	for (n = 0; n < ITERATIONS; ++n) {
		CHECK(zvm_variable_write(s, 16, n, 0) == ZVM_OK);
		CHECK(zvm_variable_read(s, 16, &v, 0) == ZVM_OK && v == n);
		checksums[5] += v;
	}
	bench_end();
	bench_id = 6;
	bench_begin();
	for (n = 0; n < ITERATIONS; ++n) {
		CHECK(zvm_write_byte(&vm, 900, n) == ZVM_OK);
		CHECK(zvm_read_byte(&vm, 900, &b) == ZVM_OK && b == n);
		checksums[6] += b;
	}
	bench_end();
	CHECK(zvm_write_byte(&vm, 1024, 1) == ZVM_EREADONLY);
	CHECK(zvm_read_byte(&vm, 2048, &b) == ZVM_EBOUNDS);
	/* Packed 'abc'; sorted dictionary containing exactly that key. */
	story[400] = 0x98;
	story[401] = 0xe8;
	story[1100] = 0;
	story[1101] = 4;
	word(1102, 1);
	story[1104] = 0x18;
	story[1105] = 0xe8;
	story[1106] = 0x94;
	story[1107] = 0xa5;
	CHECK(zvm_set_output_callback(&vm, output, 0) == ZVM_OK);
	bench_id = 7;
	bench_begin();
	for (n = 0; n < ITERATIONS; ++n) {
		CHECK(zvm_text_print(s, 400, ZVM_OUTPUT_NARRATIVE, 0) == ZVM_OK);
		checksums[7] = output_sum;
	}
	bench_end();
	CHECK(output_count == 96 && output_sum == 9408);
	bench_id = 8;
	bench_begin();
	for (n = 0; n < ITERATIONS; ++n) {
		CHECK(zvm_dictionary_lookup(s, (const uint8_t *)"abc", 3, &hit) == ZVM_OK &&
		      hit == 1104);
		checksums[8] += hit;
		CHECK(zvm_dictionary_lookup(s, (const uint8_t *)"xyz", 3, &hit) == ZVM_OK &&
		      hit == 0);
	}
	bench_end();
	CHECK(output_count == 96 && output_sum == 9408);
	CHECK(zvm_error(&vm) == ZVM_OK);
}
