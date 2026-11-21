/* Logical story-memory boundary shared by flat and callback backends.
 * Words are big-endian; every byte access remains independently checked. */
#include "zvm_internal.h"

/* The Z80 hit paths at the end of this file fall back to these. */
#if defined(__SDCC) && defined(ZVM_CACHE_ASM) && ZVM_CACHE_ASM && \
    !(defined(ZVM_PROFILE) && ZVM_PROFILE)
#define ZVM_MEM_ASM 1
zvm_error_t zvm_mem_read_slow(const zvm_state_t *s, uint32_t a, uint8_t *out);
zvm_error_t zvm_mem_fetch_slow(const zvm_state_t *s, uint32_t a, uint8_t *out);
zvm_error_t zvm_mem_write_slow(zvm_state_t *s, uint32_t a, uint8_t value);
#define zvm_mem_read zvm_mem_read_slow
#define zvm_mem_fetch zvm_mem_fetch_slow
#define zvm_mem_write zvm_mem_write_slow
#endif

#if !defined(ZVM_MEM_ASM)
/* Page windows in C (the Z80 paths below keep their own).  The window state
 * is a cache, so a const state lends it for update. */
#define HIT(s, w, t) ((w)->page && (w)->tag == (t) && (w)->generation == *(s)->page_generation)

static uint8_t lend(zvm_state_t *s, zvm_window_t *w, uint32_t a, uint8_t write)
{
	w->page = s->page(s->memory_user, a, write);
	if (!w->page)
		return 0;
	w->tag = (uint8_t)(a >> 9);
	w->generation = *s->page_generation;
	return 1;
}

static zvm_error_t data_windowed(const zvm_state_t *cs, uint32_t a, uint8_t *out)
{
	zvm_state_t *s = (zvm_state_t *)cs;
	uint8_t t = (uint8_t)(a >> 9);
	if (HIT(s, &s->data, t)) {
		*out = s->data.page[(uint16_t)a & 511u];
		return ZVM_OK;
	}
	if (HIT(s, &s->data2, t)) {
		*out = s->data2.page[(uint16_t)a & 511u];
		return ZVM_OK;
	}
	s->data2 = s->data;
	if (!lend(s, &s->data, a, 0))
		return s->read_byte(s->memory_user, a, out);
	*out = s->data.page[(uint16_t)a & 511u];
	return ZVM_OK;
}

static zvm_error_t fetch_windowed(const zvm_state_t *cs, uint32_t a, uint8_t *out)
{
	zvm_state_t *s = (zvm_state_t *)cs;
	if (!HIT(s, &s->fetch, (uint8_t)(a >> 9)) && !lend(s, &s->fetch, a, 0))
		return s->read_byte(s->memory_user, a, out);
	*out = s->fetch.page[(uint16_t)a & 511u];
	return ZVM_OK;
}

#endif

zvm_error_t zvm_mem_read(const zvm_state_t *s, uint32_t a, uint8_t *out)
{
	if (!s || !out || a >= s->story_size)
		return ZVM_EBOUNDS;
	ZVM_COUNT(s, byte_reads);
	if (s->flat_story) {
		*out = s->flat_story[a];
		return ZVM_OK;
	}
	if (!s->read_byte)
		return ZVM_ESTATE;
#if !defined(ZVM_MEM_ASM)
	if (s->page)
		return data_windowed(s, a, out);
#endif
	return s->read_byte(s->memory_user, a, out);
}

zvm_error_t zvm_mem_fetch(const zvm_state_t *s, uint32_t a, uint8_t *out)
{
#if !defined(ZVM_MEM_ASM)
	if (s && out && a < s->story_size && !s->flat_story && s->page) {
		ZVM_COUNT(s, byte_reads);
		return fetch_windowed(s, a, out);
	}
#endif
	return zvm_mem_read(s, a, out);
}

zvm_error_t zvm_mem_write(zvm_state_t *s, uint32_t a, uint8_t value)
{
	if (!s || a >= s->story_size)
		return ZVM_EBOUNDS;
	ZVM_COUNT(s, byte_writes);
	if (a >= s->static_base)
		return ZVM_EREADONLY;
	if (s->flat_story) {
		s->flat_story[a] = value;
		return ZVM_OK;
	}
	if (!s->write_byte)
		return ZVM_ESTATE;
#if !defined(ZVM_MEM_ASM)
	if (s->page && (HIT(s, &s->write, (uint8_t)(a >> 9)) || lend(s, &s->write, a, 1))) {
		s->write.page[(uint16_t)a & 511u] = value;
		return ZVM_OK;
	}
#endif
	return s->write_byte(s->memory_user, a, value);
}

#if defined(ZVM_MEM_ASM)
#undef zvm_mem_read
#undef zvm_mem_fetch
#undef zvm_mem_write
#endif

