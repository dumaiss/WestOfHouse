#ifndef LIBZVM_ZVM_H
#define LIBZVM_ZVM_H

#include "kos/types.h"

#define ZVM_VERSION 3u
#define ZVM_STACK_CAPACITY 128u
#define ZVM_FRAME_CAPACITY 16u

/* Cooperative execution states. NEED_SAVE/NEED_RESTORE are reserved; current
 * V3 save/restore instructions report failure without suspending the VM. */
typedef enum {
	ZVM_READY = 0,
	ZVM_NEED_INPUT,
	ZVM_NEED_SAVE,
	ZVM_NEED_RESTORE,
	ZVM_YIELDED,
	ZVM_FINISHED,
	ZVM_FAULT
} zvm_status_t;

typedef enum {
	ZVM_OK = 0,
	ZVM_EINVAL,
	ZVM_EVERSION,
	ZVM_EHEADER,
	ZVM_EBOUNDS,
	ZVM_EREADONLY,
	ZVM_ESTACK,
	ZVM_ESTATE,
	ZVM_EUNSUPPORTED,
	ZVM_EDIVZERO,
	ZVM_EFRAME
} zvm_error_t;

/* Fixed caller-owned storage keeps allocation explicit while hiding internals.
 * The pointer member supplies suitable alignment on both host and Z80 ABI. */
typedef union {
	void *_alignment;
	uint8_t _storage[1536];
} zvm_t;

/* All callbacks are synchronous. Keep user storage alive until release or
 * replacement; do not recursively drive the same VM from a callback.
 * Memory callbacks copy one byte; they never lend a mapped-memory pointer. */
typedef zvm_error_t (*zvm_memory_read_fn)(void *user, uint32_t address, uint8_t *value);
typedef zvm_error_t (*zvm_memory_write_fn)(void *user, uint32_t address, uint8_t value);
typedef zvm_error_t (*zvm_output_fn)(void *user, uint8_t channel, uint8_t byte);
typedef zvm_error_t (*zvm_status_fn)(void *user, uint16_t location, uint16_t value1,
				     uint16_t value2, uint8_t time_mode);

enum { ZVM_OUTPUT_NARRATIVE = 0, ZVM_OUTPUT_STATUS = 1 };

/* Borrow a complete writable story buffer, validate its V3 header and cache
 * the original checksum. Keep the buffer alive until release. No allocation. */
zvm_error_t zvm_init(zvm_t *vm, uint8_t *story, uint32_t story_size);
/* Alternative for banked/page-backed stories. Both callbacks are required.
 * The caller owns backend/user storage. Reads cover [0, story_size); the VM
 * rejects static-memory writes before invoking write_byte. This initializer
 * does not compute the original checksum; use zvm_set_story_checksum(). */
zvm_error_t zvm_init_memory(zvm_t *vm, zvm_memory_read_fn read_byte, zvm_memory_write_fn write_byte,
			    void *user, uint32_t story_size);
/* Reset PC, evaluation stack, root frame, status and error only. Story bytes
 * and other state are retained: this is not a complete story restart. */
zvm_error_t zvm_reset(zvm_t *vm);
/* Clear the context; close/free no caller-owned resources. */
void zvm_release(zvm_t *vm);
/* Output callback is synchronous; bytes are ASCII, CR becomes LF, and
 * unsupported extended ZSCII characters are represented by '?'. */
zvm_error_t zvm_set_output_callback(zvm_t *vm, zvm_output_fn callback, void *user);
/* Report location plus score/moves (time_mode == 0) or hours/minutes. Also
 * updates header byte 1; a backend failure may occur after storing callback. */
zvm_error_t zvm_set_status_callback(zvm_t *vm, zvm_status_fn callback, void *user);
/* Seed zero selects the fixed fallback seed 0xace1. */
zvm_error_t zvm_seed_random(zvm_t *vm, uint16_t seed);
/* Supply the modulo-65536 sum of original bytes [64, declared story end).
 * Used by verify; do not checksum already modified dynamic memory. */
zvm_error_t zvm_set_story_checksum(zvm_t *vm, uint16_t checksum);

/* Execute at most budget instructions. Budget zero yields immediately.
 * NEED_INPUT, FINISHED and FAULT do not advance on subsequent run calls. */
zvm_status_t zvm_run(zvm_t *vm, uint16_t budget);
/* Only valid for a pending NEED_INPUT. Truncate to V3 buffer capacity minus
 * one, lowercase ASCII, replace unsupported input with ?, then tokenize.
 * Failed writes/tokenization may leave partial story-memory updates. */
zvm_error_t zvm_submit_input(zvm_t *vm, const char *text);
zvm_status_t zvm_status(const zvm_t *vm);
zvm_error_t zvm_error(const zvm_t *vm);
/* Logical byte addresses, not native pointers. fault_opcode is normalized
 * after successful decode, or the raw byte when a failed decode can read it. */
uint32_t zvm_program_counter(const zvm_t *vm);
uint32_t zvm_fault_address(const zvm_t *vm);
uint8_t zvm_fault_opcode(const zvm_t *vm);
uint32_t zvm_previous_address(const zvm_t *vm);

/* Bounded logical-memory inspection; words are big-endian. Writes are
 * restricted to dynamic memory in both flat and callback-backed modes. */
zvm_error_t zvm_read_byte(const zvm_t *vm, uint32_t address, uint8_t *out);
zvm_error_t zvm_read_word(const zvm_t *vm, uint32_t address, uint16_t *out);
zvm_error_t zvm_write_byte(zvm_t *vm, uint32_t address, uint8_t value);
/* Operate on the current frame's bounded Z-machine evaluation stack. */
zvm_error_t zvm_stack_push(zvm_t *vm, uint16_t value);
zvm_error_t zvm_stack_pop(zvm_t *vm, uint16_t *value);
/* index is zero-based (0..239), corresponding to variable numbers 16..255. */
zvm_error_t zvm_global(const zvm_t *vm, uint8_t index, uint16_t *value);
zvm_error_t zvm_object_links(const zvm_t *vm, uint8_t object, uint8_t *parent, uint8_t *sibling,
			     uint8_t *child);
zvm_error_t zvm_object_attribute(const zvm_t *vm, uint8_t object, uint8_t attribute, uint8_t *set);
/* Copies property bytes to host storage; never returns a story-memory pointer. */
zvm_error_t zvm_object_property(const zvm_t *vm, uint8_t object, uint8_t property, uint8_t *out,
				uint8_t capacity, uint8_t *length);
/* Game-specific convention: Zork I global 0 (variable 16) stores the room. */
zvm_error_t zvm_zork_room(const zvm_t *vm, uint16_t *room);

#endif
