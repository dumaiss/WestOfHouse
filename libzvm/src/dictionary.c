/* V3 dictionary-key encoding, sorted/unsorted lookup and input tokenization.
 * Dictionary keys hold six Z-characters; parse entries retain full token lengths. */
#include "zvm_internal.h"

static zvm_error_t encode_word(const uint8_t *word, uint8_t length, uint8_t out[4])
{
	uint8_t zs[6], count = 0, i, j, base[4];
	for (i = 0; i < length && count < 6u; ++i) {
		uint8_t c = word[i], n = 0;
		if (c >= 'a' && c <= 'z') {
			base[0] = (uint8_t)(c - 'a' + 6);
			n = 1;
		} else if (c >= 'A' && c <= 'Z') {
			base[0] = 4;
			base[1] = (uint8_t)(c - 'A' + 6);
			n = 2;
		} else {
			static const char punct[] = " 0123456789.,!?_#'\"/\\-:()";
			for (j = 1; j < sizeof(punct) - 1u; ++j)
				if ((uint8_t)punct[j] == c)
					break;
			if (c == ' ') {
				base[0] = 0;
				n = 1;
			} else if (j < sizeof(punct) - 1u) {
				base[0] = 5;
				base[1] = (uint8_t)(j + 8u);
				n = 2;
			} else {
				base[0] = 5;
				base[1] = 6;
				base[2] = (uint8_t)(c >> 5);
				base[3] = (uint8_t)(c & 31u);
				n = 4;
			}
		}
		if ((uint8_t)(count + n) > 6u)
			break;
		for (j = 0; j < n; ++j)
			zs[count++] = base[j];
	}
	while (count < 6u)
		zs[count++] = 5;
	for (i = 0; i < 2u; ++i) {
		uint16_t w = (uint16_t)(((uint16_t)zs[i * 3u] << 10) |
					((uint16_t)zs[i * 3u + 1u] << 5) | zs[i * 3u + 2u]);
		if (i == 1u)
			w |= 0x8000u;
		out[i * 2u] = (uint8_t)(w >> 8);
		out[i * 2u + 1u] = (uint8_t)w;
	}
	return ZVM_OK;
}

/* Repeated addition deliberately avoids an SDCC mixed-width multiply helper.
 * Keep this target constraint in mind when changing address arithmetic. */
static uint32_t entry_span(uint16_t count, uint8_t length)
{
	uint32_t result = 0;
	uint8_t i;
	for (i = 0; i < length; ++i)
		result += (uint32_t)count;
	return result;
}

zvm_error_t zvm_dictionary_lookup(zvm_state_t *s, const uint8_t *word, uint8_t n, uint16_t *result)
{
	uint8_t sep_count, entry_len, key[4], b;
	uint16_t count, dict;
	uint32_t base, lo, hi, mid, at;
	zvm_error_t e;
	if (!s || !word || !result)
		return ZVM_EINVAL;
	ZVM_COUNT(s, dictionary_lookups);
	*result = 0;
	dict = s->dictionary_addr;
	if (!dict)
		return ZVM_OK;
	e = zvm_mem_read(s, dict, &sep_count);
	if (e)
		return e;
	if ((uint32_t)dict + 1ul + sep_count + 3ul > s->story_size)
		return ZVM_EHEADER;
	e = zvm_mem_read(s, (uint32_t)dict + 1ul + sep_count, &entry_len);
	if (e)
		return e;
	if (entry_len < 4u)
		return ZVM_EHEADER;
	e = zvm_mem_word(s, (uint32_t)dict + 2ul + sep_count, &count);
	if (e)
		return e;
	base = (uint32_t)dict + 4ul + sep_count;
	if ((uint32_t)count > 0x7fffu)
		count = (uint16_t)(0u - count);
	if (base + entry_span(count, entry_len) > s->story_size)
		return ZVM_EHEADER;
	encode_word(word, n, key);
	/* A negative count marks an unsorted dictionary; scan it in place. */
	{
		uint16_t raw;
		e = zvm_mem_word(s, (uint32_t)dict + 2ul + sep_count, &raw);
		if (e)
			return e;
		if (raw & 0x8000u) {
			for (mid = 0; mid < count; ++mid) {
				at = base + entry_span((uint16_t)mid, entry_len);
				for (b = 0; b < 4u; ++b) {
					uint8_t x;
					e = zvm_mem_read(s, at + b, &x);
					if (e)
						return e;
					ZVM_COUNT(s, dictionary_comparisons);
					if (x != key[b])
						break;
				}
				if (b == 4u) {
					*result = (uint16_t)at;
					return ZVM_OK;
				}
			}
			return ZVM_OK;
		}
	}
	lo = 0;
	hi = count;
	while (lo < hi) {
		uint8_t x = 0;
		mid = lo + (hi - lo) / 2ul;
		at = base + entry_span((uint16_t)mid, entry_len);
		for (b = 0; b < 4u; ++b) {
			e = zvm_mem_read(s, at + b, &x);
			if (e)
				return e;
			ZVM_COUNT(s, dictionary_comparisons);
			if (x != key[b])
				break;
		}
		if (b == 4u) {
			*result = (uint16_t)at;
			return ZVM_OK;
		}
		if (key[b] < x)
			hi = mid;
		else
			lo = mid + 1ul;
	}
	return ZVM_OK;
}

