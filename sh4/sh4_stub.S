/* gpSP SH4 dynarec stub
 *
 * Assembly entry points, global state, and memory handlers for SH4 (Dreamcast).
 */

#include "../gpsp_config.h"

.align 4

#define defsymbl(symbol) \
.global symbol ;         \
.global _##symbol ;      \
symbol:                  \
_##symbol:

/* Register offsets in reg[] array */
.equ REG_SP,            (13 * 4)
.equ REG_LR,            (14 * 4)
.equ REG_PC,            (15 * 4)
.equ REG_CPSR,          (16 * 4)
.equ CPU_MODE,          (17 * 4)
.equ CPU_HALT_STATE,    (18 * 4)
.equ REG_BUS_VALUE,     (19 * 4)
.equ REG_N_FLAG,        (20 * 4)
.equ REG_Z_FLAG,        (21 * 4)
.equ REG_C_FLAG,        (22 * 4)
.equ REG_V_FLAG,        (23 * 4)
.equ REG_SLEEP_CYCLES,  (24 * 4)
.equ OAM_UPDATED,       (25 * 4)
.equ REG_SAVE,          (26 * 4)

/* CPU alert types */
.equ CPU_ALERT_HALT,    (1 << 0)
.equ CPU_ALERT_SMC,     (1 << 1)
.equ CPU_ALERT_IRQ,     (1 << 2)

/* SH4 register assignments for JIT:
 *   r8  = reg_base (pointer to reg[] array)
 *   r9  = reg_cycles (cycle counter)
 *   r0  = scratch / return value
 *   r1-r3 = scratch
 *   r4-r7 = function arguments
 *   r10-r14 = callee-saved spare
 *   r15 = SP
 */

.text

/* ---- execute_arm_translate_internal(r4=cycles, r5=regptr) ---- */
defsymbl(execute_arm_translate_internal)
    /* Save callee-saved registers */
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    sts.l   pr, @-r15

    mov     r4, r9          /* reg_cycles = cycles */
    mov     r5, r8          /* reg_base = regptr */

    /* Extract flags from CPSR into separate slots */
    /* For now, just zero them - proper extraction in Phase 3 */
    mov     #0, r1
    mov     #REG_N_FLAG, r0
    mov.l   r1, @(r0, r8)
    mov     #REG_Z_FLAG, r0
    mov.l   r1, @(r0, r8)
    mov     #REG_C_FLAG, r0
    mov.l   r1, @(r0, r8)
    mov     #REG_V_FLAG, r0
    mov.l   r1, @(r0, r8)

    /* Check if CPU is halted */
    mov     #CPU_HALT_STATE, r0
    mov.l   @(r0, r8), r1
    tst     r1, r1
    bf      cpu_sleep_loop

    /* Look up and jump to translated code for current PC */
    bra     lookup_pc
    nop

lookup_pc:
    mov.l   @(REG_PC, r8), r4     /* r4 = PC (save for later) */
    /* Check CPSR thumb bit to decide ARM vs Thumb lookup */
    mov     #REG_CPSR, r0
    mov.l   @(r0, r8), r1
    mov     #0x20, r2
    tst     r2, r1
    bt      lookup_arm
    /* Thumb mode: r4 already has PC */
    mov.l   lookup_thumb_addr, r0
    jsr     @r0
    nop
    jmp     @r0
    nop
lookup_arm:
    /* r4 already has PC */
    mov.l   lookup_arm_addr, r0
    jsr     @r0
    nop
    jmp     @r0
    nop

    .align 2
lookup_arm_addr:
    .long   _block_lookup_address_arm
lookup_thumb_addr:
    .long   _block_lookup_address_thumb

return_to_main:
    /* Restore callee-saved registers and return */
    lds.l   @r15+, pr
    mov.l   @r15+, r14
    mov.l   @r15+, r13
    mov.l   @r15+, r12
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    mov.l   @r15+, r8
    rts
    nop

cpu_sleep_loop:
    /* CPU is halted, call update_gba until it wakes */
    mov     r9, r4          /* arg0 = cycles */
    mov.l   update_gba_addr, r0
    jsr     @r0
    nop
    /* Check if frame complete (bit 31) */
    cmp/pz  r0
    bf      return_to_main
    /* Update cycles */
    mov     r0, r9
    /* Check CPU_HALT_STATE */
    mov     #CPU_HALT_STATE, r0
    mov.l   @(r0, r8), r2
    tst     r2, r2
    bf      cpu_sleep_loop
    bra     lookup_pc
    nop

    .align 2
update_gba_addr:
    .long   _update_gba

/* ---- sh4_update_gba ---- */
/* Called from JIT code: r0 = current PC */
defsymbl(sh4_update_gba)
    /* Save PC */
    mov.l   r0, @(REG_PC, r8)

    /* Collapse flags back into CPSR */
    /* TODO: proper flag collapse - for now just call with cycles */
    mov     r9, r4          /* arg0 = remaining cycles */
    mov.l   update_gba_addr2, r0
    jsr     @r0
    nop

    /* Check return: bit 31 = frame complete */
    cmp/pz  r0
    bf      return_to_main

    /* Extract new cycle count (bits 0-14) */
    mov.w   cycles_mask, r1
    mov     r0, r9
    and     r1, r9

    /* Check if PC changed (bit 30) */
    mov.l   pc_change_mask, r1
    tst     r1, r0
    bt      sh4_update_return
    /* PC changed, re-lookup */
    bra     lookup_pc
    nop

sh4_update_return:
    rts
    nop

    .align 2
update_gba_addr2:
    .long   _update_gba
cycles_mask:
    .word   0x7FFF
    .align 2
pc_change_mask:
    .long   0x40000000

/* ---- Indirect branch handlers ---- */
defsymbl(sh4_indirect_branch_arm)
    mov     r0, r4
    mov.l   blk_arm_addr, r0
    jsr     @r0
    nop
    jmp     @r0
    nop

defsymbl(sh4_indirect_branch_thumb)
    mov     r0, r4
    mov.l   blk_thumb_addr, r0
    jsr     @r0
    nop
    jmp     @r0
    nop

defsymbl(sh4_indirect_branch_dual)
    mov     r0, r4
    mov.l   blk_dual_addr, r0
    jsr     @r0
    nop
    jmp     @r0
    nop

    .align 2
blk_arm_addr:
    .long   _block_lookup_address_arm
blk_thumb_addr:
    .long   _block_lookup_address_thumb
blk_dual_addr:
    .long   _block_lookup_address_dual

/* ---- BSS: Global state ---- */
.bss
.align 6

defsymbl(reg)
    .space 0x100

defsymbl(palette_ram)
    .space 0x400
defsymbl(palette_ram_converted)
    .space 0x400
defsymbl(oam_ram)
    .space 0x400

defsymbl(iwram)
    .space 0x10000
defsymbl(vram)
    .space 0x18000
defsymbl(ewram)
    .space 0x80000

defsymbl(io_registers)
    .space 0x400

defsymbl(spsr)
    .space 24
    .space 8

defsymbl(reg_mode)
    .space 196
    .space 28

defsymbl(memory_map_read)
    .space (8 * 1024 * 4)

defsymbl(rom_translation_cache)
    .space ROM_TRANSLATION_CACHE_SIZE
defsymbl(ram_translation_cache)
    .space RAM_TRANSLATION_CACHE_SIZE

#if defined(__linux__) && defined(__ELF__)
    .section .note.GNU-stack,"",%progbits
#endif
