/* Streaming V3 execution core. Operands are evaluated as they are fetched;
 * handlers consume their own store and branch bytes. No decoded-instruction
 * object or second dispatch pass sits on the execution path. */
#include "zvm_internal.h"

#if defined(__SDCC) && !(defined(ZVM_PROFILE) && ZVM_PROFILE)
/* compact_fetch.asm uses the established SDCC context layout. */
typedef char compact_pc_at_33[__builtin_offsetof(zvm_state_t, pc) == 33 ? 1 : -1];
zvm_error_t zvm_compact_next(zvm_state_t *s, uint8_t *v);
#define next zvm_compact_next
#else
static zvm_error_t next(zvm_state_t *s, uint8_t *v)
{
	zvm_error_t e = zvm_mem_fetch(s, s->pc, v);
	if (!e) { ++s->pc; ZVM_COUNT(s, decoder_fetches); }
	return e;
}

#endif

static zvm_error_t store(zvm_state_t *s, uint16_t v)
{
	uint8_t n;
	zvm_error_t e = next(s, &n);
	return e ? e : zvm_variable_write(s, n, v, 0);
}

static zvm_error_t relative(zvm_state_t *s, int16_t displacement)
{
	int32_t p = (int32_t)s->pc + displacement - 2L;
	if (p < 0) return ZVM_EBOUNDS;
	s->pc = (uint32_t)p;
	return ZVM_OK;
}

static zvm_error_t branch(zvm_state_t *s, uint8_t yes)
{
	uint8_t a, b;
	uint16_t n;
	zvm_error_t e = next(s, &a);
	if (e) return e;
	n = a & 63u;
	if (!(a & 64u)) {
		e = next(s, &b);
		if (e) return e;
		n = (n << 8) | b;
		if (n & 8192u) n |= 0xc000u;
	}
	ZVM_COUNT(s, branches);
	if (!!yes != !!(a & 128u)) return ZVM_OK;
	ZVM_COUNT(s, branches_taken);
	if (n < 2u) return zvm_return_value(s, n);
	return relative(s, (int16_t)n);
}

static zvm_error_t word_write(zvm_state_t *s, uint16_t at, uint16_t v)
{
	zvm_error_t e;
	if ((uint32_t)at + 1ul >= s->static_base) return ZVM_EREADONLY;
	ZVM_COUNT(s, word_writes);
	e = zvm_mem_write(s, at, (uint8_t)(v >> 8));
	return e ? e : zvm_mem_write(s, (uint32_t)at + 1ul, (uint8_t)v);
}

static uint8_t less(uint16_t a, uint16_t b)
{
	return (uint16_t)(a ^ 0x8000u) < (uint16_t)(b ^ 0x8000u);
}

static uint16_t magnitude(uint16_t n)
{
	return n & 0x8000u ? (uint16_t)(0u - n) : n;
}

static uint16_t random_step(zvm_state_t *s)
{
	uint16_t x = s->random_state;
	if (!x) x = 0xace1u;
	x ^= (uint16_t)(x << 7);
	x ^= (uint16_t)(x >> 9);
	x ^= (uint16_t)(x << 8);
	if (!x) x = 0xace1u;
	s->random_state = x;
	return x;
}

static zvm_error_t number(zvm_state_t *s, uint16_t n)
{
	uint8_t digits[5], k = 0;
	zvm_error_t e;
	if (n & 0x8000u) {
		e = zvm_output_byte(s, ZVM_OUTPUT_NARRATIVE, '-');
		if (e) return e;
		n = magnitude(n);
	}
	do { digits[k++] = (uint8_t)('0' + n % 10u); n /= 10u; } while (n);
	while (k) {
		e = zvm_output_byte(s, ZVM_OUTPUT_NARRATIVE, digits[--k]);
		if (e) return e;
	}
	return ZVM_OK;
}

static zvm_error_t call(zvm_state_t *s, const uint16_t *a, uint8_t n)
{
	uint8_t dest;
	zvm_error_t e = next(s, &dest);
	if (e) return e;
	return zvm_call_routine(s, a[0], a + 1, n - 1u, dest, 1, s->pc);
}

