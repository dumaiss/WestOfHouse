/* Decode instruction metadata without evaluating variable operands.
 * The execution loop resolves variables only after decoding succeeds. */
#include "zvm_internal.h"

static zvm_error_t take_byte(const zvm_state_t *s, uint32_t *pc, uint8_t *v)
{
	zvm_error_t e = zvm_mem_read(s, *pc, v);
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
