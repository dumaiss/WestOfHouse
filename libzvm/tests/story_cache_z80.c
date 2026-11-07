#include "story_cache.h"
#if defined(__SDCC) && defined(ZVM_CACHE_ASM) && ZVM_CACHE_ASM
/* Private adapter ABI, checked against the target's actual packed layout.
 * Resident cache bytes only: never an XMEM aperture pointer. */
typedef char reader_size_is_8[sizeof(kos_reader_t) == 8 ? 1 : -1];
typedef char page_size_is_3[sizeof(story_cache_page_t) == 3 ? 1 : -1];
typedef char bytes_offset_is_12[__builtin_offsetof(story_cache_t, bytes) == 12 ? 1 : -1];
typedef char hot_offset_matches
    [__builtin_offsetof(story_cache_t, dyn_hot) == 12 + STORY_CACHE_PAGES * 515 ? 1 : -1];
extern zvm_error_t story_cache_read_slow(void *, uint32_t, uint8_t *);
#if defined(ZVM_PROFILE) && ZVM_PROFILE
extern void story_cache_hit(story_cache_t *, uint32_t);
#endif
/* Same sdcccall(1) signature as the C callback: HL=user; address at SP+2..5;
 * out at SP+6..7; result A; callee removes six argument bytes. IX preserved.
 * On any miss restore entry HL/IX/SP and tail-call the ordinary C slow path.
 * No SDK call, writable static initializer, state-layout change or banking. */
zvm_error_t story_cache_read(void *user, uint32_t address, uint8_t *out) __naked
{
	user;
	address;
	out;
	// clang-format off
 __asm
  push ix
  ld ix,#0
  add ix,sp
  push hl
  pop iy
  ld a,8(ix)
  or 9(ix)
  jp z,00190$
  ; Complete 32-bit source bounds, including addresses outside V3 range.
  ld a,4(ix)
  sub 4(iy)
  ld a,5(ix)
  sbc a,5(iy)
  ld a,6(ix)
  sbc a,6(iy)
  ld a,7(ix)
  sbc a,7(iy)
  jp nc,00190$
  ; Select dynamic/static hot slot; unaligned shared boundary is distinct.
  ld a,6(ix)
  or 7(ix)
  jr nz,00110$
  ld a,4(ix)
  sub 8(iy)
  ld a,5(ix)
  sbc a,9(iy)
  jr nc,00110$
  ld de,#(12+STORY_CACHE_PAGES*515)
  jr 00111$
00110$:
  ld de,#(13+STORY_CACHE_PAGES*515)
00111$:
  add hl,de
  ld a,(hl)
  cp #255
  jr z,00190$
  ld b,a
  ; Logical page tag = bits9..16. Bit16 enters carry on the first RRA.
  ld a,6(ix)
  rra
  ld a,5(ix)
  rra
  ld c,a
  ; Metadata is tag/valid/dirty, three bytes per resident slot.
  ld a,b
  add a,a
  add a,b
  ld e,a
  ld d,#0
  push iy
  pop hl
  add hl,de
  ld de,#(12+STORY_CACHE_PAGES*512)
  add hl,de
  ld a,(hl)
  cp c
  jr nz,00190$
  inc hl
  ld a,(hl)
  or a
  jr z,00190$
 __endasm;
// clang-format on
#if defined(ZVM_PROFILE) && ZVM_PROFILE
	// clang-format off
 __asm
  push bc
  push iy
  push iy
  pop hl
  ld e,6(ix)
  ld d,7(ix)
  push de
  ld e,4(ix)
  ld d,5(ix)
  push de
  call _story_cache_hit
  pop iy
  pop bc
 __endasm;
// clang-format on
#endif
	// clang-format off
 __asm
  ; Address only the resident bytes; hot index already names its partition.
  ld a,5(ix)
  and #1
  ld d,a
  ld a,b
  add a,a
  add a,d
  ld d,a
  ld e,4(ix)
  push iy
  pop hl
  add hl,de
  ld de,#12
  add hl,de
  ld a,(hl)
  ld l,8(ix)
  ld h,9(ix)
  ld (hl),a
  xor a
  pop ix
  pop hl
  pop bc
  pop bc
  pop bc
  jp (hl)
00190$:
  push iy
  pop hl
  pop ix
  jp _story_cache_read_slow
 __endasm;
	// clang-format on
}
#else
typedef unsigned char story_cache_z80_disabled_t;
#endif