static zvm_error_t zeroop(zvm_state_t *s, uint8_t op)
{
	zvm_error_t e;
	uint16_t v;
	uint32_t end;
	switch (op) {
	case 176: return zvm_return_value(s, 1);
	case 177: return zvm_return_value(s, 0);
	case 178: case 179:
		e = zvm_text_print(s, s->pc, ZVM_OUTPUT_NARRATIVE, &end);
		if (e) return e;
		s->pc = end;
		if (op == 178) return ZVM_OK;
		e = zvm_output_byte(s, ZVM_OUTPUT_NARRATIVE, '\n');
		return e ? e : zvm_return_value(s, 1);
	case 180: return ZVM_OK;
	case 181: case 182: return branch(s, 0);
	case 183: return ZVM_EUNSUPPORTED;
	case 184: case 185:
		e = zvm_variable_read(s, 0, &v, 0);
		return e || op == 185 ? e : zvm_return_value(s, v);
	case 186: s->status = ZVM_FINISHED; return ZVM_OK;
	case 187: return zvm_output_byte(s, ZVM_OUTPUT_NARRATIVE, '\n');
	case 188: return zvm_update_status(s);
	case 190: return ZVM_EUNSUPPORTED;
	case 189: return branch(s, s->checksum_valid && s->story_checksum == s->header_checksum);
	default: return ZVM_EINVAL;
	}
}

static zvm_error_t oneop(zvm_state_t *s, uint8_t op, uint16_t a)
{
	zvm_error_t e;
	uint16_t v;
	uint8_t parent, sibling, child, len;
	uint32_t at;
	switch (op) {
	case 128: return branch(s, a == 0);
	case 129: case 130: case 131:
		e = zvm_object_links_internal(s, (uint8_t)a, &parent, &sibling, &child);
		if (e) return e;
		v = op == 129 ? sibling : op == 130 ? child : parent;
		e = store(s, v);
		return e || op == 131 ? e : branch(s, v != 0);
	case 132:
		e = zvm_object_prop_length(s, a, &len);
		return e ? e : store(s, len);
	case 133: case 134:
		e = zvm_variable_read(s, (uint8_t)a, &v, 1);
		if (e) return e;
		v += op == 133 ? 1u : 0xffffu;
		return zvm_variable_write(s, (uint8_t)a, v, 1);
	case 135: return zvm_text_print(s, a, ZVM_OUTPUT_NARRATIVE, 0);
	case 136: return call(s, &a, 1);
	case 137: return zvm_object_remove(s, (uint8_t)a);
	case 138:
		e = zvm_object_name_addr(s, (uint8_t)a, &at);
		return e ? e : zvm_text_print(s, at, ZVM_OUTPUT_NARRATIVE, 0);
	case 139: return zvm_return_value(s, a);
	case 140: return relative(s, (int16_t)a);
	case 141: return zvm_text_print(s, (uint32_t)a << 1, ZVM_OUTPUT_NARRATIVE, 0);
	case 142:
		e = zvm_variable_read(s, (uint8_t)a, &v, 1);
		return e ? e : store(s, v);
	case 143: return store(s, (uint16_t)~a);
	default: return ZVM_EUNSUPPORTED;
	}
}

