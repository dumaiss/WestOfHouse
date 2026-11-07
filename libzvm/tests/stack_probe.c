/* Optional target-only native-stack watermark for acceptance measurements.
 * This instrumentation is unrelated to the VM evaluation stack. */
#include "stack_probe.h"
#if !defined(ZVM_STACK_PROBE) || !ZVM_STACK_PROBE
typedef unsigned char stack_probe_disabled_t;
#endif
#if defined(ZVM_STACK_PROBE) && ZVM_STACK_PROBE
#if !defined(__SDCC)
#error Stack watermark requires the target SDCC ABI
#endif
/* Diagnostic build only; reserves DE00-DFFF in the EXE manifest. Mark below
 * this call's SP-16, never active frames. No calls or pushes while marking.
 * SDCC sdcccall(1): void result, IX preserved; other registers are scratch. */
void stack_probe_mark(void) __naked
{
	// clang-format off
 __asm
  ld hl,#0
  add hl,sp
  ld de,#16
  or a
  sbc hl,de
  ld de,#0xde00
  or a
  sbc hl,de
  ret c
  ld b,h
  ld c,l
  ex de,hl
001$:
  ld a,b
  or c
  ret z
  ld (hl),#0xa5
  inc hl
  dec bc
  jr 001$
 __endasm;
	// clang-format on
}

/* A write watermark is a lower bound, not a continuous SP trace: an allocated
 * but unwritten frame and accidental A5 bytes can hide additional stack use.
 * Stop at DF00: upper256 bytes were partly active during marking. */
unsigned int stack_probe_written(void)
{
	volatile unsigned char *p = (volatile unsigned char *)0xde00;
	while (p < (volatile unsigned char *)0xdf00 && *p == 0xa5u)
		++p;
	return (unsigned int)(0xe000u - (unsigned int)p);
}

unsigned char stack_probe_guard_ok(void)
{
	volatile unsigned char *p = (volatile unsigned char *)0xde00;
	unsigned char i;
	for (i = 0; i < 16u; ++i)
		if (p[i] != 0xa5u)
			return 0;
	return 1;
}
#endif
