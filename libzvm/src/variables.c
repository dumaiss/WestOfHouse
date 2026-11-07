/* Z-machine variables: evaluation stack (0), locals (1..15), globals (16..255).
 * Indirect access to variable zero peeks/replaces instead of popping/pushing. */
#include "zvm_internal.h"

zvm_error_t zvm_variable_read(zvm_state_t *s, uint8_t var, uint16_t *value, uint8_t indirect)
{
	zvm_frame_t *f;
	uint32_t address;
	if (!s || !value || !s->frame_count)
		return ZVM_ESTATE;
	if (var == 0u) {
		if (s->sp <= s->frames[s->frame_count - 1u].stack_base)
			return ZVM_ESTACK;
		if (indirect)
			*value = s->stack[s->sp - 1u];
		else
			*value = s->stack[--s->sp];
		return ZVM_OK;
	}
	f = &s->frames[s->frame_count - 1u];
	if (var < 16u) {
		if (var > f->local_count)
			return ZVM_EINVAL;
		*value = f->locals[var - 1u];
		return ZVM_OK;
	}
	address = (uint32_t)s->globals_addr + ((uint32_t)(var - 16u) << 1);
	return zvm_mem_word(s, address, value);
}

zvm_error_t zvm_variable_write(zvm_state_t *s, uint8_t var, uint16_t value, uint8_t indirect)
{
	zvm_frame_t *f;
	uint32_t address;
	if (!s || !s->frame_count)
		return ZVM_ESTATE;
	if (var == 0u) {
		if (indirect) {
			if (s->sp <= s->frames[s->frame_count - 1u].stack_base)
				return ZVM_ESTACK;
			s->stack[s->sp - 1u] = value;
			return ZVM_OK;
		}
		if (s->sp >= ZVM_STACK_CAPACITY)
			return ZVM_ESTACK;
		s->stack[s->sp++] = value;
		return ZVM_OK;
	}
	f = &s->frames[s->frame_count - 1u];
	if (var < 16u) {
		if (var > f->local_count)
			return ZVM_EINVAL;
		f->locals[var - 1u] = value;
		return ZVM_OK;
	}
	address = (uint32_t)s->globals_addr + ((uint32_t)(var - 16u) << 1);
	ZVM_COUNT(s, word_writes);
	{
		zvm_error_t e = zvm_mem_write(s, address, (uint8_t)(value >> 8));
		if (e)
			return e;
	}
	return zvm_mem_write(s, address + 1ul, (uint8_t)value);
}

zvm_error_t zvm_stack_push(zvm_t *vm, uint16_t value)
{
	return vm ? zvm_variable_write(ZVM_STATE(vm), 0, value, 0) : ZVM_EINVAL;
}

zvm_error_t zvm_stack_pop(zvm_t *vm, uint16_t *value)
{
	return vm ? zvm_variable_read(ZVM_STATE(vm), 0, value, 0) : ZVM_EINVAL;
}

zvm_error_t zvm_global(const zvm_t *vm, uint8_t index, uint16_t *value)
{
	if (!vm || index >= 240u)
		return ZVM_EINVAL;
	return zvm_mem_word(ZVM_CSTATE(vm),
			    (uint32_t)ZVM_CSTATE(vm)->globals_addr + ((uint32_t)index << 1), value);
}
