/* Public lifecycle, V3 header validation and diagnostic accessors.
 * Resource ownership stays with the caller; see docs/embedding.md. */
#include "zvm_internal.h"

static void zero_context(zvm_t *vm)
{
	uint16_t n;
	uint8_t *p = (uint8_t *)vm;
	for (n = 0; n < sizeof(*vm); ++n)
		p[n] = 0;
}

static zvm_error_t validate_header(zvm_t *vm)
{
	zvm_state_t *s = ZVM_STATE(vm);
	uint8_t version;
	uint16_t w;
	uint32_t n, declared;
	uint32_t sum = 0;
	zvm_error_t e;
	e = zvm_mem_read(s, 0, &version);
	if (e)
		return s->error = e;
	if (version != ZVM_VERSION)
		return s->error = ZVM_EVERSION;
	e = zvm_mem_word(s, 0x06, &w);
	if (e)
		return s->error = e;
	s->pc = w;
	e = zvm_mem_word(s, 0x0a, &w);
	if (e)
		return s->error = e;
	s->objects_addr = w;
	e = zvm_mem_word(s, 0x0c, &w);
	if (e)
		return s->error = e;
	s->globals_addr = w;
	e = zvm_mem_word(s, 0x0e, &w);
	if (e)
		return s->error = e;
	s->static_base = w;
	e = zvm_mem_word(s, 0x08, &s->dictionary_addr);
	if (e)
		return s->error = e;
	e = zvm_mem_word(s, 0x18, &s->abbreviation_addr);
	if (e)
		return s->error = e;
	e = zvm_mem_word(s, 0x1a, &w);
	if (e)
		return s->error = e;
	if (!w)
		return s->error = ZVM_EHEADER;
	declared = (uint32_t)w << 1;
	if (s->static_base < 64u || s->static_base > s->story_size || s->pc >= s->story_size ||
	    declared > s->story_size || (uint32_t)s->globals_addr + 480ul > s->story_size ||
	    (uint32_t)s->objects_addr + 62ul > s->story_size)
		return s->error = ZVM_EHEADER;
	e = zvm_mem_word(s, 0x1c, &s->header_checksum);
	if (e)
		return s->error = e;
	if (s->flat_story) {
		for (n = 64ul; n < declared; ++n)
			sum += s->flat_story[n];
		s->story_checksum = (uint16_t)sum;
		s->checksum_valid = 1;
	}
	s->random_state = 0xace1u;
	s->frame_count = 1;
	s->frames[0].stack_base = 0;
	s->frames[0].local_count = 0;
	s->status = ZVM_READY;
	s->error = ZVM_OK;
	return ZVM_OK;
}

zvm_error_t zvm_init(zvm_t *vm, uint8_t *story, uint32_t size)
{
	if (!vm || !story)
		return ZVM_EINVAL;
	zero_context(vm);
	ZVM_STATE(vm)->flat_story = story;
	ZVM_STATE(vm)->story_size = size;
	return validate_header(vm);
}

zvm_error_t zvm_init_memory(zvm_t *vm, zvm_memory_read_fn read_fn, zvm_memory_write_fn write_fn,
			    void *user, uint32_t size)
{
	if (!vm || !read_fn || !write_fn)
		return ZVM_EINVAL;
	zero_context(vm);
	ZVM_STATE(vm)->read_byte = read_fn;
	ZVM_STATE(vm)->write_byte = write_fn;
	ZVM_STATE(vm)->memory_user = user;
	ZVM_STATE(vm)->story_size = size;
	return validate_header(vm);
}

/* Execution reset only: story bytes, callbacks and other retained state are
 * not restored. This is not an implementation of the restart opcode. */
