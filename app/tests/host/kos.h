#ifndef WOH_HOST_KOS_H
#define WOH_HOST_KOS_H
#include <stdio.h>
#include <string.h>
#include "kos/types.h"
#include "kos/errno.h"
#include "kos/fs.h"
#include "kos/xmem.h"
int open(const char *, unsigned char);
int close(unsigned int);
int blob(unsigned int);
int xread(unsigned int, unsigned int, void *, unsigned int);
int xwrite(unsigned int, unsigned int, const void *, unsigned int);
int xalloc(unsigned int, unsigned int);
int xfree(unsigned int);
#endif
