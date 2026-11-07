#ifndef ZVM_STACK_PROBE_H
#define ZVM_STACK_PROBE_H
#if defined(ZVM_STACK_PROBE) && ZVM_STACK_PROBE
void stack_probe_mark(void);
unsigned int stack_probe_written(void);
unsigned char stack_probe_guard_ok(void);
#else
#define stack_probe_mark() ((void)0)
#endif
#endif
