.module turnprof_entry
.globl _start, _probe_main, _host_trap, _turn_begin, _turn_end, _probe_fail
.area _CODE
_start::
 ld sp, #0xE000
 call _probe_main
 halt
_host_trap::
 ret
_turn_begin::
 ret
_turn_end::
 ret
_probe_fail::
 halt
