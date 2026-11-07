/* Cooperative instruction driver: decode, evaluate operands, then dispatch.
 * Errors become VM faults here; input and instruction budgets return to the host. */
#include "zvm_internal.h"

static zvm_error_t fault(zvm_state_t *s, zvm_error_t e)
{
	s->error = e;
	s->status = ZVM_FAULT;
	return e;
}

/* These instructions take a variable number as their first operand. Resolving
 * it like an ordinary variable operand would change stack-zero semantics. */
static uint8_t variable_number_operand(const zvm_instruction_t *i, uint8_t n)
{
	if (n != 0u)
		return 0;
	if (i->form == ZI_SHORT)
		return i->opcode == 133u || i->opcode == 134u || i->opcode == 142u;
	if (i->form == ZI_LONG || (i->form == ZI_VAR && i->opcode < 32u))
		return i->opcode == 4u || i->opcode == 5u || i->opcode == 13u;
	return i->form == ZI_VAR && i->opcode == 233u;
}

zvm_status_t zvm_run(zvm_t *vm, uint16_t budget)
{
	zvm_state_t *s;
	uint16_t used;
	zvm_instruction_t i;
	uint16_t args[4];
	zvm_error_t e;
	uint8_t n;
	if (!vm)
		return ZVM_FAULT;
	s = ZVM_STATE(vm);
	if (!s->story_size || (!s->flat_story && !s->read_byte))
		return ZVM_FAULT;
	if (s->status == ZVM_FINISHED || s->status == ZVM_FAULT || s->status == ZVM_NEED_INPUT ||
	    s->status == ZVM_NEED_SAVE || s->status == ZVM_NEED_RESTORE)
		return s->status;
	if (!budget) {
		ZVM_COUNT(s, yields);
		s->status = ZVM_YIELDED;
		return s->status;
	}
	s->status = ZVM_READY;
	for (used = 0; used < budget; ++used) {
		s->previous_address = s->last_address;
		s->last_address = s->pc;
		s->fault_address = s->pc;
		e = zvm_decode(s, s->pc, &i);
		if (e) {
			uint8_t raw = 0;
			if (zvm_mem_read(s, s->pc, &raw) == ZVM_OK)
				s->pending_opcode = raw;
			fault(s, e);
			return s->status;
		}
		s->pending_opcode = i.opcode;
		ZVM_COUNT(s, instructions);
#if defined(ZVM_PROFILE) && ZVM_PROFILE
		if (i.opcode >= 224u) {
			ZVM_COUNT(s, op_var);
		} else if (i.opcode >= 176u) {
			ZVM_COUNT(s, op_0op);
		} else if (i.opcode >= 128u) {
			ZVM_COUNT(s, op_1op);
		} else {
			ZVM_COUNT(s, op_2op);
		}
#endif
		for (n = 0; n < i.operand_count; ++n) {
			if (i.operand_types[n] == 2u && !variable_number_operand(&i, n)) {
				e = zvm_variable_read(s, (uint8_t)i.operands[n], &args[n], 0);
				if (e) {
					fault(s, e);
					return s->status;
				}
			} else
				args[n] = i.operands[n];
		}
		e = zvm_execute_instruction(s, &i, args);
		if (e) {
			fault(s, e);
			return s->status;
		}
		if (s->status == ZVM_NEED_INPUT)
			return s->status;
		if (s->status == ZVM_FINISHED)
			return s->status;
	}
	ZVM_COUNT(s, yields);
	s->status = ZVM_YIELDED;
	return s->status;
}
