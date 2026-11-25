/* Instruction semantics for the normalized opcode numbers from decode.c.
 * Dispatch uses ordinary calls so the SDK can place code automatically.
 * Case comments name opcodes; expressions retain explicit VM-width conversions. */
#include "zvm_internal.h"

/* Widen before signed arithmetic; truncate results explicitly at VM boundaries. */
static int32_t signed16(uint16_t v)
{
	return (v & 0x8000u) ? ((int32_t)v - 65536L) : (int32_t)v;
}

static uint16_t wrap16(uint32_t v)
{
	return (uint16_t)v;
}

static uint8_t is_twoop(const zvm_instruction_t *i)
{
	return i->form == ZI_LONG || (i->form == ZI_VAR && i->opcode < 32u);
}

static zvm_error_t need(const zvm_instruction_t *i, uint8_t n)
{
	return i->operand_count == n ? ZVM_OK : ZVM_EINVAL;
}

static zvm_error_t store_result(zvm_state_t *s, const zvm_instruction_t *i, uint16_t v)
{
	return i->has_store ? zvm_variable_write(s, i->store_var, v, 0) : ZVM_EINVAL;
}

/* Branch offsets 0 and 1 return from the current routine instead of jumping. */
static zvm_error_t branch_result(zvm_state_t *s, const zvm_instruction_t *i, uint8_t v)
{
	if (!i->has_branch)
		return ZVM_EINVAL;
	ZVM_COUNT(s, branches);
	if ((v != 0) == (i->branch_on_true)) {
		ZVM_COUNT(s, branches_taken);
		if (i->branch_offset == 0 || i->branch_offset == 1)
			return zvm_return_value(s, (uint16_t)i->branch_offset);
		s->pc = i->branch_target;
	}
	return ZVM_OK;
}

static zvm_error_t store_word(zvm_state_t *s, uint16_t a, uint16_t v)
{
	ZVM_COUNT(s, word_writes);
	if ((uint32_t)a + 1ul >= s->static_base)
		return ZVM_EREADONLY;
	{
		zvm_error_t e = zvm_mem_write(s, a, (uint8_t)(v >> 8));
		if (e)
			return e;
		return zvm_mem_write(s, (uint32_t)a + 1ul, (uint8_t)v);
	}
}

static zvm_error_t call_op(zvm_state_t *s, const zvm_instruction_t *i, const uint16_t *a,
			   uint8_t base)
{
	if (i->operand_count < base)
		return ZVM_EINVAL;
	return zvm_call_routine(s, a[0], a + 1, (uint8_t)(i->operand_count - 1u), i->store_var,
				i->has_store, i->next_pc);
}

static zvm_error_t print_number(zvm_state_t *s, int32_t value)
{
	uint8_t digits[6], n = 0;
	uint32_t v;
	zvm_error_t e;
	if (value < 0) {
		e = zvm_output_byte(s, ZVM_OUTPUT_NARRATIVE, '-');
		if (e)
			return e;
		v = (uint32_t)(-value);
	} else
		v = (uint32_t)value;
	do {
		digits[n++] = (uint8_t)('0' + (v % 10u));
		v /= 10u;
	} while (v && n < 6u);
	while (n) {
		e = zvm_output_byte(s, ZVM_OUTPUT_NARRATIVE, digits[--n]);
		if (e)
			return e;
	}
	return ZVM_OK;
}

static zvm_error_t suspend_for_input(zvm_state_t *s, const zvm_instruction_t *i, const uint16_t *a)
{
	if (i->operand_count != 2u)
		return ZVM_EINVAL;
	{
		zvm_error_t e = zvm_update_status(s);
		if (e)
			return e;
	}
	s->pending_text = a[0];
	s->pending_parse = a[1];
	s->input_pending = 1;
	s->status = ZVM_NEED_INPUT;
	return ZVM_OK;
}

static uint16_t random_step(zvm_state_t *s)
{
	uint16_t x = s->random_state;
	if (!x)
		x = 0xace1u;
	x ^= (uint16_t)(x << 7);
	x ^= (uint16_t)(x >> 9);
	x ^= (uint16_t)(x << 8);
	if (!x)
		x = 0xace1u;
	s->random_state = x;
	return x;
}

