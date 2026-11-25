/* The complete story lives in 8 KiB mappable XMEM objects. Callbacks
 * access those objects directly. The resident execution scope retains the
 * mapping across byte accesses and restores the caller's code mapping on
 * yield, input, quit or fault. Calls outside a scope restore per access. */
#include "mapped_story.h"
#include "kos.h"
#include "kos/xmem.h"

static unsigned char *story_byte(mapped_story_t *m, uint32_t address,
                                 uint8_t *token)
{
	uint8_t slot = (uint8_t)(address / MAPPED_STORY_PAGE_SIZE);
	uint16_t offset = (uint16_t)address & (MAPPED_STORY_PAGE_SIZE - 1u);
	if (slot >= m->count || offset >= m->lengths[slot])
		return 0;
	*token = m->tokens[slot];
	if (!m->valid || m->current != slot) {
		m->current = slot;
		m->valid = 1;
		++m->page_loads;
	}
	return (unsigned char *)(m->addresses[slot] + offset);
}

static uint8_t open_byte(mapped_story_t *m, uint8_t token)
{
	uint8_t previous, slot = m->current;
	if (m->active && m->held && m->held_slot == slot) return m->previous;
	previous = aperture_open(token);
	++m->remaps;
	if (m->active) {
		if (!m->held) m->previous = previous;
		m->held = 1;
		m->held_slot = slot;
	}
	return previous;
}

zvm_error_t mapped_story_read(void *user, uint32_t address, uint8_t *value)
{
	mapped_story_t *m = (mapped_story_t *)user;
	unsigned char *at;
	uint8_t token, previous, byte;
	if (!m || !value || !m->reader || address >= m->reader->size)
		return ZVM_EBOUNDS;
	at = story_byte(m, address, &token);
	if (!at)
		return ZVM_EBOUNDS;
	previous = open_byte(m, token);
	byte = *at;
	if (!m->active) aperture_close(previous);
	/* The destination may belong to the caller's mapped code segment. */
	*value = byte;
	return ZVM_OK;
}

zvm_error_t mapped_story_write(void *user, uint32_t address, uint8_t value)
{
	mapped_story_t *m = (mapped_story_t *)user;
	unsigned char *at;
	uint8_t token, previous;
	if (!m || !m->reader || address >= m->dynamic_size ||
	    address >= m->reader->size)
		return ZVM_EREADONLY;
	at = story_byte(m, address, &token);
	if (!at)
		return ZVM_EBOUNDS;
	previous = open_byte(m, token);
	*at = value;
	if (!m->active) aperture_close(previous);
	++m->page_writes;
	return ZVM_OK;
}

/* Lend a native 512-byte view into the current 8 KiB mapping. The VM's
 * resident assembly hit paths access it directly without byte callbacks.
 * held_slot identifies the mapping: returning to the same region makes
 * its old views valid again; a different region rejects them. */
uint8_t *mapped_story_page(void *user, uint32_t address, uint8_t write)
{
	mapped_story_t *m = user;
	uint8_t token;
	unsigned char *at;
	if (!m->active || (address | 511ul) >= m->reader->size ||
	    (write && address >= m->dynamic_size)) return 0;
	at = story_byte(m, address, &token);
	if (!at) return 0;
	(void)open_byte(m, token);
	return at - ((uint16_t)address & 511u);
}

/* This entire call closure is resident. No caller code in the aperture
 * executes until the original mapping has been restored. */
zvm_status_t mapped_story_run(mapped_story_t *m, zvm_t *vm, uint16_t budget)
{
	zvm_status_t status;
	if (m->active) return ZVM_FAULT;
	m->active = 1;
	m->held = 0;
	m->held_slot = 0xffu;
	status = zvm_run(vm, budget);
	m->held_slot = 0xffu;
	if (m->held) aperture_close(m->previous);
	m->held = m->active = 0;
	return status;
}

void mapped_story_close(mapped_story_t *m)
{
	uint8_t i;
	if (!m)
		return;
	for (i = 0; i < m->count; ++i)
		(void)xfree(m->handles[i]);
	m->count = 0;
	m->valid = 0;
}
