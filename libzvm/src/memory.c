/* Logical story-memory boundary shared by flat and callback backends.
 * Words are big-endian; every byte access remains independently checked. */
#include "zvm_internal.h"

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
	return s->read_byte(s->memory_user, a, out);
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
	return s->write_byte(s->memory_user, a, value);
}

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
