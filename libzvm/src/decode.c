/* Decode instruction metadata without evaluating variable operands.
 * The execution loop resolves variables only after decoding succeeds. */
#include "zvm_internal.h"

/* The Z80 fast path at the end of this file falls back to this decoder. */
#if defined(__SDCC) && defined(ZVM_CACHE_ASM) && ZVM_CACHE_ASM && \
    !(defined(ZVM_PROFILE) && ZVM_PROFILE)
zvm_error_t zvm_decode_slow(const zvm_state_t *s, uint32_t address, zvm_instruction_t *i);
#define zvm_decode zvm_decode_slow
#endif

static zvm_error_t take_byte(const zvm_state_t *s, uint32_t *pc, uint8_t *v)
{
	zvm_error_t e = zvm_mem_fetch(s, *pc, v);
	if (e != ZVM_OK)
		return e;
	ZVM_COUNT(s, decoder_fetches);
	++*pc;
	return ZVM_OK;
}

static uint8_t stores(uint8_t form, uint8_t op)
{
	if (form == ZI_LONG || (form == ZI_VAR && op < 32u)) {
		if (op == 8 || op == 9 || (op >= 15 && op <= 25))
			return 1;
		return 0;
	}
	if (form == ZI_SHORT) {
		if (op == 129 || op == 130 || (op >= 131 && op <= 132) || op == 136 || op == 142 ||
		    op == 143)
			return 1;
		return 0;
	}
	/* V3 VAR call is call_vs. Opcodes introduced in later versions are
	 * decoded as invalid by the dispatch layer. */
	return op == 224u || op == 231u;
}

static uint8_t branches(uint8_t form, uint8_t op)
{
	if (form == ZI_LONG || (form == ZI_VAR && op < 32u))
		return (op >= 1 && op <= 7) || op == 10;
	if (form == ZI_SHORT)
		return op == 128 || op == 129 || op == 130 || op == 181 || op == 182 || op == 189;
	return 0;
}

static uint8_t inline_text(uint8_t form, uint8_t op)
{
	return form == ZI_SHORT && (op == 178 || op == 179);
}

