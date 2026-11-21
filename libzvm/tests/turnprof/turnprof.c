/* Turn profiler, host side: drives tests/simcore's C Z80 core over the
 * probe image, serves the probe's story/XMEM traps in zero T-states and
 * keeps a per-PC T-state histogram for each turn (turnprof.py reports).
 * usage: turnprof IMAGE.bin STORY SYMBOLS OUT.bin */
#include "z80core.h"
#include "tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TURNS 32
static uint8_t mem[65536], xmem[32768], story[131072];
static long story_size;
static int64_t hist[TURNS][65536];
static uint32_t execs[65536];

static int io_in(int port) { (void)port; return 0xFF; }
static int io_out(int port, int value) { (void)port; (void)value; return 0; }
static int clk(int64_t t) { (void)t; return 0; }
static int acc(void) { return 0; }

static unsigned rd16(int a) { return mem[a] | (mem[a + 1] << 8); }
static uint32_t rd32(int a) { return rd16(a) | ((uint32_t)rd16(a + 2) << 16); }

int main(int argc, char **argv)
{
	struct core c;
	FILE *f;
	char name[128];
	unsigned addr, s_trap = 0, s_begin = 0, s_end = 0, s_fail = 0;
	unsigned s_op = 0, s_ptr = 0, s_len = 0, s_off = 0, s_res = 0, s_turn = 0, s_hash = 0;
	int turn = -1, i, turns = 0;
	int64_t started = 0, total[TURNS];
	if (argc != 5) return 2;
	f = fopen(argv[1], "rb"); if (!f) return 2;
	if (fread(mem, 1, sizeof mem, f) < 0x200) return 2; fclose(f);
	f = fopen(argv[2], "rb"); if (!f) return 2;
	story_size = (long)fread(story, 1, sizeof story, f); fclose(f);
	f = fopen(argv[3], "r"); if (!f) return 2;
	while (fscanf(f, "%127s %x", name, &addr) == 2) {
		if (!strcmp(name, "_host_trap")) s_trap = addr;
		else if (!strcmp(name, "_turn_begin")) s_begin = addr;
		else if (!strcmp(name, "_turn_end")) s_end = addr;
		else if (!strcmp(name, "_probe_fail")) s_fail = addr;
		else if (!strcmp(name, "_host_op")) s_op = addr;
		else if (!strcmp(name, "_host_ptr")) s_ptr = addr;
		else if (!strcmp(name, "_host_len")) s_len = addr;
		else if (!strcmp(name, "_host_off")) s_off = addr;
		else if (!strcmp(name, "_host_result")) s_res = addr;
		else if (!strcmp(name, "_turn_id")) s_turn = addr;
		else if (!strcmp(name, "_output_hash")) s_hash = addr;
	}
	fclose(f);
	memset(&c, 0, sizeof c);
	for (i = 0; i < 8; ++i) { c.bank[i] = mem; c.bank_len[i] = sizeof mem; }
	for (i = 0; i < 256; ++i) {
		c.one_t[i] = ONE_T[i]; c.ed_t[i] = ED_T[i]; c.dd_t[i] = DD_T[i]; c.ed_block[i] = ED_BLOCK[i];
	}
	c.interrupt_tstates = INTERRUPT_T;
	c.revision = 13; c.latch = 0x18; c.stop_out_port = -1;
	c.io_in = io_in; c.io_out = io_out; c.clock = clk; c.accept = acc;
	c.pc = rd16(0) ? 0x0100 : 0x0100; c.sp = 0xE000;
	for (;;) {
		unsigned pc = (unsigned)c.pc;
		int64_t before = c.tstates;
		if (mem[pc] == 0x76) {
			if (pc == s_fail) { fprintf(stderr, "probe failed\n"); return 1; }
			break;
		}
		if (pc == s_trap) {
			unsigned op = mem[s_op], ptr = rd16(s_ptr), len = rd16(s_len);
			uint32_t off = rd32(s_off);
			int res = 0;
			if (op == 0) { mem[s_off] = story_size & 255; mem[s_off+1] = (story_size >> 8) & 255;
				mem[s_off+2] = (story_size >> 16) & 255; mem[s_off+3] = 0; }
			else if (op == 1) { long n = off >= (uint32_t)story_size ? 0 : story_size - off;
				if (n > (long)len) n = len; memcpy(mem + ptr, story + off, n); res = (int)n; }
			else if (op == 2) { memcpy(mem + ptr, xmem + off, len); res = (int)len; }
			else if (op == 3) { memcpy(xmem + off, mem + ptr, len); res = (int)len; }
			mem[s_res] = res & 255; mem[s_res + 1] = (res >> 8) & 255;
			/* return from the trap at no cost */
			c.pc = rd16(c.sp); c.sp += 2;
			continue;
		}
		if (pc == s_begin) { turn = mem[s_turn]; started = c.tstates; }
		if (pc == s_end) {
			if (turn >= 0 && turn < TURNS) { total[turn] = c.tstates - started; turns = turn + 1; }
			printf("turn %d output %08x\n", turn, (unsigned)rd32(s_hash));
			turn = -1;
		}
		if (core_step(&c)) { fprintf(stderr, "core error %d at %04X: %s\n", c.err, pc, c.msg); return 1; }
		if (turn >= 0 && turn < TURNS) { hist[turn][pc] += c.tstates - before; ++execs[pc]; }
	}
	f = fopen(argv[4], "wb");
	fwrite(&turns, sizeof turns, 1, f);
	fwrite(total, sizeof total[0], turns, f);
	fwrite(hist, sizeof hist[0], turns, f);
	fwrite(execs, sizeof execs, 1, f);
	fclose(f);
	return 0;
}
