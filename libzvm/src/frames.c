/* Bounded Z-machine call frames, separate from the native C call stack.
 * Each frame owns its locals and the evaluation-stack suffix above stack_base. */
#include "zvm_internal.h"

zvm_error_t zvm_call_routine(zvm_state_t *s, uint16_t packed, const uint16_t *args, uint8_t argc,
			     uint8_t store_var, uint8_t has_store, uint32_t return_pc)
{
	uint32_t address, first;
	uint8_t locals, n;
	zvm_frame_t *frame;
	uint16_t initial;
	zvm_error_t e;
	if (!s || argc > 3u)
		return ZVM_EINVAL;
	ZVM_COUNT(s, calls);
	if (!packed)
		return has_store ? zvm_variable_write(s, store_var, 0, 0) : ZVM_OK;
	if (s->frame_count >= ZVM_FRAME_CAPACITY)
		return ZVM_EFRAME;
	/* V3 packed routine addresses are word addresses. */
	address = (uint32_t)packed << 1;
	e = zvm_mem_read(s, address, &locals);
	if (e)
		return e;
	if (locals > 15u)
		return ZVM_EINVAL;
	first = address + 1ul + ((uint32_t)locals << 1);
	if (first > s->story_size)
		return ZVM_EBOUNDS;
	frame = &s->frames[s->frame_count];
	frame->return_pc = return_pc;
	frame->stack_base = s->sp;
	frame->local_count = locals;
	frame->store_var = store_var;
	frame->has_store = has_store;
	frame->argument_count = argc;
	/* Read V3 local defaults even when an argument supplies the final value. */
	for (n = 0; n < locals; ++n) {
		e = zvm_mem_word(s, address + 1ul + ((uint32_t)n << 1), &initial);
		if (e)
			return e;
		frame->locals[n] = (n < argc) ? args[n] : initial;
	}
	++s->frame_count;
	s->pc = first;
	return ZVM_OK;
}

zvm_error_t zvm_return_value(zvm_state_t *s, uint16_t value)
{
	zvm_frame_t frame;
	zvm_error_t e;
	if (!s || s->frame_count <= 1u)
		return ZVM_ESTATE;
	ZVM_COUNT(s, returns);
	frame = s->frames[s->frame_count - 1u];
	/* Discard callee evaluation values before storing into the caller. */
	s->sp = frame.stack_base;
	--s->frame_count;
	s->pc = frame.return_pc;
	if (frame.has_store) {
		e = zvm_variable_write(s, frame.store_var, value, 0);
		if (e)
			return e;
	}
	return ZVM_OK;
}