zvm_error_t zvm_mem_word(const zvm_state_t *s, uint32_t a, uint16_t *out)
{
	uint8_t hi, lo;
	zvm_error_t e;
	if (!out)
		return ZVM_EINVAL;
#if defined(ZVM_PROFILE) && ZVM_PROFILE
	if (s)
		ZVM_COUNT(s, word_reads);
#endif
	e = zvm_mem_read(s, a, &hi);
	if (e)
		return e;
	e = zvm_mem_read(s, a + 1ul, &lo);
	if (e)
		return e;
	*out = ((uint16_t)hi << 8) | lo;
	return ZVM_OK;
}


zvm_error_t zvm_read_byte(const zvm_t *vm, uint32_t a, uint8_t *out)
{
	return vm ? zvm_mem_read(ZVM_CSTATE(vm), a, out) : ZVM_EINVAL;
}

zvm_error_t zvm_read_word(const zvm_t *vm, uint32_t a, uint16_t *out)
{
	return vm ? zvm_mem_word(ZVM_CSTATE(vm), a, out) : ZVM_EINVAL;
}

zvm_error_t zvm_write_byte(zvm_t *vm, uint32_t a, uint8_t value)
{
	return vm ? zvm_mem_write(ZVM_STATE(vm), a, value) : ZVM_EINVAL;
}

/* Z80 paths for zvm_mem_read, zvm_mem_fetch and zvm_mem_write (the C
 * versions above are *_slow and keep no windows on the Z80).  A hit is an
 * address whose page tag and backend generation match a window: one indexed
 * load or store in the lent resident page.  A miss asks the backend for the
 * page (zvm_lend) and refills the window; a page the backend does not lend,
 * an out-of-range address and a state without page access take the C
 * version with the arguments untouched. */
#if defined(ZVM_MEM_ASM)
/* The state layout the assembly reads, checked against the compiler's. */
#define AT(field, offset) typedef char field##_at_##offset[__builtin_offsetof(zvm_state_t, field) == offset ? 1 : -1]
AT(memory_user, 6);
AT(page, 8);
AT(page_generation, 10);
AT(fetch, 12);
AT(data, 16);
AT(data2, 20);
AT(write, 24);
AT(story_size, 29);
AT(static_base, 37);
typedef char window_is_4[sizeof(zvm_window_t) == 4 ? 1 : -1];
typedef char window_tag_at_2[__builtin_offsetof(zvm_window_t, tag) == 2 ? 1 : -1];
typedef char window_gen_at_3[__builtin_offsetof(zvm_window_t, generation) == 3 ? 1 : -1];

/* sdcccall(1): HL = state; address at SP+2..5; out at SP+6..7 (read, fetch)
 * or the value at SP+6 (write); result A; the callee removes the argument
 * bytes.  IX preserved; the page callback is called the same way (HL =
 * user, address and write on the stack) and returns the page in DE. */
zvm_error_t zvm_mem_read(const zvm_state_t *s, uint32_t a, uint8_t *out) __naked
{
	s;
	a;
	out;
	// clang-format off
 __asm
  ld a,h
  or l
  jp z,_zvm_mem_read_slow
  push hl
  pop iy
  ld a,8(iy)
  or 9(iy)
  jp z,190$
  ld hl,#2
  add hl,sp
  ld a,(hl)
  sub 29(iy)
  inc hl
  ld a,(hl)
  sbc a,30(iy)
  inc hl
  ld a,(hl)
  sbc a,31(iy)
  inc hl
  ld a,(hl)
  sbc a,32(iy)
  jr nc,00190$
  ; B = tag (address bits 9..16), C = the backend generation; HL at byte 1
  dec hl
  ld a,(hl)
  rra
  dec hl
  ld a,(hl)
  rra
  ld b,a
  ld e,10(iy)
  ld d,11(iy)
  ld a,(de)
  ld c,a
  ld a,18(iy)
  cp b
  jr nz,00191$
  ld a,19(iy)
  cp c
  jr nz,00191$
  ld e,16(iy)
  ld d,17(iy)
  ld a,d
  or e
  jr nz,00180$
00191$:
  ld a,22(iy)
  cp b
  jr nz,00192$
  ld a,23(iy)
  cp c
  jr nz,00192$
  ld e,20(iy)
  ld d,21(iy)
  ld a,d
  or e
  jr nz,00180$
00192$:
  ; refill: the newest window moves to the second
  ld a,16(iy)
  ld 20(iy),a
  ld a,17(iy)
  ld 21(iy),a
  ld a,18(iy)
  ld 22(iy),a
  ld a,19(iy)
  ld 23(iy),a
  push iy
  pop bc
  ld hl,#16
  add hl,bc
  ld b,h
  ld c,l
  ld a,#0
  call zvm_lend
  jr z,190$
  ld hl,#3
  add hl,sp
00180$:
  ld a,(hl)
  and #1
  dec hl
  ld l,(hl)
  ld h,a
  add hl,de
  ld a,(hl)
  ld hl,#6
  add hl,sp
  ld e,(hl)
  inc hl
  ld d,(hl)
  ld (de),a
  pop hl
  pop bc
  pop bc
  pop bc
  xor a
  jp (hl)
190$:
  push iy
  pop hl
  jp _zvm_mem_read_slow
 __endasm;
	// clang-format on
}

