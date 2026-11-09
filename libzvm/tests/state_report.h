#ifndef WOH_STATE_REPORT_H
#define WOH_STATE_REPORT_H

#include "zvm.h"

#define WOH_BINDINGS_SIZE 70u
#define WOH_STORY_HEADER_SIZE 64u

/* Test application's build-time bindings, never a VM or room-dictionary API. */
typedef struct {
	uint8_t here_index, state_index, enabled;
} woh_state_report_t;

/* Compare against the immutable source header before any VM header writes.
 * No binding is accepted for an absent, truncated or mismatched sidecar. */
int woh_state_report_init(woh_state_report_t *report, const uint8_t *metadata,
			 uint16_t size, const uint8_t *source_header);
zvm_error_t woh_state_report_read(const zvm_t *vm, const woh_state_report_t *report,
				 uint16_t *here, uint16_t *state);

#endif
