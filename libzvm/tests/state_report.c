/* Numeric reporting adapter for ZVMTest. Symbolic indices are supplied by the
 * story build, and all inspection goes through the existing public VM API. */
#include "state_report.h"

int woh_state_report_init(woh_state_report_t *report, const uint8_t *metadata,
			 uint16_t size, const uint8_t *source_header)
{
	uint8_t i;
	if (!report)
		return 0;
	report->enabled = 0;
	if (!metadata || !source_header || size != WOH_BINDINGS_SIZE)
		return 0;
	if (metadata[0] != 'W' || metadata[1] != 'O' || metadata[2] != 'H' ||
	    metadata[3] != '1' || metadata[4] >= 240u || metadata[5] >= 240u ||
	    metadata[4] == metadata[5] || source_header[0] != 3u)
		return 0;
	for (i = 0; i < WOH_STORY_HEADER_SIZE; ++i)
		if (metadata[6u + i] != source_header[i])
			return 0;
	report->here_index = metadata[4];
	report->state_index = metadata[5];
	report->enabled = 1;
	return 1;
}

zvm_error_t woh_state_report_read(const zvm_t *vm, const woh_state_report_t *report,
				 uint16_t *here, uint16_t *state)
{
	zvm_error_t error;
	if (!report || !report->enabled || !here || !state)
		return ZVM_EINVAL;
	error = zvm_global(vm, report->here_index, here);
	if (error != ZVM_OK)
		return error;
	return zvm_global(vm, report->state_index, state);
}
