/* Preload the story into the mapped objects used by the VM callbacks. */
#include "mapped_story.h"
#include "kos.h"
#include "kos/xmem.h"

#define PRELOAD_CHUNK 256u

/* The caller lends 256 bytes of scratch before VM initialization. */

int mapped_story_init(mapped_story_t *m, kos_reader_t *reader, uint16_t dynamic_size,
		      uint32_t story_end, uint16_t *checksum, uint8_t *preload_scratch)
{
	uint32_t offset = 0;
	uint16_t sum = 0;
	if (!m || !reader || !checksum || !preload_scratch || !dynamic_size || story_end < 64ul ||
	    story_end > reader->size || dynamic_size > story_end)
		return -1;
	memset(m, 0, sizeof(*m));
	m->reader = reader;
	m->dynamic_size = dynamic_size;
	m->current = m->held_slot = 0xffu;
	while (offset < reader->size) {
		uint32_t remain = reader->size - offset;
		uint16_t length = (uint16_t)(remain > MAPPED_STORY_PAGE_SIZE ?
					    MAPPED_STORY_PAGE_SIZE : remain);
		uint8_t slot;
		int handle, token;
		if (m->count >= MAPPED_STORY_OBJECTS)
			goto fail;
		handle = xalloc(length, KOS_XMEM_FLAG_MAPPABLE);
		if (handle < 1)
			goto fail;
		slot = m->count++;
		m->handles[slot] = (unsigned int)handle;
		m->lengths[slot] = length;
		token = xmap((unsigned int)handle, &m->addresses[slot]);
		if (token < 0)
			goto fail;
		m->tokens[slot] = (uint8_t)token;
		for (uint16_t done = 0; done < length;) {
			uint16_t n = (uint16_t)((uint16_t)(length - done) > PRELOAD_CHUNK ?
					       PRELOAD_CHUNK : (uint16_t)(length - done));
			if (kos_reader_read_full(reader, offset + done,
						  preload_scratch, n) != (int)n ||
			    xwrite((unsigned int)handle, done, preload_scratch, n) != (int)n)
				goto fail;
			if (offset + done < story_end) {
				uint16_t start = offset + done < 64ul ?
					(uint16_t)(64ul - (offset + done)) : 0u;
				uint16_t end = n;
				uint16_t i;
				if (offset + done + end > story_end)
					end = (uint16_t)(story_end - (offset + done));
				for (i = start; i < end; ++i)
					sum = (uint16_t)(sum + preload_scratch[i]);
			}
			done = (uint16_t)(done + n);
		}
		offset += length;
	}
	m->bytes = offset;
	*checksum = sum;
	return 0;
fail:
	mapped_story_close(m);
	return -1;
}
