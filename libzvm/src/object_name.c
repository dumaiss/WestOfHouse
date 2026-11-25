/* Object addressing and short names used by the resident status callback.
 * Keep property mutation code outside this callback dependency. */
#include "zvm_internal.h"

zvm_error_t zvm_object_entry_address(const zvm_state_t *s, uint8_t object, uint32_t *address)
{
	uint32_t p;
	if (!s || !address || !object)
		return ZVM_EINVAL;
	{
		uint32_t n = (uint32_t)object - 1ul;
		p = (uint32_t)s->objects_addr + 62ul + (n << 3) + n;
	}
	if (p + 9ul > s->story_size)
		return ZVM_EBOUNDS;
	*address = p;
	return ZVM_OK;
}

zvm_error_t zvm_object_name_addr(const zvm_state_t *s, uint8_t object, uint32_t *address)
{
	uint32_t p;
	uint16_t t;
	uint8_t words;
	zvm_error_t e;
	if (!address || !object)
		return ZVM_EINVAL;
	e = zvm_object_entry_address(s, object, &p);
	if (e)
		return e;
	e = zvm_mem_word(s, p + 7ul, &t);
	if (e)
		return e;
	e = zvm_mem_read(s, t, &words);
	if (e)
		return e;
	*address = (uint32_t)t + 1ul;
	return ZVM_OK;
}
