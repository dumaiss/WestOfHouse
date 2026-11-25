#ifndef WOH_MAPPED_STORY_H
#define WOH_MAPPED_STORY_H

#include "kos/reader.h"
#include "zvm.h"

#define MAPPED_STORY_PAGE_SIZE 8192u
#define MAPPED_STORY_OBJECTS 16u

typedef struct {
	kos_reader_t *reader;
	unsigned int handles[MAPPED_STORY_OBJECTS], addresses[MAPPED_STORY_OBJECTS];
	uint16_t lengths[MAPPED_STORY_OBJECTS];
	uint8_t tokens[MAPPED_STORY_OBJECTS];
	uint8_t count, current, valid;
	uint8_t active, held, held_slot, previous;
	uint16_t dynamic_size;
	uint32_t bytes, remaps, page_loads, page_writes;
} mapped_story_t;

int mapped_story_init(mapped_story_t *mapped, kos_reader_t *reader, uint16_t dynamic_size,
		      uint32_t story_end, uint16_t *checksum, uint8_t *scratch);
zvm_error_t mapped_story_read(void *user, uint32_t address, uint8_t *value);
zvm_error_t mapped_story_write(void *user, uint32_t address, uint8_t value);
uint8_t *mapped_story_page(void *user, uint32_t address, uint8_t write);
zvm_status_t mapped_story_run(mapped_story_t *mapped, zvm_t *vm, uint16_t budget);
void mapped_story_close(mapped_story_t *mapped);

#endif
