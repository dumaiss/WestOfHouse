#ifndef LIBZVM_PROFILE_H
#define LIBZVM_PROFILE_H
#include "zvm.h"
/* Optional diagnostic interface, present only with -DZVM_PROFILE=1.
 * Counters wrap modulo 2^32. In-range byte operations count attempts;
 * word reads count entries and also count their constituent byte reads. No production API or
 * allocation is required. Build the application and library with the same setting. */
#if defined(ZVM_PROFILE) && ZVM_PROFILE
typedef struct {
	uint32_t instructions, op_0op, op_1op, op_2op, op_var;
	uint32_t calls, returns, branches, branches_taken, yields;
	uint32_t decoder_fetches, byte_reads, word_reads, byte_writes, word_writes;
	uint32_t text_chars, abbreviations, dictionary_lookups, dictionary_comparisons;
	uint32_t output_calls, output_bytes;
} zvm_profile_t;

/* Borrowed VM-owned counters; release clears them. */
const zvm_profile_t *zvm_profile_get(const zvm_t *vm);
/* Clear counters only, preserving execution state. */
void zvm_profile_reset(zvm_t *vm);
/* Implementation instrumentation below uses the private zvm_state_t.
 * Applications consume the getter/reset API instead of ZVM_COUNT. */
void zvm_profile_increment(uint32_t *counter);
#define ZVM_COUNT(s, field) zvm_profile_increment(&((zvm_state_t *)(void *)(s))->profile.field)
#else
#define ZVM_COUNT(s, field) ((void)0)
#endif
#endif
