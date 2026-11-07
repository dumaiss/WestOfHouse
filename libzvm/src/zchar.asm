; Packed Z-character extraction, independently written for libzvm.
; uint8_t zvm_unpack(uint16_t word, uint16_t slot), sdcccall(1):
; HL = word, DE = slot (0, 1 or 2 only), A = the five-bit character.
; IX and SP preserved; other registers and flags are scratch.
; The caller retains word fetch, end-bit, error and slot advancement logic.

    .module zchar
    .globl _zvm_unpack
    .area _CODE

_zvm_unpack::
    ld a,e
    or a
    jr z,zchar_first
    dec a
    jr nz,zchar_last
    add hl,hl
    add hl,hl
    add hl,hl
    ld a,h
    and #31
    ret
zchar_first:
    ld a,h
    rrca
    rrca
    and #31
    ret
zchar_last:
    ld a,l
    and #31
    ret