static zvm_error_t twoop(zvm_state_t *s, uint8_t op, const uint16_t *a, uint8_t n)
{
	zvm_error_t e;
	uint16_t v, at;
	uint32_t address;
	uint8_t b, parent, sibling, child, len, k;
	if (op == 1) {
		if (n < 2) return ZVM_EINVAL;
		b = 0;
		for (k = 1; k < n; ++k) if (a[0] == a[k]) b = 1;
		return branch(s, b);
	}
	if (n != 2) return ZVM_EINVAL;
	switch (op) {
	case 2: return branch(s, less(a[0], a[1]));
	case 3: return branch(s, less(a[1], a[0]));
	case 4: case 5:
		e = zvm_variable_read(s, (uint8_t)a[0], &v, 1);
		if (e) return e;
		v += op == 5 ? 1u : 0xffffu;
		e = zvm_variable_write(s, (uint8_t)a[0], v, 1);
		return e ? e : branch(s, op == 5 ? less(a[1], v) : less(v, a[1]));
	case 6:
		e = zvm_object_links_internal(s, (uint8_t)a[0], &parent, &sibling, &child);
		return e ? e : branch(s, parent == a[1]);
	case 7: return branch(s, (a[0] & a[1]) == a[1]);
	case 8: return store(s, a[0] | a[1]);
	case 9: return store(s, a[0] & a[1]);
	case 10: case 11: case 12:
		e = zvm_object_attribute_internal(s, (uint8_t)a[0], (uint8_t)a[1], &b,
					      op == 10 ? 0 : op == 11 ? 1 : 2);
		return e || op != 10 ? e : branch(s, b);
	case 13: return zvm_variable_write(s, (uint8_t)a[0], a[1], 1);
	case 14: return zvm_object_insert(s, (uint8_t)a[0], (uint8_t)a[1]);
	case 15:
		at = a[0] + (uint16_t)(a[1] << 1);
		e = zvm_mem_word(s, at, &v);
		return e ? e : store(s, v);
	case 16:
		at = a[0] + a[1];
		e = zvm_mem_read(s, at, &b);
		return e ? e : store(s, b);
	case 17:
		e = zvm_object_get_prop(s, (uint8_t)a[0], (uint8_t)a[1], &v);
		return e ? e : store(s, v);
	case 18:
		e = zvm_object_prop_addr(s, (uint8_t)a[0], (uint8_t)a[1], &address, &len);
		return e ? e : store(s, (uint16_t)address);
	case 19:
		e = zvm_object_next_prop(s, (uint8_t)a[0], (uint8_t)a[1], &b);
		return e ? e : store(s, b);
	case 20: return store(s, a[0] + a[1]);
	case 21: return store(s, a[0] - a[1]);
	case 22: return store(s, (uint16_t)(a[0] * (unsigned int)a[1]));
	case 23: case 24:
		if (!a[1]) return ZVM_EDIVZERO;
		v = op == 23 ? magnitude(a[0]) / magnitude(a[1]) : magnitude(a[0]) % magnitude(a[1]);
		if (op == 23 ? (a[0] ^ a[1]) & 0x8000u : a[0] & 0x8000u) v = 0u - v;
		return store(s, v);
	case 25: return call(s, a, n);
	default: return ZVM_EUNSUPPORTED;
	}
}

static zvm_error_t varop(zvm_state_t *s, uint8_t op, const uint16_t *a, uint8_t n)
{
	zvm_error_t e;
	uint16_t at, v;
	uint32_t address;
	uint8_t b, len;
	if (op == 224) return n ? call(s, a, n) : ZVM_EINVAL;
	if (op >= 225 && op <= 227) {
		if (n != 3) return ZVM_EINVAL;
		if (op == 225) return word_write(s, a[0] + (uint16_t)(a[1] << 1), a[2]);
		if (op == 226) { at = a[0] + a[1]; return zvm_mem_write(s, at, (uint8_t)a[2]); }
		e = zvm_object_prop_addr(s, (uint8_t)a[0], (uint8_t)a[1], &address, &len);
		if (e) return e;
		if (!address) return ZVM_EINVAL;
		if (len == 1) return zvm_mem_write(s, address, (uint8_t)a[2]);
		return len == 2 ? word_write(s, (uint16_t)address, a[2]) : ZVM_EINVAL;
	}
	if (op == 228) {
		if (n != 2) return ZVM_EINVAL;
		e = zvm_update_status(s);
		if (e) return e;
		s->pending_text = a[0]; s->pending_parse = a[1];
		s->input_pending = 1; s->status = ZVM_NEED_INPUT;
		return ZVM_OK;
	}
	if (op > 233) return ZVM_EUNSUPPORTED;
	if (n != 1) return ZVM_EINVAL;
	switch (op) {
	case 229:
		b = (uint8_t)a[0];
		return zvm_output_byte(s, ZVM_OUTPUT_NARRATIVE,
				       b == 13 ? '\n' : b >= 32 && b <= 126 ? b : '?');
	case 230: return number(s, a[0]);
	case 231:
		v = 0;
		if (a[0] & 0x8000u) s->random_state = magnitude(a[0]);
		else if (!a[0]) {
			s->random_state = (uint16_t)(random_step(s) * 25173u + 13849u);
			if (!s->random_state) s->random_state = 1;
		} else v = random_step(s) % a[0] + 1u;
		return store(s, v);
	case 232: return zvm_variable_write(s, 0, a[0], 0);
	case 233:
		e = zvm_variable_read(s, 0, &v, 0);
		return e ? e : zvm_variable_write(s, (uint8_t)a[0], v, 1);
	default: return ZVM_EUNSUPPORTED;
	}
}