zvm_error_t zvm_decode(const zvm_state_t *s, uint32_t address, zvm_instruction_t *i)
{
	uint8_t raw, typebyte, type, count, n, b1, b2;
	uint16_t branch_bits;
	uint32_t pc;
	zvm_error_t e;
	if (!s || !i)
		return ZVM_EINVAL;
	pc = address;
	e = take_byte(s, &pc, &raw);
	if (e)
		return e;
	i->address = address;
	i->next_pc = 0;
	i->branch_target = 0;
	i->operand_count = 0;
	i->has_store = 0;
	i->has_branch = 0;
	i->branch_return = 0;
	i->branch_offset = 0;
	if ((raw & 0xc0u) == 0x80u) {
		i->form = ZI_SHORT;
		type = (raw >> 4) & 3u;
		i->opcode = (raw & 0x0fu) + (type == 3u ? 176u : 128u);
		if (type != 3u) {
			i->operand_types[0] = type;
			i->operand_count = 1;
		}
	} else if ((raw & 0xc0u) == 0xc0u) {
		i->form = ZI_VAR;
		i->opcode = (raw & 0x20u) ? (uint8_t)(raw & 0x1fu) : (uint8_t)(raw & 0x1fu);
		e = take_byte(s, &pc, &typebyte);
		if (e)
			return e;
		count = 0;
		n = 0;
		{
			uint8_t omitted = 0;
			for (; n < 4; ++n) {
				type = (typebyte >> (6u - 2u * n)) & 3u;
				if (type == 3u) {
					omitted = 1;
					continue;
				}
				if (omitted)
					return ZVM_EINVAL;
				i->operand_types[count++] = type;
			}
		}
		i->operand_count = count;
		/* This is a V3 decoder: call_vs2/call_vn2 do not exist here. */
	} else {
		i->form = ZI_LONG;
		if ((raw & 0xc0u) == 0x40u) {
			i->opcode = raw & 0x1fu;
			i->operand_types[0] = (raw & 0x40u) ? 2u : 1u;
			i->operand_types[1] = (raw & 0x20u) ? 2u : 1u;
			i->operand_count = 2;
		} else {
			/* 00xxxxxx is long 2OP: bit 6 is fixed zero; variable/small
			 * type is encoded by bits 6 and 5. */
			i->opcode = raw & 0x1fu;
			i->operand_types[0] = (raw & 0x40u) ? 2u : 1u;
			i->operand_types[1] = (raw & 0x20u) ? 2u : 1u;
			i->operand_count = 2;
		}
	}
	for (n = 0; n < i->operand_count; ++n) {
		type = i->operand_types[n];
		if (type == 0u) {
			e = take_byte(s, &pc, &b1);
			if (e)
				return e;
			e = take_byte(s, &pc, &b2);
			if (e)
				return e;
			i->operands[n] = ((uint16_t)b1 << 8) | b2;
		} else {
			e = take_byte(s, &pc, &b1);
			if (e)
				return e;
			i->operands[n] = b1;
		}
	}
	/* Normalize short-form opcode ranges into the standard number spaces. */
	if (i->form == ZI_SHORT && i->opcode >= 128u && i->opcode < 176u)
		i->opcode = (uint8_t)(128u + (raw & 0x0fu));
	if (i->form == ZI_SHORT && i->opcode >= 176u)
		i->opcode = (uint8_t)(176u + (raw & 0x0fu));
	if (i->form == ZI_VAR && (raw & 0x20u))
		i->opcode = (uint8_t)(224u + (raw & 0x1fu));
	else if (i->form == ZI_VAR)
		i->opcode = raw & 0x1fu;
	if (i->form == ZI_SHORT && i->opcode == 190u)
		return ZVM_EUNSUPPORTED;
	if (stores(i->form, i->opcode)) {
		e = take_byte(s, &pc, &i->store_var);
		if (e)
			return e;
		i->has_store = 1;
	}
	if (branches(i->form, i->opcode)) {
		e = take_byte(s, &pc, &b1);
		if (e)
			return e;
		i->has_branch = 1;
		i->branch_on_true = (b1 >> 7) & 1u;
		if (b1 & 0x40u)
			i->branch_offset = b1 & 0x3fu;
		else {
			e = take_byte(s, &pc, &b2);
			if (e)
				return e;
			branch_bits = (uint16_t)(((uint16_t)(b1 & 0x3fu) << 8) | b2);
			i->branch_offset = (branch_bits & 0x2000u)
					       ? (int16_t)((int32_t)branch_bits - 0x4000L)
					       : (int16_t)branch_bits;
		}
		if (i->branch_offset == 0 || i->branch_offset == 1)
			i->branch_return = (uint8_t)i->branch_offset;
		else
			i->branch_target = (uint32_t)((int32_t)pc + (int32_t)i->branch_offset - 2L);
	}
	if (inline_text(i->form, i->opcode)) {
		uint8_t hi, lo;
		for (;;) {
			e = take_byte(s, &pc, &hi);
			if (e)
				return e;
			e = take_byte(s, &pc, &lo);
			if (e)
				return e;
			if (hi & 0x80u)
				break;
		}
	}
	i->next_pc = pc;
	return ZVM_OK;
}

#undef zvm_decode

/* Z80 fast path for zvm_decode (the C decoder above is zvm_decode_slow).  It decodes straight from the fetch window when the
 * instruction's page is current and wholly inside the story and the
 * instruction starts at most 496 bytes into it.  Inline-text prints, opcode
 * 190, malformed type bytes and every other case take the C decoder. */
#if defined(__SDCC) && defined(ZVM_CACHE_ASM) && ZVM_CACHE_ASM && \
    !(defined(ZVM_PROFILE) && ZVM_PROFILE)

