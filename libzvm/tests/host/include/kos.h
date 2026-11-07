#ifndef ZVM_HOST_KOS_H
#define ZVM_HOST_KOS_H
/* Cache-only host tests replace these two services with bounded arrays.
 * Use host libc declarations rather than the target's 16-bit size_t ABI. */
#include <string.h>
#include "kos/errno.h"
int xread(unsigned int handle, unsigned int offset, void *buffer, unsigned int length);
int xwrite(unsigned int handle, unsigned int offset, const void *buffer, unsigned int length);
#endif