zvm_error_t zvm_mem_fetch(const zvm_state_t *s, uint32_t a, uint8_t *out) __naked
{
	s;
	a;
	out;
	// clang-format off
 __asm
  ld a,h
  or l
  jp z,_zvm_mem_fetch_slow
  push hl
  pop iy
  ld a,8(iy)
  or 9(iy)
  jp z,290$
  ld hl,#2
  add hl,sp
  ld a,(hl)
  sub 29(iy)
  inc hl
  ld a,(hl)
  sbc a,30(iy)
  inc hl
  ld a,(hl)
  sbc a,31(iy)
  inc hl
  ld a,(hl)
  sbc a,32(iy)
  jr nc,00290$
  ; B = tag (address bits 9..16), C = the backend generation; HL at byte 1
  dec hl
  ld a,(hl)
  rra
  dec hl
  ld a,(hl)
  rra
  ld b,a
  ld e,10(iy)
  ld d,11(iy)
  ld a,(de)
  ld c,a
  ld a,14(iy)
  cp b
  jr nz,00291$
  ld a,15(iy)
  cp c
  jr nz,00291$
  ld e,12(iy)
  ld d,13(iy)
  ld a,d
  or e
  jr nz,00280$
00291$:
  push iy
  pop bc
  ld hl,#12
  add hl,bc
  ld b,h
  ld c,l
  ld a,#0
  call zvm_lend
  jr z,290$
  ld hl,#3
  add hl,sp
00280$:
  ld a,(hl)
  and #1
  dec hl
  ld l,(hl)
  ld h,a
  add hl,de
  ld a,(hl)
  ld hl,#6
  add hl,sp
  ld e,(hl)
  inc hl
  ld d,(hl)
  ld (de),a
  pop hl
  pop bc
  pop bc
  pop bc
  xor a
  jp (hl)
290$:
  push iy
  pop hl
  jp _zvm_mem_fetch_slow
 __endasm;
	// clang-format on
}

zvm_error_t zvm_mem_write(zvm_state_t *s, uint32_t a, uint8_t value) __naked
{
	s;
	a;
	value;
	// clang-format off
 __asm
  ld a,h
  or l
  jp z,_zvm_mem_write_slow
  push hl
  pop iy
  ld a,8(iy)
  or 9(iy)
  jp z,390$
  ld hl,#4
  add hl,sp
  ; dynamic memory: address bits 16..31 zero, bits 0..15 < static_base
  ld a,(hl)
  inc hl
  or (hl)
  jr nz,00390$
  dec hl
  dec hl
  dec hl
  ld a,(hl)
  sub 37(iy)
  inc hl
  ld a,(hl)
  sbc a,38(iy)
  jr nc,00390$
  ; tag = address bits 9..15 (bit 16 is zero)
  ld a,(hl)
  srl a
  cp 26(iy)
  jr nz,00391$
  ld e,10(iy)
  ld d,11(iy)
  ld a,(de)
  cp 27(iy)
  jr nz,00391$
  ld e,24(iy)
  ld d,25(iy)
  ld a,d
  or e
  jr nz,00380$
00391$:
  push iy
  pop bc
  ld hl,#24
  add hl,bc
  ld b,h
  ld c,l
  ld a,#1
  call zvm_lend
  jr z,390$
  ld hl,#3
  add hl,sp
00380$:
  ld a,(hl)
  and #1
  dec hl
  ld l,(hl)
  ld h,a
  add hl,de
  ; the value at SP+6
  ex de,hl
  ld hl,#6
  add hl,sp
  ld a,(hl)
  ld (de),a
  pop hl
  pop bc
  pop bc
  inc sp
  xor a
  jp (hl)
390$:
  push iy
  pop hl
  jp _zvm_mem_write_slow
 __endasm;
	// clang-format on
}

/* The shared refill, local to this module (called only from the paths
 * above). */
static void zvm_lend_holder(void) __naked
{
	// clang-format off
 __asm
zvm_lend:
  ; IY = state, BC = the window, A = write; the address at SP+4..7 (the
  ; hit path return address at SP+2).  Z: nothing lent.  NZ: DE = page and
  ; the window holds page, tag and the generation after the lend.
  push bc
  push iy
  push af
  inc sp
  ld hl,#11
  add hl,sp
  ld e,(hl)
  inc hl
  ld d,(hl)
  push de
  ld hl,#11
  add hl,sp
  ld e,(hl)
  inc hl
  ld d,(hl)
  push de
  ld l,6(iy)
  ld h,7(iy)
  ld e,8(iy)
  ld d,9(iy)
  push de
  pop iy
  call ___sdcc_call_iy
  pop iy
  pop bc
  ld a,d
  or e
  ret z
  ld a,e
  ld (bc),a
  inc bc
  ld a,d
  ld (bc),a
  inc bc
  ld hl,#6
  add hl,sp
  ld a,(hl)
  rra
  dec hl
  ld a,(hl)
  rra
  ld (bc),a
  inc bc
  ld l,10(iy)
  ld h,11(iy)
  ld a,(hl)
  ld (bc),a
  ld a,d
  or e
  ret
 __endasm;
	// clang-format on
}
#endif
