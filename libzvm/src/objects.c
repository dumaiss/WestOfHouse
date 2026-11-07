/* V3 object tables are read and modified in story memory, without a shadow tree.
 * Object entries are nine bytes, following 31 two-byte default properties. */
#include "zvm_internal.h"

static zvm_error_t entry_address(const zvm_state_t *s, uint8_t object, uint32_t *address)
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

zvm_error_t zvm_object_links_internal(const zvm_state_t *s, uint8_t object, uint8_t *parent,
				      uint8_t *sibling, uint8_t *child)
{
	uint32_t p;
	zvm_error_t e;
	if (!parent || !sibling || !child)
		return ZVM_EINVAL;
	if (!object) {
		*parent = *sibling = *child = 0;
		return ZVM_OK;
	}
	e = entry_address(s, object, &p);
	if (e)
		return e;
	if (zvm_mem_read(s, p + 4ul, parent) || zvm_mem_read(s, p + 5ul, sibling) ||
	    zvm_mem_read(s, p + 6ul, child))
		return ZVM_EBOUNDS;
	return ZVM_OK;
}

/* change: 0 reads, 1 sets, 2 clears; attribute bits are most-significant first. */
zvm_error_t zvm_object_attribute_internal(const zvm_state_t *s, uint8_t object, uint8_t attr,
					  uint8_t *set, uint8_t change)
{
	uint32_t p;
	uint8_t b, mask;
	zvm_error_t e;
	if (!set || attr >= 32u || !object)
		return ZVM_EINVAL;
	e = entry_address(s, object, &p);
	if (e)
		return e;
	p += (uint32_t)(attr >> 3);
	mask = (uint8_t)(0x80u >> (attr & 7u));
	e = zvm_mem_read(s, p, &b);
	if (e)
		return e;
	*set = (uint8_t)((b & mask) != 0);
	if (change) {
		if (change == 1u)
			b |= mask;
		else
			b &= (uint8_t)~mask;
		e = zvm_mem_write((zvm_state_t *)s, p, b);
	}
	return e;
}