zvm_error_t zvm_execute_instruction(zvm_state_t *s, const zvm_instruction_t *i, const uint16_t *a)
{
	zvm_error_t e;
	uint16_t r, v;
	uint8_t b, parent, sibling, child, len;
	uint32_t address;
	int32_t x, y;
	if (!s || !i)
		return ZVM_EINVAL;
	/* Default continuation; branches, calls and returns may replace it below. */
	s->pc = i->next_pc;
	if (i->form == ZI_SHORT && i->opcode >= 176u) {
		/* rtrue */
		if (i->opcode == 176u)
			return zvm_return_value(s, 1);
		/* rfalse */
		if (i->opcode == 177u)
			return zvm_return_value(s, 0);
		/* print / print_ret */
		if (i->opcode == 178u || i->opcode == 179u) {
			e = zvm_text_print(s, i->address + 1ul, ZVM_OUTPUT_NARRATIVE, 0);
			if (e)
				return e;
			if (i->opcode == 179u) {
				e = zvm_output_byte(s, ZVM_OUTPUT_NARRATIVE, '\n');
				if (e)
					return e;
				return zvm_return_value(s, 1);
			}
			return ZVM_OK;
		}
		/* nop */
		if (i->opcode == 180u)
			return ZVM_OK;
		/* V3 save / restore currently report failure through a false branch. */
		if (i->opcode == 181u || i->opcode == 182u)
			return branch_result(s, i, 0);
		/* restart (unsupported) */
		if (i->opcode == 183u)
			return ZVM_EUNSUPPORTED;
		/* ret_popped */
		if (i->opcode == 184u) {
			e = zvm_variable_read(s, 0, &v, 0);
			if (e)
				return e;
			return zvm_return_value(s, v);
		}
		/* pop */
		if (i->opcode == 185u) {
			e = zvm_variable_read(s, 0, &v, 0);
			return e;
		}
		/* quit */
		if (i->opcode == 186u) {
			s->status = ZVM_FINISHED;
			return ZVM_OK;
		}
		/* new_line */
		if (i->opcode == 187u)
			return zvm_output_byte(s, ZVM_OUTPUT_NARRATIVE, '\n');
		/* show_status */
		if (i->opcode == 188u)
			return zvm_update_status(s);
		/* verify */
		if (i->opcode == 189u)
			return branch_result(s, i,
					     (uint8_t)(s->checksum_valid &&
						       s->story_checksum == s->header_checksum));
		return ZVM_EINVAL;
	}
	if (i->form == ZI_SHORT) {
		switch (i->opcode) {
		case 128u: /* jz */
			e = need(i, 1);
			if (e)
				return e;
			return branch_result(s, i, a[0] == 0);
		case 129u: /* get_sibling */
			e = need(i, 1);
			if (e)
				return e;
			e = zvm_object_links_internal(s, (uint8_t)a[0], &parent, &sibling, &child);
			if (e)
				return e;
			e = zvm_variable_write(s, i->store_var, sibling, 0);
			if (e)
				return e;
			return branch_result(s, i, sibling != 0);
		case 130u: /* get_child */
			e = need(i, 1);
			if (e)
				return e;
			e = zvm_object_links_internal(s, (uint8_t)a[0], &parent, &sibling, &child);
			if (e)
				return e;
			e = zvm_variable_write(s, i->store_var, child, 0);
			if (e)
				return e;
			return branch_result(s, i, child != 0);
		case 131u: /* get_parent */
			e = need(i, 1);
			if (e)
				return e;
			e = zvm_object_links_internal(s, (uint8_t)a[0], &parent, &sibling, &child);
			if (e)
				return e;
			return store_result(s, i, parent);
		case 132u: /* get_prop_len */
			e = need(i, 1);
			if (e)
				return e;
			e = zvm_object_prop_length(s, a[0], &len);
			if (e)
				return e;
			return store_result(s, i, len);
		case 133u: /* inc */
			e = need(i, 1);
			if (e)
				return e;
			e = zvm_variable_read(s, (uint8_t)a[0], &v, 1);
			if (e)
				return e;
			return zvm_variable_write(s, (uint8_t)a[0], wrap16((uint32_t)v + 1u), 1);
		case 134u: /* dec */
			e = need(i, 1);
			if (e)
				return e;
			e = zvm_variable_read(s, (uint8_t)a[0], &v, 1);
			if (e)
				return e;
			return zvm_variable_write(s, (uint8_t)a[0], wrap16((uint32_t)v - 1u), 1);
		case 136u: /* call_1s */
			e = need(i, 1);
			if (e)
				return e;
			return call_op(s, i, a, 1);
		case 137u: /* remove_obj */
			e = need(i, 1);
			if (e)
				return e;
			return zvm_object_remove(s, (uint8_t)a[0]);
		case 139u: /* ret */
			e = need(i, 1);
			if (e)
				return e;
			return zvm_return_value(s, a[0]);
		case 140u: /* jump */
			e = need(i, 1);
			if (e)
				return e;
			x = signed16(a[0]);
			y = (int32_t)i->next_pc + x - 2L;
			if (y < 0)
				return ZVM_EBOUNDS;
			s->pc = (uint32_t)y;
			return ZVM_OK;
		case 135u: /* print_addr */
			e = need(i, 1);
			if (e)
				return e;
			return zvm_text_print(s, a[0], ZVM_OUTPUT_NARRATIVE, 0);
		case 138u: /* print_obj */
			e = need(i, 1);
			if (e)
				return e;
			e = zvm_object_name_addr(s, (uint8_t)a[0], &address);
			if (e)
				return e;
			return zvm_text_print(s, address, ZVM_OUTPUT_NARRATIVE, 0);
		case 141u: /* print_paddr */
			e = need(i, 1);
			if (e)
				return e;
			return zvm_text_print(s, (uint32_t)a[0] * 2ul, ZVM_OUTPUT_NARRATIVE, 0);
		case 142u: /* load */
			e = need(i, 1);
			if (e)
				return e;
			e = zvm_variable_read(s, (uint8_t)a[0], &v, 1);
			if (e)
				return e;
			return store_result(s, i, v);
		case 143u: /* not */
			e = need(i, 1);
			if (e)
				return e;
			return store_result(s, i, (uint16_t)~a[0]);
		default:
			return ZVM_EUNSUPPORTED;
		}
	}
	if (is_twoop(i)) {
		/* je */
		if (i->opcode == 1u) {
			uint8_t k;
			if (i->operand_count < 2u)
				return ZVM_EINVAL;
			b = 0;
			for (k = 1; k < i->operand_count; ++k)
				if (a[0] == a[k])
					b = 1;
			return branch_result(s, i, b);
		}
		e = need(i, 2);
		if (e)
			return e;
		switch (i->opcode) {
		case 2u: /* jl */
			return branch_result(s, i, signed16(a[0]) < signed16(a[1]));
		case 3u: /* jg */
			return branch_result(s, i, signed16(a[0]) > signed16(a[1]));
		case 4u: /* dec_chk */
		case 5u: /* inc_chk */
			e = zvm_variable_read(s, (uint8_t)a[0], &v, 1);
			if (e)
				return e;
			r = wrap16((uint32_t)v + (i->opcode == 5u ? 1u : 0xffffu));
			e = zvm_variable_write(s, (uint8_t)a[0], r, 1);
			if (e)
				return e;
			return branch_result(s, i,
					     i->opcode == 5u ? signed16(r) > signed16(a[1])
							     : signed16(r) < signed16(a[1]));
		case 6u: /* jin */
			e = zvm_object_links_internal(s, (uint8_t)a[0], &parent, &sibling, &child);
			if (e)
				return e;
			return branch_result(s, i, parent == a[1]);
		case 7u: /* test */
			return branch_result(s, i, (a[0] & a[1]) == a[1]);
		case 8u: /* or */
			return store_result(s, i, a[0] | a[1]);
		case 9u: /* and */
			return store_result(s, i, a[0] & a[1]);
		case 10u: /* test_attr */
			e = zvm_object_attribute_internal(s, (uint8_t)a[0], (uint8_t)a[1], &b, 0);
			if (e)
				return e;
			return branch_result(s, i, b);
		case 11u: /* set_attr */
			return zvm_object_attribute_internal(s, (uint8_t)a[0], (uint8_t)a[1], &b,
							     1);
		case 12u: /* clear_attr */
			return zvm_object_attribute_internal(s, (uint8_t)a[0], (uint8_t)a[1], &b,
							     2);
		case 13u: /* store */
			return zvm_variable_write(s, (uint8_t)a[0], a[1], 1);
		case 14u: /* insert_obj */
			return zvm_object_insert(s, (uint8_t)a[0], (uint8_t)a[1]);
		case 15u: /* loadw */
			address = wrap16((uint32_t)a[0] + ((uint32_t)(int16_t)signed16(a[1]) << 1));
			e = zvm_mem_word(s, address, &v);
			if (e)
				return e;
			return store_result(s, i, v);
		case 16u: /* loadb */
			address = wrap16((uint32_t)a[0] + (uint32_t)signed16(a[1]));
			e = zvm_mem_read(s, address, &b);
			if (e)
				return e;
			return store_result(s, i, b);
		case 17u: /* get_prop */
			e = zvm_object_get_prop(s, (uint8_t)a[0], (uint8_t)a[1], &v);
			if (e)
				return e;
			return store_result(s, i, v);
		case 18u: /* get_prop_addr */
			e = zvm_object_prop_addr(s, (uint8_t)a[0], (uint8_t)a[1], &address, &len);
			if (e)
				return e;
			return store_result(s, i, (uint16_t)address);
		case 19u: /* get_next_prop */
			e = zvm_object_next_prop(s, (uint8_t)a[0], (uint8_t)a[1], &b);
			if (e)
				return e;
			return store_result(s, i, b);
		case 20u: /* add */
			return store_result(s, i, wrap16((uint32_t)a[0] + a[1]));
		case 21u: /* sub */
			return store_result(s, i, wrap16((uint32_t)a[0] - a[1]));
		case 22u: /* mul */
			return store_result(s, i, (uint16_t)(a[0] * a[1]));
		case 23u: /* div */
			x = signed16(a[0]);
			y = signed16(a[1]);
			if (!y)
				return ZVM_EDIVZERO;
			r = (uint16_t)(x / y);
			return store_result(s, i, r);
		case 24u: /* mod */
			x = signed16(a[0]);
			y = signed16(a[1]);
			if (!y)
				return ZVM_EDIVZERO;
			r = (uint16_t)(x % y);
			return store_result(s, i, r);
		case 25u: /* call_2s */
			return call_op(s, i, a, 2);
		case 26u: /* call_2n (unsupported) */
			return ZVM_EUNSUPPORTED;
		default:
			return ZVM_EUNSUPPORTED;
		}
	}
	if (i->opcode >= 224u) {
		/* sread */
		if (i->opcode == 228u)
			return suspend_for_input(s, i, a);
		/* random */
		if (i->opcode == 231u) {
			uint16_t result = 0;
			int32_t range;
			e = need(i, 1);
			if (e)
				return e;
			range = signed16(a[0]);
			if (range < 0) {
				s->random_state = (uint16_t)(-range);
				if (!s->random_state)
					s->random_state = 1;
			} else if (range == 0) {
				s->random_state = (uint16_t)(random_step(s) * 25173u + 13849u);
				if (!s->random_state)
					s->random_state = 1;
			} else
				result = (uint16_t)(random_step(s) % (uint16_t)range + 1u);
			return store_result(s, i, result);
		}
		switch (i->opcode) {
		case 224u: /* call */
			return call_op(s, i, a, 1);
		case 225u: /* storew */
			e = need(i, 3);
			if (e)
				return e;
			address = wrap16((uint32_t)a[0] + ((uint32_t)signed16(a[1]) << 1));
			return store_word(s, (uint16_t)address, a[2]);
		case 226u: /* storeb */
			e = need(i, 3);
			if (e)
				return e;
			address = wrap16((uint32_t)a[0] + (uint32_t)signed16(a[1]));
			return zvm_mem_write(s, address, (uint8_t)a[2]);
		case 227u: /* put_prop */
			e = need(i, 3);
			if (e)
				return e;
			e = zvm_object_prop_addr(s, (uint8_t)a[0], (uint8_t)a[1], &address, &len);
			if (e)
				return e;
			if (!address)
				return ZVM_EINVAL;
			if (len == 1u)
				return zvm_mem_write(s, address, (uint8_t)a[2]);
			if (len == 2u)
				return store_word(s, (uint16_t)address, a[2]);
			return ZVM_EINVAL;
		case 229u: /* print_char */
			e = need(i, 1);
			if (e)
				return e;
			b = (uint8_t)a[0];
			return zvm_output_byte(s, ZVM_OUTPUT_NARRATIVE,
					       b == 13u ? '\n' : (b >= 32u && b <= 126u ? b : '?'));
		case 230u: /* print_num */
			e = need(i, 1);
			if (e)
				return e;
			return print_number(s, signed16(a[0]));
		case 232u: /* push */
			e = need(i, 1);
			if (e)
				return e;
			return zvm_variable_write(s, 0, a[0], 0);
		case 233u: /* pull */
			e = need(i, 1);
			if (e)
				return e;
			e = zvm_variable_read(s, 0, &v, 0);
			if (e)
				return e;
			return zvm_variable_write(s, (uint8_t)a[0], v, 1);
		default:
			return ZVM_EUNSUPPORTED;
		}
	}
	return ZVM_EUNSUPPORTED;
}
