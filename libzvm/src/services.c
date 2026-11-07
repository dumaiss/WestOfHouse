/* Build status events from story globals and send them to the host adapter.
 * Presentation and terminal access belong to the application. */
#include "zvm_internal.h"

zvm_error_t zvm_update_status(zvm_state_t *s)
{
	uint16_t location, value1, value2;
	uint8_t flags;
	uint32_t addr;
	zvm_error_t e;
	if (!s->status_callback && !s->output)
		return ZVM_OK;
	e = zvm_mem_word(s, s->globals_addr, &location);
	if (e)
		return e;
	e = zvm_mem_word(s, (uint32_t)s->globals_addr + 2ul, &value1);
	if (e)
		return e;
	e = zvm_mem_word(s, (uint32_t)s->globals_addr + 4ul, &value2);
	if (e)
		return e;
	e = zvm_mem_read(s, 1, &flags);
	if (e)
		return e;
	if (location) {
		e = zvm_object_name_addr(s, (uint8_t)location, &addr);
		if (e)
			return e;
		e = zvm_text_print(s, addr, ZVM_OUTPUT_STATUS, 0);
		if (e)
			return e;
	}
	if (s->status_callback)
		return s->status_callback(s->status_user, location, value1, value2,
					  (uint8_t)((flags >> 1) & 1u));
	return ZVM_OK;
}
