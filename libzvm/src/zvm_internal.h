#ifndef LIBZVM_INTERNAL_H
#define LIBZVM_INTERNAL_H

#include "zvm.h"
#include "zvm_profile.h"

/* Private state only. Public users allocate zvm_t and use the API.
 * stack_base separates a frame's evaluation stack from its caller's values. */
typedef struct {
	uint32_t return_pc;
	uint16_t stack_base;
	uint16_t locals[15];
	uint8_t local_count, store_var, has_store, argument_count;
} zvm_frame_t;

/* Backend, execution state and host callbacks. Layout fits the public storage. */
typedef struct {
	/* Exactly one backend: flat_story, or read_byte/write_byte + memory_user. */
	uint8_t *flat_story;
	zvm_memory_read_fn read_byte;
	zvm_memory_write_fn write_byte;
	void *memory_user;
	uint32_t story_size, pc;
	uint16_t static_base, globals_addr, objects_addr;
	/* sp counts used words; frame_count includes the non-returnable root. */
	uint16_t stack[ZVM_STACK_CAPACITY], sp;
	zvm_frame_t frames[ZVM_FRAME_CAPACITY];
	uint8_t frame_count;
	zvm_status_t status;
	zvm_error_t error;
	uint8_t pending_opcode;
	zvm_output_fn output;
	void *output_user;
	zvm_status_fn status_callback;
	void *status_user;
	uint16_t dictionary_addr, abbreviation_addr;
	/* Logical buffer addresses captured by sread until input is submitted. */
	uint16_t pending_text, pending_parse;
	uint8_t input_pending, checksum_valid;
	uint16_t story_checksum, header_checksum, random_state;
	/* fault_address/last_address track the current instruction; previous_address
	 * tracks the preceding instruction start during execution. */
	uint32_t fault_address, previous_address, last_address;
#if defined(ZVM_PROFILE) && ZVM_PROFILE
	zvm_profile_t profile;
#endif
} zvm_state_t;

typedef char zvm_context_size_check[(sizeof(zvm_state_t) <= sizeof(zvm_t)) ? 1 : -1];

#define ZVM_STATE(v) ((zvm_state_t *)(void *)(v))
#define ZVM_CSTATE(v) ((const zvm_state_t *)(const void *)(v))

/* Normalized opcodes: 2OP 0..31, 1OP 128..143, 0OP 176..191, VAR 224..255.
 * Operand types: 0 large constant, 1 small constant, 2 variable number.
 * operands[] still contains encoded values; execute.c performs evaluation. */
typedef enum { ZI_LONG = 0, ZI_SHORT = 1, ZI_VAR = 2 } zvm_form_t;

typedef struct {
	uint32_t address, next_pc, branch_target;
	uint16_t operands[4];
	uint8_t operand_types[4], opcode, form, operand_count;
	uint8_t has_store, store_var, has_branch, branch_on_true;
	uint8_t branch_return;
	int16_t branch_offset;
} zvm_instruction_t;

/* Logical memory: no function returns a pointer into a mapped story page. */
zvm_error_t zvm_mem_read(const zvm_state_t *s, uint32_t address, uint8_t *out);
zvm_error_t zvm_mem_write(zvm_state_t *s, uint32_t address, uint8_t value);
zvm_error_t zvm_mem_word(const zvm_state_t *s, uint32_t address, uint16_t *out);
/* Decode only; no variable evaluation or execution side effects. */
zvm_error_t zvm_decode(const zvm_state_t *s, uint32_t address, zvm_instruction_t *instruction);
/* indirect changes variable-zero behavior only; see variables.c. */
zvm_error_t zvm_variable_read(zvm_state_t *s, uint8_t variable, uint16_t *value, uint8_t indirect);
zvm_error_t zvm_variable_write(zvm_state_t *s, uint8_t variable, uint16_t value, uint8_t indirect);
/* In-place V3 object tables and properties. */
zvm_error_t zvm_object_links_internal(const zvm_state_t *s, uint8_t object, uint8_t *parent,
				      uint8_t *sibling, uint8_t *child);
zvm_error_t zvm_object_attribute_internal(const zvm_state_t *s, uint8_t object, uint8_t attribute,
					  uint8_t *set, uint8_t change);
zvm_error_t zvm_object_prop_addr(const zvm_state_t *s, uint8_t object, uint8_t property,
				 uint32_t *address, uint8_t *length);
zvm_error_t zvm_object_get_prop(const zvm_state_t *s, uint8_t object, uint8_t property,
				uint16_t *value);
zvm_error_t zvm_object_next_prop(const zvm_state_t *s, uint8_t object, uint8_t property,
				 uint8_t *next);
zvm_error_t zvm_object_prop_length(const zvm_state_t *s, uint32_t address, uint8_t *length);
zvm_error_t zvm_object_name_addr(const zvm_state_t *s, uint8_t object, uint32_t *address);
zvm_error_t zvm_object_remove(zvm_state_t *s, uint8_t object);
zvm_error_t zvm_object_insert(zvm_state_t *s, uint8_t object, uint8_t destination);
/* Call frames and opcode dispatch. */
zvm_error_t zvm_call_routine(zvm_state_t *s, uint16_t packed, const uint16_t *arguments,
			     uint8_t argument_count, uint8_t store_var, uint8_t has_store,
			     uint32_t return_pc);
zvm_error_t zvm_return_value(zvm_state_t *s, uint16_t value);
zvm_error_t zvm_execute_instruction(zvm_state_t *s, const zvm_instruction_t *i,
				    const uint16_t *args);
/* Text, dictionary and host status services. */
zvm_error_t zvm_text_print(zvm_state_t *s, uint32_t address, uint8_t channel,
			   uint32_t *next_address);
zvm_error_t zvm_output_byte(zvm_state_t *s, uint8_t channel, uint8_t byte);
zvm_error_t zvm_dictionary_lookup(zvm_state_t *s, const uint8_t *word, uint8_t length,
				  uint16_t *address);
zvm_error_t zvm_tokenize(zvm_state_t *s, uint16_t text, uint16_t parse);
zvm_error_t zvm_update_status(zvm_state_t *s);

#endif