typedef char address_at_0[__builtin_offsetof(zvm_instruction_t, address) == 0 ? 1 : -1];
typedef char next_pc_at_4[__builtin_offsetof(zvm_instruction_t, next_pc) == 4 ? 1 : -1];
typedef char target_at_8[__builtin_offsetof(zvm_instruction_t, branch_target) == 8 ? 1 : -1];
typedef char operands_at_12[__builtin_offsetof(zvm_instruction_t, operands) == 12 ? 1 : -1];
typedef char types_at_20[__builtin_offsetof(zvm_instruction_t, operand_types) == 20 ? 1 : -1];
typedef char opcode_at_24[__builtin_offsetof(zvm_instruction_t, opcode) == 24 ? 1 : -1];
typedef char form_at_25[__builtin_offsetof(zvm_instruction_t, form) == 25 ? 1 : -1];
typedef char count_at_26[__builtin_offsetof(zvm_instruction_t, operand_count) == 26 ? 1 : -1];
typedef char has_store_at_27[__builtin_offsetof(zvm_instruction_t, has_store) == 27 ? 1 : -1];
typedef char store_var_at_28[__builtin_offsetof(zvm_instruction_t, store_var) == 28 ? 1 : -1];
typedef char has_branch_at_29[__builtin_offsetof(zvm_instruction_t, has_branch) == 29 ? 1 : -1];
typedef char on_true_at_30[__builtin_offsetof(zvm_instruction_t, branch_on_true) == 30 ? 1 : -1];
typedef char return_at_31[__builtin_offsetof(zvm_instruction_t, branch_return) == 31 ? 1 : -1];
typedef char offset_at_32[__builtin_offsetof(zvm_instruction_t, branch_offset) == 32 ? 1 : -1];
typedef char state_fetch_at_12[__builtin_offsetof(zvm_state_t, fetch) == 12 ? 1 : -1];
typedef char state_full_pages_at_28[__builtin_offsetof(zvm_state_t, full_pages) == 28 ? 1 : -1];

/* Normalized opcode bitmaps (bit op & 7 of byte op >> 3), decode.c's
 * stores() and branches() rules: stores first, then branches. */