static zvm_error_t is_separator(zvm_state_t *s, uint8_t c, uint8_t *yes)
{
	uint8_t n, k, x;
	zvm_error_t e;
	*yes = 0;
	if (!s->dictionary_addr)
		return ZVM_OK;
	e = zvm_mem_read(s, s->dictionary_addr, &n);
	if (e)
		return e;
	for (k = 0; k < n; ++k) {
		e = zvm_mem_read(s, (uint32_t)s->dictionary_addr + 1ul + k, &x);
		if (e)
			return e;
		if (x == c) {
			*yes = 1;
			break;
		}
	}
	return ZVM_OK;
}

zvm_error_t zvm_tokenize(zvm_state_t *s, uint16_t text, uint16_t parse)
{
	uint8_t capacity, count = 0, textcap, n, c, sep, start, pos, wordbuf[6], seen;
	uint16_t dict;
	uint32_t cursor;
	zvm_error_t e;
	e = zvm_mem_read(s, text, &textcap);
	if (e)
		return e;
	e = zvm_mem_read(s, parse, &capacity);
	if (e)
		return e;
	if ((uint32_t)parse + 2ul + (uint32_t)capacity * 4ul > s->static_base)
		return ZVM_EBOUNDS;
	cursor = (uint32_t)text + 1ul;
	pos = 1;
	while (pos < textcap) {
		e = zvm_mem_read(s, cursor, &c);
		if (e)
			return e;
		if (!c)
			break;
		e = is_separator(s, c, &sep);
		if (e)
			return e;
		if (c == ' ') {
			++cursor;
			++pos;
			continue;
		}
		start = pos;
		if (sep) {
			wordbuf[0] = c;
			dict = 0;
			e = zvm_dictionary_lookup(s, wordbuf, 1, &dict);
			if (e)
				return e;
			++cursor;
			++pos;
			n = 1;
		} else {
			n = 0;
			seen = 0;
			while (pos < textcap) {
				e = zvm_mem_read(s, cursor, &c);
				if (e)
					return e;
				if (!c || c == ' ')
					break;
				e = is_separator(s, c, &sep);
				if (e)
					return e;
				if (sep)
					break;
				if (seen < sizeof(wordbuf))
					wordbuf[seen++] =
					    (c >= 'A' && c <= 'Z') ? (uint8_t)(c + ('a' - 'A')) : c;
				if (n < 255u)
					++n;
				++cursor;
				++pos;
			}
			dict = 0;
			e = zvm_dictionary_lookup(s, wordbuf, n, &dict);
			if (e)
				return e;
		}
		if (count < capacity) {
			uint32_t out = (uint32_t)parse + 2ul + (uint32_t)count * 4ul;
			e = zvm_mem_write(s, out, (uint8_t)(dict >> 8));
			if (e)
				return e;
			e = zvm_mem_write(s, out + 1ul, (uint8_t)dict);
			if (e)
				return e;
			e = zvm_mem_write(s, out + 2ul, n);
			if (e)
				return e;
			e = zvm_mem_write(s, out + 3ul, start);
			if (e)
				return e;
			++count;
		}
	}
	return zvm_mem_write(s, (uint32_t)parse + 1ul, count);
}