/* Skip the packed short name to locate the descending property list. */
static zvm_error_t prop_table(const zvm_state_t *s, uint8_t object, uint32_t *at)
{
	uint32_t p;
	uint16_t table;
	uint8_t words;
	zvm_error_t e;
	e = entry_address(s, object, &p);
	if (e)
		return e;
	e = zvm_mem_word(s, p + 7ul, &table);
	if (e)
		return e;
	e = zvm_mem_read(s, table, &words);
	if (e)
		return e;
	*at = (uint32_t)table + 1ul + ((uint32_t)words << 1);
	if (*at > s->story_size)
		return ZVM_EBOUNDS;
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
	e = entry_address(s, object, &p);
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

zvm_error_t zvm_object_prop_addr(const zvm_state_t *s, uint8_t object, uint8_t property,
				 uint32_t *address, uint8_t *length)
{
	uint32_t at;
	uint8_t h, num, size;
	zvm_error_t e;
	if (!address || !length || !property || property > 31u)
		return ZVM_EINVAL;
	*address = 0;
	*length = 0;
	e = prop_table(s, object, &at);
	if (e)
		return e;
	for (;;) {
		if (at >= s->story_size)
			return ZVM_EBOUNDS;
		e = zvm_mem_read(s, at++, &h);
		if (e)
			return e;
		if (!h)
			return ZVM_OK;
		num = h & 0x1fu;
		size = (uint8_t)((h >> 5) + 1u);
		if (num == property) {
			*address = at;
			*length = size;
			return ZVM_OK;
		}
		if (num < property)
			return ZVM_OK;
		at += size;
	}
}

zvm_error_t zvm_object_get_prop(const zvm_state_t *s, uint8_t object, uint8_t property,
				uint16_t *value)
{
	uint32_t a;
	uint8_t len, hi, lo;
	zvm_error_t e;
	if (!value)
		return ZVM_EINVAL;
	e = zvm_object_prop_addr(s, object, property, &a, &len);
	if (e)
		return e;
	if (!a) {
		return zvm_mem_word(s, (uint32_t)s->objects_addr + ((uint32_t)(property - 1u) << 1),
				    value);
	}
	e = zvm_mem_read(s, a, &hi);
	if (e)
		return e;
	if (len == 1u) {
		*value = hi;
		return ZVM_OK;
	}
	e = zvm_mem_read(s, a + 1ul, &lo);
	if (e)
		return e;
	*value = ((uint16_t)hi << 8) | lo;
	return ZVM_OK;
}

zvm_error_t zvm_object_next_prop(const zvm_state_t *s, uint8_t object, uint8_t property,
				 uint8_t *next)
{
	uint32_t at;
	uint8_t h, num;
	zvm_error_t e;
	if (!next || property > 31u)
		return ZVM_EINVAL;
	e = prop_table(s, object, &at);
	if (e)
		return e;
	if (!property) {
		e = zvm_mem_read(s, at, &h);
		if (e)
			return e;
		*next = h & 0x1fu;
		return ZVM_OK;
	}
	for (;;) {
		if (at >= s->story_size)
			return ZVM_EBOUNDS;
		e = zvm_mem_read(s, at++, &h);
		if (e)
			return e;
		if (!h)
			return ZVM_EINVAL;
		num = h & 0x1fu;
		if (num == property) {
			e = zvm_mem_read(s, at + ((uint32_t)(h >> 5) + 1ul), &h);
			if (e)
				return e;
			*next = h & 0x1fu;
			return ZVM_OK;
		}
		if (num < property)
			return ZVM_EINVAL;
		at += (uint32_t)(h >> 5) + 1ul;
	}
}

zvm_error_t zvm_object_prop_length(const zvm_state_t *s, uint32_t address, uint8_t *length)
{
	uint8_t h;
	zvm_error_t e;
	if (!length)
		return ZVM_EINVAL;
	if (!address) {
		*length = 0;
		return ZVM_OK;
	}
	if (address > s->story_size)
		return ZVM_EBOUNDS;
	e = zvm_mem_read(s, address - 1ul, &h);
	if (e)
		return e;
	if (!h)
		return ZVM_EINVAL;
	*length = (uint8_t)((h >> 5) + 1u);
	return ZVM_OK;
}

/* Unlink from the parent's child/sibling chain; traversal is bounded for
 * malformed stories. The removed object's own children are not moved. */
zvm_error_t zvm_object_remove(zvm_state_t *s, uint8_t object)
{
	uint8_t parent, sibling, child, prev, cur, n;
	uint32_t p, pp;
	zvm_error_t e;
	if (!object)
		return ZVM_OK;
	e = entry_address(s, object, &p);
	if (e)
		return e;
	e = zvm_object_links_internal(s, object, &parent, &sibling, &child);
	if (e)
		return e;
	if (!parent)
		return ZVM_OK;
	e = entry_address(s, parent, &pp);
	if (e)
		return e;
	e = zvm_mem_read(s, pp + 6ul, &cur);
	if (e)
		return e;
	prev = 0;
	for (n = 0; cur && n < 255u; ++n) {
		uint32_t cp;
		e = entry_address(s, cur, &cp);
		if (e)
			return e;
		if (cur == object) {
			if (prev) {
				uint32_t vp;
				e = entry_address(s, prev, &vp);
				if (e)
					return e;
				e = zvm_mem_write(s, vp + 5ul, sibling);
			} else
				e = zvm_mem_write(s, pp + 6ul, sibling);
			if (e)
				return e;
			if ((e = zvm_mem_write(s, p + 4ul, 0)) != ZVM_OK)
				return e;
			return zvm_mem_write(s, p + 5ul, 0);
		}
		prev = cur;
		e = zvm_mem_read(s, cp + 5ul, &cur);
		if (e)
			return e;
	}
	return cur ? ZVM_EINVAL : ZVM_EINVAL;
}

/* Reject a cycle before unlinking, then prepend to the destination's children. */
zvm_error_t zvm_object_insert(zvm_state_t *s, uint8_t object, uint8_t dest)
{
	uint32_t op, dp;
	uint8_t child, parent, sibling, walk, n;
	zvm_error_t e;
	if (!object)
		return ZVM_OK;
	if (!dest || object == dest)
		return ZVM_EINVAL;
	e = entry_address(s, object, &op);
	if (e)
		return e;
	e = entry_address(s, dest, &dp);
	if (e)
		return e;
	walk = dest;
	for (n = 0; walk && n < 255u; ++n) {
		if (walk == object)
			return ZVM_EINVAL;
		e = zvm_object_links_internal(s, walk, &parent, &sibling, &child);
		if (e)
			return e;
		walk = parent;
	}
	if (walk)
		return ZVM_EINVAL;
	e = zvm_object_remove(s, object);
	if (e)
		return e;
	e = zvm_mem_read(s, dp + 6ul, &child);
	if (e)
		return e;
	e = zvm_mem_write(s, op + 4ul, dest);
	if (e)
		return e;
	e = zvm_mem_write(s, op + 5ul, child);
	if (e)
		return e;
	e = zvm_mem_write(s, dp + 6ul, object);
	if (e)
		return e;
	return ZVM_OK;
}

zvm_error_t zvm_object_links(const zvm_t *vm, uint8_t obj, uint8_t *p, uint8_t *s, uint8_t *c)
{
	return vm ? zvm_object_links_internal(ZVM_CSTATE(vm), obj, p, s, c) : ZVM_EINVAL;
}

zvm_error_t zvm_object_attribute(const zvm_t *vm, uint8_t obj, uint8_t attr, uint8_t *set)
{
	return vm ? zvm_object_attribute_internal(ZVM_CSTATE(vm), obj, attr, set, 0) : ZVM_EINVAL;
}

zvm_error_t zvm_object_property(const zvm_t *vm, uint8_t obj, uint8_t prop, uint8_t *out,
				uint8_t cap, uint8_t *len)
{
	uint32_t a;
	uint8_t n, k;
	zvm_error_t e;
	if (!vm || !out || !len)
		return ZVM_EINVAL;
	e = zvm_object_prop_addr(ZVM_CSTATE(vm), obj, prop, &a, &n);
	if (e)
		return e;
	if (!a) {
		uint16_t d;
		e = zvm_mem_word(
		    ZVM_CSTATE(vm),
		    (uint32_t)ZVM_CSTATE(vm)->objects_addr + ((uint32_t)(prop - 1u) << 1), &d);
		if (e)
			return e;
		if (cap < 2)
			return ZVM_EBOUNDS;
		out[0] = (uint8_t)(d >> 8);
		out[1] = (uint8_t)d;
		*len = 2;
		return ZVM_OK;
	}
	if (cap < n)
		return ZVM_EBOUNDS;
	for (k = 0; k < n; ++k) {
		e = zvm_mem_read(ZVM_CSTATE(vm), a + k, &out[k]);
		if (e)
			return e;
	}
	*len = n;
	return ZVM_OK;
}
