/* Complete a pending sread request using host-supplied, NUL-terminated text.
 * Input normalization and parse-table updates operate through logical memory. */
#include "zvm_internal.h"

zvm_error_t zvm_submit_input(zvm_t *vm, const char *text)
{
	zvm_state_t *s;
	uint8_t capacity, c;
	uint16_t length = 0;
	uint32_t at;
	zvm_error_t e;
	if (!vm || !text)
		return ZVM_EINVAL;
	s = ZVM_STATE(vm);
	if (s->status != ZVM_NEED_INPUT || !s->input_pending)
		return ZVM_ESTATE;
	e = zvm_mem_read(s, s->pending_text, &capacity);
	if (e)
		return e;
	if (!capacity)
		return ZVM_EHEADER;
	while (text[length] && length < (uint16_t)(capacity - 1u))
		++length;
	at = (uint32_t)s->pending_text + 1ul;
	for (c = 0; c < (uint8_t)length; ++c) {
		uint8_t ch = (uint8_t)text[c];
		if (ch >= 'A' && ch <= 'Z')
			ch = (uint8_t)(ch + ('a' - 'A'));
		if (ch < 32u || ch > 126u)
			ch = '?';
		e = zvm_mem_write(s, at + c, ch);
		if (e)
			return e;
	}
	e = zvm_mem_write(s, at + length, 0);
	if (e)
		return e;
	e = zvm_tokenize(s, s->pending_text, s->pending_parse);
	if (e)
		return e;
	s->input_pending = 0;
	s->status = ZVM_READY;
	return ZVM_OK;
}
