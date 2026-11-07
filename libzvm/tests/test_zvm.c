/* Native C fixtures for the public API and internal instruction contracts.
 * Stories are constructed byte by byte so each check has a controlled input. */
#include <stdio.h>
#include <string.h>
#include "zvm.h"
#include "zvm_internal.h"
static int failures;
#define CHECK(c)                                                                                   \
	do {                                                                                       \
		if (!(c)) {                                                                        \
			printf("FAIL:%u\n", (unsigned)__LINE__);                                   \
			++failures;                                                                \
		}                                                                                  \
	} while (0)
static char emitted[32];
static unsigned int emitted_n;

static zvm_error_t capture(void *u, uint8_t channel, uint8_t byte)
{
	(void)u;
	CHECK(channel == ZVM_OUTPUT_NARRATIVE);
	if (emitted_n < sizeof(emitted))
		emitted[emitted_n++] = (char)byte;
	return ZVM_OK;
}

static void put16(uint8_t *p, unsigned int v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

static void put_zword(uint8_t *s, unsigned int at, uint8_t a, uint8_t b, uint8_t c, uint8_t end)
{
	uint16_t w =
	    (uint16_t)(((uint16_t)a << 10) | ((uint16_t)b << 5) | c | (end ? 0x8000u : 0u));
	put16(s + at, w);
}

static void fixture(uint8_t *s, unsigned int n)
{
	memset(s, 0, n);
	s[0] = 3;
	put16(s + 6, 64);
	put16(s + 0xc, 64);
	put16(s + 0xe, 128);
	put16(s + 0x1a, n / 2u);
}

int main(void)
{
	uint8_t s[640], b, p, si, ch, set, prop[2], plen, in[640];
	uint16_t v;
	zvm_t vm, input_vm;
	fixture(s, sizeof(s));
	CHECK(zvm_init(&vm, s, sizeof(s)) == ZVM_OK);
	CHECK(zvm_program_counter(&vm) == 64 && zvm_status(&vm) == ZVM_READY);
	s[400] = 0x98;
	s[401] = 0xe8;
	CHECK(zvm_set_output_callback(&vm, capture, 0) == ZVM_OK);
	emitted_n = 0;
	CHECK(zvm_text_print(ZVM_STATE(&vm), 400, ZVM_OUTPUT_NARRATIVE, 0) == ZVM_OK);
	CHECK(emitted_n == 3 && memcmp(emitted, "abc", 3) == 0);
	emitted_n = 0;
	CHECK(zvm_output_byte(ZVM_STATE(&vm), ZVM_OUTPUT_NARRATIVE, 0) == ZVM_OK && emitted_n == 0);
	put_zword(s, 410, 4, 6, 5, 1);
	emitted_n = 0;
	CHECK(zvm_text_print(ZVM_STATE(&vm), 410, ZVM_OUTPUT_NARRATIVE, 0) == ZVM_OK &&
	      emitted_n == 1 && emitted[0] == 'A');
	put_zword(s, 412, 5, 19, 5, 0);
	put_zword(s, 414, 5, 5, 5, 1);
	emitted_n = 0;
	CHECK(zvm_text_print(ZVM_STATE(&vm), 412, ZVM_OUTPUT_NARRATIVE, 0) == ZVM_OK &&
	      emitted_n == 1 && emitted[0] == ',');
	put_zword(s, 416, 5, 6, 2, 0);
	put_zword(s, 418, 1, 5, 5, 1);
	emitted_n = 0;
	CHECK(zvm_text_print(ZVM_STATE(&vm), 416, ZVM_OUTPUT_NARRATIVE, 0) == ZVM_OK &&
	      emitted_n == 1 && emitted[0] == 'A');
	put16(s + 0x18, 500);
	put16(s + 500, 260);
	put_zword(s, 520, 13, 14, 5, 1);
	put_zword(s, 422, 1, 0, 5, 1);
	CHECK(zvm_init(&vm, s, sizeof(s)) == ZVM_OK);
	CHECK(zvm_set_output_callback(&vm, capture, 0) == ZVM_OK);
	emitted_n = 0;
	CHECK(zvm_text_print(ZVM_STATE(&vm), 422, ZVM_OUTPUT_NARRATIVE, 0) == ZVM_OK &&
	      emitted_n == 2 && memcmp(emitted, "hi", 2) == 0);
	put_zword(s, 424, 5, 7, 5, 1);
	emitted_n = 0;
	CHECK(zvm_text_print(ZVM_STATE(&vm), 424, ZVM_OUTPUT_NARRATIVE, 0) == ZVM_OK &&
	      emitted_n == 1 && emitted[0] == '\n');
	put_zword(s, 430, 6, 7, 8, 0);
	put_zword(s, 432, 5, 6, 2, 1);
	emitted_n = 0;
	CHECK(zvm_text_print(ZVM_STATE(&vm), 430, ZVM_OUTPUT_NARRATIVE, 0) == ZVM_OK &&
	      emitted_n == 3 && memcmp(emitted, "abc", 3) == 0);
	CHECK(zvm_text_print(ZVM_STATE(&vm), 638, ZVM_OUTPUT_NARRATIVE, 0) == ZVM_EBOUNDS);
	s[64] = 0xae;
	s[65] = 16;
	s[66] = 16;
	{
		zvm_instruction_t i;
		CHECK(zvm_decode(ZVM_CSTATE(&vm), 64, &i) == ZVM_OK);
		CHECK(i.form == ZI_SHORT && i.opcode == 142 && i.operand_count == 1 &&
		      i.has_store && i.store_var == 16 && i.next_pc == 67);
	}
	{
		zvm_instruction_t i;
		s[64] = 0xb4;
		CHECK(zvm_decode(ZVM_CSTATE(&vm), 64, &i) == ZVM_OK && i.form == ZI_SHORT &&
		      i.opcode == 180 && i.operand_count == 0 && i.next_pc == 65);
		s[64] = 0x8e;
		s[65] = 0x12;
		s[66] = 0x34;
		s[67] = 16;
		CHECK(zvm_decode(ZVM_CSTATE(&vm), 64, &i) == ZVM_OK && i.operands[0] == 0x1234 &&
		      i.has_store && i.next_pc == 68);
		s[64] = 0x75;
		s[65] = 0;
		s[66] = 0;
		s[67] = 16;
		CHECK(zvm_decode(ZVM_CSTATE(&vm), 64, &i) == ZVM_OK && i.form == ZI_LONG &&
		      i.operand_count == 2 && i.operand_types[0] == 2 && i.has_store &&
		      i.next_pc == 68);
		s[64] = 0xe0;
		s[65] = 0x55;
		s[66] = 1;
		s[67] = 2;
		s[68] = 3;
		s[69] = 4;
		s[70] = 16;
		CHECK(zvm_decode(ZVM_CSTATE(&vm), 64, &i) == ZVM_OK && i.form == ZI_VAR &&
		      i.operand_count == 4 && i.operands[3] == 4 && i.store_var == 16 &&
		      i.next_pc == 71);
		s[64] = 0x80;
		s[65] = 0;
		s[66] = 0;
		s[67] = 0xbf;
		s[68] = 0xff;
		CHECK(zvm_decode(ZVM_CSTATE(&vm), 64, &i) == ZVM_OK && i.has_branch &&
		      i.branch_offset == -1 && i.branch_target == 66);
		s[67] = 0xc1;
		CHECK(zvm_decode(ZVM_CSTATE(&vm), 64, &i) == ZVM_OK && i.branch_return == 1);
		s[64] = 0xb2;
		s[65] = 0;
		s[66] = 1;
		s[67] = 0x80;
		s[68] = 2;
		CHECK(zvm_decode(ZVM_CSTATE(&vm), 64, &i) == ZVM_OK && i.next_pc == 69);
		s[64] = 0xe0;
		s[65] = 0x5d;
		CHECK(zvm_decode(ZVM_CSTATE(&vm), 64, &i) == ZVM_EINVAL);
		s[639] = 0x8d;
		CHECK(zvm_decode(ZVM_CSTATE(&vm), 639, &i) == ZVM_EBOUNDS);
	}
	s[127] = 0x12;
	s[128] = 0x34;
	CHECK(zvm_read_word(&vm, 127, &v) == ZVM_OK && v == 0x1234);
	CHECK(zvm_read_byte(&vm, 128, &b) == ZVM_OK && b == 0x34);
	CHECK(zvm_write_byte(&vm, 127, 7) == ZVM_OK);
	CHECK(zvm_write_byte(&vm, 128, 7) == ZVM_EREADONLY);
	CHECK(zvm_stack_push(&vm, 0xabcd) == ZVM_OK);
	CHECK(zvm_stack_pop(&vm, &v) == ZVM_OK && v == 0xabcd);
	CHECK(zvm_stack_pop(&vm, &v) == ZVM_ESTACK);
	for (v = 0; v < ZVM_STACK_CAPACITY; v++) {
		CHECK(zvm_stack_push(&vm, v) == ZVM_OK);
	}
	CHECK(zvm_stack_push(&vm, 0) == ZVM_ESTACK);
	s[64] = 0xb4;
	CHECK(zvm_reset(&vm) == ZVM_OK);
	CHECK(zvm_run(&vm, 1) == ZVM_YIELDED && zvm_program_counter(&vm) == 65);
	CHECK(zvm_run(&vm, 0) == ZVM_YIELDED && zvm_program_counter(&vm) == 65);
	/* A taken V3 branch offset 0 returns false from the active routine. */
	fixture(s, sizeof(s));
	s[64] = 0x98;
	s[65] = 64;
	s[66] = 16;
	s[128] = 0;
	s[129] = 0x90;
	s[130] = 0;
	s[131] = 0x80;
	CHECK(zvm_init(&vm, s, sizeof(s)) == ZVM_OK && zvm_run(&vm, 2) == ZVM_YIELDED);
	CHECK(zvm_program_counter(&vm) == 67 && zvm_global(&vm, 0, &v) == ZVM_OK && v == 0);
	s[0] = 5;
	CHECK(zvm_init(&vm, s, sizeof(s)) == ZVM_EVERSION);
	s[0] = 3;
	CHECK(zvm_init(&vm, s, 63) == ZVM_EHEADER);
	fixture(s, sizeof(s));
	put16(s + 0xe, 0);
	CHECK(zvm_init(&vm, s, sizeof(s)) == ZVM_EHEADER);
	fixture(s, sizeof(s));
	put16(s + 0xa, 64);
	s[126] = 0x80;
	s[130] = 2;
	s[131] = 3;
	s[132] = 4;
	put16(s + 133, 180);
	s[180] = 0;
	s[181] = 1;
	s[182] = 0xaa;
	s[183] = 0;
	CHECK(zvm_init(&vm, s, sizeof(s)) == ZVM_OK);
	CHECK(zvm_global(&vm, 0, &v) == ZVM_OK);
	CHECK(zvm_read_byte(&vm, 126, &b) == ZVM_OK && b == 0x80);
	CHECK(zvm_read_word(&vm, 10, &v) == ZVM_OK && v == 64);
	CHECK(zvm_object_links(&vm, 1, &p, &si, &ch) == ZVM_OK && p == 2 && si == 3 && ch == 4);
	CHECK(zvm_object_attribute(&vm, 1, 0, &set) == ZVM_OK && set == 1);
	CHECK(zvm_object_property(&vm, 1, 1, prop, sizeof(prop), &plen) == ZVM_OK && plen == 1 &&
	      prop[0] == 0xaa);
	/* Cooperative sread plus V3 dictionary lookup/tokenization. */
	fixture(in, sizeof(in));
	in[0x0e] = 0;
	in[0x0f] = 192;
	put16(in + 8, 320);
	in[320] = 1;
	in[321] = ',';
	in[322] = 4;
	put16(in + 323, 1);
	in[325] = 0x46;
	in[326] = 0x94;
	in[327] = 0xc0;
	in[328] = 0xa5;
	in[90] = 3;
	in[110] = 12;
	in[64] = 0xe4;
	in[65] = 0x5f;
	in[66] = 110;
	in[67] = 90;
	in[68] = 0xba;
	CHECK(zvm_init(&input_vm, in, sizeof(in)) == ZVM_OK);
	CHECK(zvm_run(&input_vm, 1) == ZVM_NEED_INPUT && zvm_program_counter(&input_vm) == 68);
	CHECK(zvm_submit_input(&input_vm, "LOOK,xxx") == ZVM_OK);
	CHECK(in[91] == 3 && in[92] == 1 && in[93] == 69 && in[94] == 4 && in[95] == 1);
	CHECK(in[96] == 0 && in[97] == 0 && in[98] == 1 && in[99] == 5);
	CHECK(in[100] == 0 && in[101] == 0 && in[102] == 3 && in[103] == 6);
	CHECK(zvm_run(&input_vm, 1) == ZVM_FINISHED);
	zvm_release(&vm);
	CHECK(zvm_status(&vm) == ZVM_FAULT);
	if (failures) {
		printf("zvmtest: FAIL %d\n", failures);
		return 1;
	}
	puts("zvmtest: PASS");
	return 0;
}