static zvm_error_t instruction(zvm_state_t *s)
{
	uint16_t a[4], v;
	uint8_t raw, op, types, kind, n = 0, b, indirect, k;
	zvm_error_t e = next(s, &raw);
	if (e) return e;
	if (raw < 128) {
		op = raw & 31u; kind = 2;
		types = (raw & 64u ? 128u : 64u) | (raw & 32u ? 32u : 16u) | 15u;
	} else if (raw < 192) {
		types = (raw & 48u) << 2;
		if (types == 192u) {
			s->pending_opcode = (raw & 15u) + 176u;
			ZVM_COUNT(s, instructions);
			ZVM_COUNT(s, op_0op);
			return zeroop(s, s->pending_opcode);
		}
		op = (raw & 15u) + 128u; kind = 1; types |= 63u;
	} else {
		op = raw & 32u ? (raw & 31u) + 224u : raw & 31u;
		kind = raw & 32u ? 3 : 2;
		e = next(s, &types);
		if (e) return e;
	}
	s->pending_opcode = op;
#if defined(ZVM_PROFILE) && ZVM_PROFILE
	if (kind == 1) { ZVM_COUNT(s, op_1op); }
	else if (kind == 2) { ZVM_COUNT(s, op_2op); }
	else { ZVM_COUNT(s, op_var); }
#endif
	indirect = kind == 1 ? (op == 133 || op == 134 || op == 142) :
		   kind == 2 ? (op == 4 || op == 5 || op == 13) : op == 233;
	for (k = 0; k < 4; ++k) {
		b = types >> 6;
		if (b == 3) {
			if (types != (uint8_t)(255u << (2u * k))) return ZVM_EINVAL;
			break;
		}
		types <<= 2;
		e = next(s, &raw);
		if (e) return e;
		v = raw;
		if (!b) {
			e = next(s, &raw);
			if (e) return e;
			v = (v << 8) | raw;
		} else if (b == 2 && !(k == 0 && indirect)) {
			e = zvm_variable_read(s, raw, &v, 0);
			if (e) return e;
		}
		a[n++] = v;
	}
	ZVM_COUNT(s, instructions);
	if (kind == 1) return n == 1 ? oneop(s, op, a[0]) : ZVM_EINVAL;
	return kind == 2 ? twoop(s, op, a, n) : varop(s, op, a, n);
}

zvm_status_t zvm_run(zvm_t *vm, uint16_t budget)
{
	zvm_state_t *s;
	zvm_error_t e;
	if (!vm) return ZVM_FAULT;
	s = ZVM_STATE(vm);
	if (!s->story_size || (!s->flat_story && !s->read_byte)) return ZVM_FAULT;
	if (s->status != ZVM_READY && s->status != ZVM_YIELDED) return s->status;
	s->status = ZVM_READY;
	while (budget--) {
		s->previous_address = s->last_address;
		s->last_address = s->fault_address = s->pc;
		e = instruction(s);
		if (e) { s->error = e; s->status = ZVM_FAULT; return s->status; }
		if (s->status != ZVM_READY) return s->status;
	}
	ZVM_COUNT(s, yields);
	s->status = ZVM_YIELDED;
	return s->status;
}