const uint8_t zvm_decode_flags[64] = {
	0x00, 0x83, 0xff, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x1e, 0xc1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x00, 0x00, 0x00,
	0xfe, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

extern zvm_error_t zvm_decode_slow(const zvm_state_t *, uint32_t, zvm_instruction_t *);

/* sdcccall(1): HL = state; address at SP+2..5; record at SP+6..7; result A;
 * the callee removes the six argument bytes.  IX preserved. */
zvm_error_t zvm_decode(const zvm_state_t *s, uint32_t address, zvm_instruction_t *i) __naked
{
	s;
	address;
	i;
	// clang-format off
 __asm
  ld a,h
  or l
  jp z,00390$
  push hl
  pop iy
  ; the address: a3 == 0, a2 <= 1 (V3), tag = bits 9..16
  ld hl,#5
  add hl,sp
  ld a,(hl)
  or a
  jp nz,00389$
  dec hl
  ld a,(hl)
  cp #2
  jp nc,00389$
  rra
  dec hl
  ld a,(hl)
  rra
  ; A = tag: the fetch window page, still current, wholly in the story
  cp 14(iy)
  jp nz,00389$
  cp 28(iy)
  jp nc,00389$
  ld c,10(iy)
  ld b,11(iy)
  ld a,(bc)
  cp 15(iy)
  jp nz,00389$
  ld e,12(iy)
  ld d,13(iy)
  ld a,d
  or e
  jp z,00389$
  ; offset = bits 0..8, at most 496: the instruction (no inline text, at
  ; most 13 bytes) cannot leave the page
  ld a,(hl)
  and #1
  ld b,a
  dec hl
  ld c,(hl)
  jr z,00301$
  ld a,c
  cp #241
  jp nc,00389$
00301$:
  ex de,hl
  add hl,bc
  ; HL = the first byte; IY = the instruction record
  push iy
  ld iy,#8
  add iy,sp
  ld e,0(iy)
  ld d,1(iy)
  ld a,d
  or e
  jr nz,00302$
  pop iy
  jp 00389$
00302$:
  push de
  pop iy
  ; raw byte: print, print_ret and 190 take the C decoder
  ld a,(hl)
  cp #0xb2
  jp z,00387$
  cp #0xb3
  jp z,00387$
  cp #0xbe
  jp z,00387$
  ; address, the start pointer low byte (in next_pc until the end), zeroes
  ld 4(iy),l
  push hl
  ld hl,#6
  add hl,sp
  ld a,(hl)
  ld 0(iy),a
  inc hl
  ld a,(hl)
  ld 1(iy),a
  inc hl
  ld a,(hl)
  ld 2(iy),a
  inc hl
  ld a,(hl)
  ld 3(iy),a
  pop hl
  xor a
  ld 8(iy),a
  ld 9(iy),a
  ld 10(iy),a
  ld 11(iy),a
  ld 26(iy),a
  ld 27(iy),a
  ld 29(iy),a
  ld 31(iy),a
  ld 32(iy),a
  ld 33(iy),a
  ld a,(hl)
  inc hl
  ld b,a
  and #0xc0
  cp #0x80
  jr z,00305$
  cp #0xc0
  jr z,00306$
  ; long 2OP
  ld 25(iy),#0
  ld a,b
  and #0x1f
  ld 24(iy),a
  ld c,#1
  bit 6,b
  jr z,00303$
  inc c
00303$:
  ld 20(iy),c
  ld c,#1
  bit 5,b
  jr z,00304$
  inc c
00304$:
  ld 21(iy),c
  ld 26(iy),#2
  jp 00320$
00305$:
  ; short: 1OP 128 + n, or 0OP 176 + n
  ld 25(iy),#1
  ld a,b
  and #0x30
  cp #0x30
  jr z,00307$
  rrca
  rrca
  rrca
  rrca
  ld 20(iy),a
  ld 26(iy),#1
  ld a,b
  and #0x0f
  add a,#128
  ld 24(iy),a
  jp 00320$
00307$:
  ld a,b
  and #0x0f
  add a,#176
  ld 24(iy),a
  jp 00320$
00306$:
  ; variable: 2OP n or VAR 224 + n, then the operand-type byte
  ld 25(iy),#2
  ld a,b
  and #0x1f
  bit 5,b
  jr z,00308$
  add a,#224
00308$:
  ld 24(iy),a
  ld a,(hl)
  inc hl
  ld b,a
  ld c,a
  ld a,c
  rlca
  rlca
  ld c,a
  and #3
  cp #3
  jr z,311$
  ld 20(iy),a
  ld a,c
  rlca
  rlca
  ld c,a
  and #3
  cp #3
  jr z,312$
  ld 21(iy),a
  ld a,c
  rlca
  rlca
  ld c,a
  and #3
  cp #3
  jr z,313$
  ld 22(iy),a
  ld a,c
  rlca
  rlca
  ld c,a
  and #3
  cp #3
  jr z,314$
  ld 23(iy),a
  ld 26(iy),#4
  jp 00320$
311$:
  ld a,b
  and #0x3f
  cp #0x3f
  jp nz,00388$
  ld 26(iy),#0
  jp 00320$
312$:
  ld a,b
  and #0x0f
  cp #0x0f
  jp nz,00388$
  ld 26(iy),#1
  jp 00320$
313$:
  ld a,b
  and #0x03
  cp #0x03
  jp nz,00388$
  ld 26(iy),#2
  jp 00320$
314$:
  ld a,b
  and #0x00
  cp #0x00
  jp nz,00388$
  ld 26(iy),#3
  jp 00320$
00320$:
  ; operand 0
  ld a,26(iy)
  cp #1
  jp c,00340$
  ld a,20(iy)
  or a
  jr nz,331$
  ld a,(hl)
  inc hl
  ld 13(iy),a
  ld a,(hl)
  inc hl
  ld 12(iy),a
  jr 332$
331$:
  ld a,(hl)
  inc hl
  ld 12(iy),a
  ld 13(iy),#0
332$:
  ; operand 1
  ld a,26(iy)
  cp #2
  jp c,00340$
  ld a,21(iy)
  or a
  jr nz,333$
  ld a,(hl)
  inc hl
  ld 15(iy),a
  ld a,(hl)
  inc hl
  ld 14(iy),a
  jr 334$
333$:
  ld a,(hl)
  inc hl
  ld 14(iy),a
  ld 15(iy),#0
334$:
  ; operand 2
  ld a,26(iy)
  cp #3
  jp c,00340$
  ld a,22(iy)
  or a
  jr nz,335$
  ld a,(hl)
  inc hl
  ld 17(iy),a
  ld a,(hl)
  inc hl
  ld 16(iy),a
  jr 336$
335$:
  ld a,(hl)
  inc hl
  ld 16(iy),a
  ld 17(iy),#0
336$:
  ; operand 3
  ld a,26(iy)
  cp #4
  jp c,00340$
  ld a,23(iy)
  or a
  jr nz,337$
  ld a,(hl)
  inc hl
  ld 19(iy),a
  ld a,(hl)
  inc hl
  ld 18(iy),a
  jr 338$
337$:
  ld a,(hl)
  inc hl
  ld 18(iy),a
  ld 19(iy),#0
338$:
00340$:
  ; store and branch bytes: C bit 0 stores, bit 1 branches
  push hl
  ld a,24(iy)
  ld c,a
  rrca
  rrca
  rrca
  and #0x1f
  ld e,a
  ld d,#0
  ld hl,#_zvm_decode_flags
  add hl,de
  ld a,c
  and #7
  ld b,a
  ld a,(hl)
  ld de,#32
  add hl,de
  ld e,(hl)
  inc b
  dec b
  jr z,00345$
00344$:
  srl a
  srl e
  djnz 00344$
00345$:
  and #1
  ld c,a
  ld a,e
  and #1
  add a,a
  or c
  ld c,a
  pop hl
  bit 0,c
  jr z,00341$
  ld a,(hl)
  inc hl
  ld 28(iy),a
  ld 27(iy),#1
00341$:
  bit 1,c
  jr z,00350$
  ld 29(iy),#1
  ld a,(hl)
  inc hl
  ld b,a
  rlca
  and #1
  ld 30(iy),a
  ld a,b
  and #0x3f
  bit 6,b
  jr z,00342$
  ld 32(iy),a
  jp 00350$
00342$:
  ; 14-bit offset, sign bit 13
  bit 5,a
  jr z,00343$
  or #0xc0
00343$:
  ld 33(iy),a
  ld a,(hl)
  inc hl
  ld 32(iy),a
00350$:
  ; next_pc = address + length
  ld a,l
  sub 4(iy)
  ld e,a
  ld a,0(iy)
  add a,e
  ld 4(iy),a
  ld a,1(iy)
  adc a,#0
  ld 5(iy),a
  ld a,2(iy)
  adc a,#0
  ld 6(iy),a
  ld a,3(iy)
  adc a,#0
  ld 7(iy),a
  ; a branch: offsets 0 and 1 return, others target next_pc + offset - 2
  ld a,29(iy)
  or a
  jr z,00360$
  ld l,32(iy)
  ld h,33(iy)
  ld a,h
  or a
  jr nz,00351$
  ld a,l
  cp #2
  jr nc,00351$
  ld 31(iy),a
  jp 00360$
00351$:
  dec hl
  dec hl
  ld a,h
  add a,a
  sbc a,a
  ld c,a
  ld a,4(iy)
  add a,l
  ld 8(iy),a
  ld a,5(iy)
  adc a,h
  ld 9(iy),a
  ld a,6(iy)
  adc a,c
  ld 10(iy),a
  ld a,7(iy)
  adc a,c
  ld 11(iy),a
00360$:
  pop hl
  pop hl
  pop bc
  pop bc
  pop bc
  xor a
  jp (hl)
00387$:
00388$:
  ; the C decoder from scratch: the state pointer saved on entry
  pop hl
  jp _zvm_decode_slow
00389$:
  push iy
  pop hl
00390$:
  jp _zvm_decode_slow
 __endasm;
	// clang-format on
}
#endif
