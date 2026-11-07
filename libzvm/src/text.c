/* Streaming V3 packed-text decoding, abbreviation expansion and byte output.
 * Decoder state uses logical addresses, never borrowed backend mappings. */
#include "zvm_internal.h"

#if defined(__SDCC) && defined(ZVM_ASM) && ZVM_ASM
/* SDCC sdcccall(1): word in HL, slot in DE, result in A. */
uint8_t zvm_unpack(uint16_t word, uint16_t slot);
#endif

zvm_error_t zvm_output_byte(zvm_state_t *s, uint8_t channel, uint8_t byte)
{
	if (byte == 0u)
		return ZVM_OK;
	if (byte == 13u)
		byte = '\n';
	else if (byte > 126u)
		byte = '?';
	ZVM_COUNT(s, text_chars);
#if defined(ZVM_PROFILE) && ZVM_PROFILE
	if (s->output) {
		ZVM_COUNT(s, output_calls);
		ZVM_COUNT(s, output_bytes);
	}
#endif
	return s->output ? s->output(s->output_user, channel, byte) : ZVM_OK;
}

/* slot == 3 requests another packed word. ended records its final-word bit;
 * the remaining slots in that word must still be consumed before stopping. */
static zvm_error_t get_z(zvm_state_t *s, uint32_t *pc, uint16_t *word, uint8_t *slot,
			 uint8_t *ended, uint8_t *out)
{
	zvm_error_t e;
	if (*slot == 3u) {
		if (*ended)
			return ZVM_EBOUNDS;
		e = zvm_mem_word(s, *pc, word);
		if (e)
			return e;
		*pc += 2ul;
		*ended = (uint8_t)((*word & 0x8000u) != 0);
		*slot = 0;
	}
#if defined(__SDCC) && defined(ZVM_ASM) && ZVM_ASM
	*out = zvm_unpack(*word, (uint16_t)*slot);
#else
	*out = (uint8_t)((*word >> (10u - 5u * (*slot))) & 31u);
#endif
	++*slot;
	return ZVM_OK;
}

/* alpha is the temporary alphabet shift; esc/eh collect a two-part ZSCII escape.
 * Recursive abbreviation expansion is bounded to protect the native stack. */
static zvm_error_t packed(zvm_state_t *s, uint32_t at, uint8_t channel, uint8_t depth,
			  uint32_t *next)
{
	static const char a2[] = " 0123456789.,!?_#'\"/\\-:()";
	uint16_t w, tab;
	uint8_t slot = 3, ended = 0, c, alpha = 0, esc = 0, eh = 0;
	uint32_t pc = at;
	zvm_error_t e;
	if (depth > 4u)
		return ZVM_EINVAL;
	for (;;) {
		e = get_z(s, &pc, &w, &slot, &ended, &c);
		if (e)
			return e;
		if (esc) {
			if (esc == 1u) {
				eh = (uint8_t)(c << 5);
				esc = 2u;
			} else {
				e = zvm_output_byte(s, channel, (uint8_t)(eh | c));
				if (e)
					return e;
				esc = 0;
				alpha = 0;
			}
			if (ended && slot == 3u)
				break;
			continue;
		}
		if (c == 0u) {
			e = zvm_output_byte(s, channel, ' ');
			alpha = 0;
		} else if (c >= 1u && c <= 3u) {
			uint8_t index;
			e = get_z(s, &pc, &w, &slot, &ended, &index);
			if (e)
				return e;
			ZVM_COUNT(s, abbreviations);
			if (depth >= 4u)
				return ZVM_EINVAL;
			e = zvm_mem_word(s,
					 (uint32_t)s->abbreviation_addr +
					     ((uint32_t)(c - 1u) * 64ul) + (uint32_t)index * 2ul,
					 &tab);
			if (e)
				return e;
			e = packed(s, (uint32_t)tab * 2ul, channel, (uint8_t)(depth + 1u), 0);
			alpha = 0;
		} else if (c == 4u || c == 5u) {
			alpha = (uint8_t)(c - 3u);
			e = ZVM_OK;
		} else if (alpha == 2u && c == 7u) {
			e = zvm_output_byte(s, channel, '\n');
			alpha = 0;
		} else if (!alpha) {
			e = zvm_output_byte(s, channel, (uint8_t)('a' + c - 6u));
			alpha = 0;
		} else if (alpha == 1u) {
			e = zvm_output_byte(s, channel, (uint8_t)('A' + c - 6u));
			alpha = 0;
		} else if (c == 6u) {
			esc = 1u;
			e = ZVM_OK;
		} else if (c <= 31u) {
			e = zvm_output_byte(s, channel, (uint8_t)a2[c - 7u]);
			alpha = 0;
		} else
			e = ZVM_EINVAL;
		if (e)
			return e;
		if (ended && slot == 3u)
			break;
	}
	if (next)
		*next = pc;
	return ZVM_OK;
}

zvm_error_t zvm_text_print(zvm_state_t *s, uint32_t address, uint8_t channel,
			   uint32_t *next_address)
{
	return packed(s, address, channel, 0, next_address);
}
