; Streaming instruction fetch, sdcccall(1): HL=context, DE=output.
; The memory boundary checks/fetches one byte. Advance the four-byte logical
; PC in place only on success; the common case increments its low byte.
; The page-backed memory implementation owns mapping and bounds checks.
; No global interpreter scratch or native pointer survives the memory call.
	.module compact_fetch
	.globl _zvm_compact_next
	.globl _zvm_mem_fetch
	.area _CODE
_zvm_compact_next::
	push iy
	push hl
	pop iy
	ld bc,#33
	add iy,bc
	push iy                 ; PC pointer survives the callback
	push de                 ; third argument: output
	ld e,2(iy)
	ld d,3(iy)
	push de                 ; high half of logical address
	ld e,0(iy)
	ld d,1(iy)
	push de                 ; low half; HL still names context
	call _zvm_mem_fetch      ; callee removes address and output arguments
	pop hl
	or a
	jr nz,00101$
	inc (hl)
	jr nz,00101$
	inc hl
	inc (hl)
	jr nz,00101$
	inc hl
	inc (hl)
	jr nz,00101$
	inc hl
	inc (hl)
00101$:
	pop iy
	ret