zvm_error_t zvm_reset(zvm_t *vm)
{
	zvm_state_t *s;
	if (!vm)
		return ZVM_EINVAL;
	s = ZVM_STATE(vm);
	if (!s->flat_story && !s->read_byte)
		return ZVM_ESTATE;
	s->pc = 0;
	{
		uint16_t initial;
		zvm_error_t e = zvm_mem_word(s, 6, &initial);
		if (e)
			return e;
		s->pc = initial;
	}
	s->sp = 0;
	s->frame_count = 1;
	s->frames[0].stack_base = 0;
	s->frames[0].local_count = 0;
	s->status = ZVM_READY;
	s->error = ZVM_OK;
	return ZVM_OK;
}

void zvm_release(zvm_t *vm)
{
	if (vm)
		zero_context(vm);
}

zvm_error_t zvm_set_output_callback(zvm_t *vm, zvm_output_fn cb, void *user)
{
	if (!vm || !ZVM_STATE(vm)->story_size)
		return ZVM_EINVAL;
	ZVM_STATE(vm)->output = cb;
	ZVM_STATE(vm)->output_user = user;
	return ZVM_OK;
}

zvm_error_t zvm_set_status_callback(zvm_t *vm, zvm_status_fn cb, void *user)
{
	uint8_t flags;
	zvm_state_t *s;
	if (!vm || !ZVM_STATE(vm)->story_size)
		return ZVM_EINVAL;
	s = ZVM_STATE(vm);
	s->status_callback = cb;
	s->status_user = user;
	if (zvm_mem_read(s, 1, &flags) != ZVM_OK)
		return ZVM_EBOUNDS;
	if (cb)
		flags |= 1u;
	else
		flags &= (uint8_t)~1u;
	return zvm_mem_write(s, 1, flags);
}

zvm_error_t zvm_seed_random(zvm_t *vm, uint16_t seed)
{
	if (!vm || !ZVM_STATE(vm)->story_size)
		return ZVM_EINVAL;
	ZVM_STATE(vm)->random_state = seed ? seed : 0xace1u;
	return ZVM_OK;
}

zvm_error_t zvm_set_story_checksum(zvm_t *vm, uint16_t checksum)
{
	if (!vm || !ZVM_STATE(vm)->story_size)
		return ZVM_EINVAL;
	ZVM_STATE(vm)->story_checksum = checksum;
	ZVM_STATE(vm)->checksum_valid = 1;
	return ZVM_OK;
}

zvm_status_t zvm_status(const zvm_t *vm)
{
	return vm && ZVM_CSTATE(vm)->story_size ? ZVM_CSTATE(vm)->status : ZVM_FAULT;
}

zvm_error_t zvm_error(const zvm_t *vm)
{
	return vm && ZVM_CSTATE(vm)->story_size ? ZVM_CSTATE(vm)->error : ZVM_EINVAL;
}

uint32_t zvm_program_counter(const zvm_t *vm)
{
	return vm ? ZVM_CSTATE(vm)->pc : 0xfffffffful;
}

uint32_t zvm_fault_address(const zvm_t *vm)
{
	return vm ? ZVM_CSTATE(vm)->fault_address : 0xfffffffful;
}

uint8_t zvm_fault_opcode(const zvm_t *vm)
{
	return vm ? ZVM_CSTATE(vm)->pending_opcode : 0xffu;
}

uint32_t zvm_previous_address(const zvm_t *vm)
{
	return vm ? ZVM_CSTATE(vm)->previous_address : 0xfffffffful;
}

zvm_error_t zvm_zork_room(const zvm_t *vm, uint16_t *room)
{
	return zvm_global(vm, 0, room);
}

#if defined(ZVM_PROFILE) && ZVM_PROFILE
void zvm_profile_increment(uint32_t *counter)
{
	++*counter;
}

const zvm_profile_t *zvm_profile_get(const zvm_t *vm)
{
	return vm ? &ZVM_CSTATE(vm)->profile : 0;
}

void zvm_profile_reset(zvm_t *vm)
{
	uint16_t n;
	uint8_t *p;
	if (!vm)
		return;
	p = (uint8_t *)&ZVM_STATE(vm)->profile;
	for (n = 0; n < sizeof(zvm_profile_t); ++n)
		p[n] = 0;
}
#endif
