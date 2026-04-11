/* gpSP SH4 dynarec - code generation macros
 *
 * This header is included by cpu_threaded.c and provides all the
 * generate_*, arm_*, and thumb_* macros for SH4 code emission.
 *
 * Phase 1: All higher-level macros copied from x86_emit.h structure.
 * Low-level generate_* operations emit NOPs for now.
 */

#ifndef SH4_EMIT_H
#define SH4_EMIT_H

/* Forward declarations */
u32 sh4_update_gba(u32 pc);
void sh4_indirect_branch_arm(u32 address);
void sh4_indirect_branch_thumb(u32 address);
void sh4_indirect_branch_dual(u32 address);
u32 function_cc sh4_execute_store_cpsr(u32 new_cpsr, u32 user_mask, u32 priv_mask);
extern void lookup_pc(void); /* in sh4_stub.S — block dispatch entry */
extern void write_epilogue(void); /* in sh4_stub.S — store alert handler */

/* JIT calls read/write_memory directly — no wrappers needed */
#define execute_load_u8   read_memory8
#define execute_load_u16  read_memory16
#define execute_load_u32  read_memory32
#define execute_load_s8   read_memory8s
#define execute_load_s16  read_memory16s
#define execute_store_u8  write_memory8
#define execute_store_u16 write_memory16
#define execute_store_u32 write_memory32
#define execute_store_aligned_u32 write_memory32

/* Debug/validation - define SH4_DYNAREC_DEBUG before including to enable */
#include "sh4_debug.h"

/* ---- SH4 instruction emission ---- */
#define sh4_emit16(value)                                                     \
  do { *((u16 *)translation_ptr) = (u16)(value);                              \
       translation_ptr += 2; } while(0)

/* ---- Register mapping ---- */
#define reg_base    8    /* r8: callee-saved, pointer to reg[] */
#define reg_cycles  9    /* r9: callee-saved, cycle counter */
#define reg_a0      4    /* r4: first C arg */
#define reg_a1      5    /* r5: second C arg */
#define reg_a2      6    /* r6: third C arg */
#define reg_t0      0    /* r0: scratch */
#define reg_rv      0    /* r0: return value */

#define a0    reg_a0
#define a1    reg_a1
#define a2    reg_a2
#define t0    reg_t0
#define rv    reg_rv
#define base  reg_base
#define arg0  reg_a0

#define block_prologue_size 0

/* ---- Trace stubs ---- */
#define emit_trace_arm_instruction(pc)
#define emit_trace_thumb_instruction(pc)

/* ---- Flag status bits ---- */
#define check_generate_n_flag   (flag_status & 0x08)
#define check_generate_z_flag   (flag_status & 0x04)
#define check_generate_c_flag   (flag_status & 0x02)
#define check_generate_v_flag   (flag_status & 0x01)

/* ==================================================================== */
/* Low-level generate_* primitives                                      */
/* ==================================================================== */

/*
 * SH4 encodings used (verified against sh4_opcodes.h):
 *
 *  mov #imm8, Rn           0xE000 | Rn<<8 | imm&0xFF       sign-ext to 32b
 *  mov Rm, Rn              0x6003 | Rn<<8 | Rm<<4
 *  not Rm, Rn              0x6007 | Rn<<8 | Rm<<4
 *  mov.l @(disp4,Rm), Rn   0x5000 | Rn<<8 | Rm<<4 | disp   EA = Rm + disp*4
 *  mov.l Rm, @(disp4,Rn)   0x1000 | Rn<<8 | Rm<<4 | disp   EA = Rn + disp*4
 *  mov.l @(R0,Rm), Rn      0x000E | Rn<<8 | Rm<<4           EA = R0 + Rm
 *  mov.l Rm, @(R0,Rn)      0x0006 | Rn<<8 | Rm<<4           EA = R0 + Rn
 *  mov.l @(disp8,PC), Rn   0xD000 | Rn<<8 | disp            EA = (PC+4)&~3 + disp*4
 *  mov.l Rm, @Rn           0x2002 | Rn<<8 | Rm<<4
 *  add #imm8, Rn           0x7000 | Rn<<8 | imm&0xFF
 *  bra disp12              0xA000 | disp&0xFFF               PC+4 + disp*2
 */

/* Load reg[reg_index] into SH4 register ireg.
 * ARM regs 0-15: single mov.l @(disp, r8), ireg
 * Extended regs >15: mov #offset, r0; mov.l @(r0, r8), ireg */
#define generate_load_reg(ireg, reg_index)                                    \
  do {                                                                        \
    if ((u32)(reg_index) <= 15) {                                             \
      sh4_emit16(0x5000 | ((ireg) << 8) | (reg_base << 4) | (reg_index));    \
    } else {                                                                  \
      sh4_emit16(0xE000 | ((u32)((reg_index) * 4) & 0xFF));                  \
      sh4_emit16(0x000E | ((ireg) << 8) | (reg_base << 4));                  \
    }                                                                         \
  } while(0)

/* Store SH4 register ireg into reg[reg_index].
 * ARM regs 0-15: single mov.l ireg, @(disp, r8)
 * Extended >15, ireg!=r0: mov #offset, r0; mov.l ireg, @(r0, r8)
 * Extended >15, ireg==r0: mov r8,r1; add #offset,r1; mov.l r0,@r1 */
#define generate_store_reg(ireg, reg_index)                                   \
  do {                                                                        \
    if ((u32)(reg_index) <= 15) {                                             \
      sh4_emit16(0x1000 | (reg_base << 8) | ((ireg) << 4) | (reg_index));    \
    } else if ((ireg) != 0) {                                                 \
      sh4_emit16(0xE000 | ((u32)((reg_index) * 4) & 0xFF));                  \
      sh4_emit16(0x0006 | (reg_base << 8) | ((ireg) << 4));                  \
    } else {                                                                  \
      sh4_emit16(0x6003 | (1 << 8) | (reg_base << 4));                       \
      sh4_emit16(0x7000 | (1 << 8) | ((u32)((reg_index) * 4) & 0xFF));      \
      sh4_emit16(0x2002 | (1 << 8) | (0 << 4));                              \
    }                                                                         \
  } while(0)

/* Load immediate into ireg.
 * Small (-128..127): mov #imm8, ireg
 * Large: inline constant with branch-over:
 *   mov.l @(d,PC), ireg; bra skip; nop; [pad]; .long val; skip: */
#define generate_load_imm(ireg, imm)                                          \
  do {                                                                        \
    u32 _li_v = (u32)(imm);                                                   \
    if ((s32)_li_v >= -128 && (s32)_li_v <= 127) {                            \
      sh4_emit16(0xE000 | ((ireg) << 8) | (_li_v & 0xFF));                   \
    } else {                                                                  \
      u8 *_li_s = translation_ptr;                                            \
      sh4_emit16(0);      /* placeholder: mov.l @(d,PC), ireg */              \
      sh4_emit16(0);      /* placeholder: bra skip */                         \
      sh4_emit16(0x0009); /* nop (delay slot) */                              \
      if ((uintptr_t)translation_ptr & 2)                                     \
        sh4_emit16(0x0009); /* pad to 4-byte align */                         \
      u8 *_li_c = translation_ptr;                                            \
      *(u32 *)translation_ptr = _li_v;                                        \
      translation_ptr += 4;                                                   \
      *(u16 *)_li_s = 0xD000 | ((ireg) << 8) |                               \
        (u32)((_li_c - (u8*)(((uintptr_t)_li_s + 4) & ~(uintptr_t)3)) >> 2);\
      *(u16 *)(_li_s + 2) = 0xA000 |                                         \
        ((u32)((translation_ptr - (_li_s + 2) - 4) >> 1) & 0xFFF);           \
    }                                                                         \
  } while(0)

/* Load PC constant (known at translation time) */
#define generate_load_pc(ireg, new_pc)                                        \
  generate_load_imm(ireg, new_pc)

/* Load reg or PC+offset: if reg_index==15 load pc+pc_off, else load reg */
#define generate_load_reg_pc(ireg, reg_index, pc_off)                         \
  do {                                                                        \
    if ((reg_index) == 15)                                                    \
      generate_load_imm(ireg, (pc + (pc_off)));                               \
    else                                                                      \
      generate_load_reg(ireg, reg_index);                                     \
  } while(0)

/* Store immediate value to reg[reg_index]. Uses r0 as scratch. */
#define generate_store_reg_i32(imm, reg_index)                                \
  do {                                                                        \
    generate_load_imm(0, imm);                                                \
    generate_store_reg(0, reg_index);                                         \
  } while(0)

/* Register-to-register move: mov Rm, Rn */
#define generate_mov(dest, src)                                               \
  sh4_emit16(0x6003 | ((dest) << 8) | ((src) << 4))

/* Bitwise NOT in place: not Rm, Rn (same register) */
#define generate_not(ireg)                                                    \
  sh4_emit16(0x6007 | ((ireg) << 8) | ((ireg) << 4))

/* ---- Arithmetic ---- */

/* add Rm, Rn -> Rn += Rm  (0x300C) */
#define generate_add(dest, src)                                               \
  sh4_emit16(0x300C | ((dest) << 8) | ((src) << 4));                          \
  sh4_res_reg = (dest); sh4_src_reg = (src); sh4_is_sub = 0

/* add #imm8, Rn (0x7000). For large imm: load into r0, add r0. */
#define generate_add_imm(ireg, imm)                                           \
  do {                                                                        \
    if ((s32)(imm) >= -128 && (s32)(imm) <= 127) {                            \
      sh4_emit16(0x7000 | ((ireg) << 8) | ((u32)(imm) & 0xFF));              \
    } else {                                                                  \
      generate_load_imm(0, imm);                                              \
      sh4_emit16(0x300C | ((ireg) << 8) | (0 << 4));                         \
    }                                                                         \
  } while(0)

/* d = s + i */
#define generate_add_reg_reg_imm(d, s, i)                                     \
  do {                                                                        \
    if ((d) != (s)) generate_mov(d, s);                                       \
    generate_add_imm(d, i);                                                   \
  } while(0)

/* sub Rm, Rn -> Rn -= Rm  (0x3008) */
#define generate_sub(dest, src)                                               \
  sh4_emit16(0x3008 | ((dest) << 8) | ((src) << 4));                          \
  sh4_res_reg = (dest); sh4_src_reg = (src); sh4_is_sub = 1

#define generate_sub_imm(ireg, imm)                                           \
  generate_add_imm(ireg, -(s32)(imm))

/* addc Rm, Rn -> Rn += Rm + T  (0x300E) */
#define generate_adc(dest, src)                                               \
  sh4_emit16(0x300E | ((dest) << 8) | ((src) << 4));                          \
  sh4_res_reg = (dest); sh4_src_reg = (src); sh4_is_sub = 0

/* subc Rm, Rn -> Rn -= Rm - T  (0x300A) */
#define generate_sbb(dest, src)                                               \
  sh4_emit16(0x300A | ((dest) << 8) | ((src) << 4));                          \
  sh4_res_reg = (dest); sh4_src_reg = (src); sh4_is_sub = 1

/* Load reg[ri] and add to src: src += reg[ri] */
#define generate_add_memreg(src, ri)                                          \
  do {                                                                        \
    generate_load_reg(0, ri);                                                 \
    sh4_emit16(0x300C | ((src) << 8) | (0 << 4));                            \
  } while(0)

/* ---- Logic ---- */

/* and Rm, Rn (0x2009) */
#define generate_and(dest, src)                                               \
  sh4_emit16(0x2009 | ((dest) << 8) | ((src) << 4));                          \
  sh4_res_reg = (dest)

/* and with immediate. Uses r0 as scratch (or and #imm,R0 if ireg==r0) */
#define generate_and_imm(ireg, imm)                                           \
  do {                                                                        \
    if ((ireg) != 0) {                                                        \
      generate_load_imm(0, imm);                                              \
      sh4_emit16(0x2009 | ((ireg) << 8) | (0 << 4));                         \
    } else if ((u32)(imm) <= 255) {                                           \
      sh4_emit16(0xC900 | ((u32)(imm) & 0xFF));                              \
    } else {                                                                  \
      generate_load_imm(1, imm);                                              \
      sh4_emit16(0x2009 | (0 << 8) | (1 << 4));                              \
    }                                                                         \
  } while(0)

/* or Rm, Rn (0x200B) */
#define generate_or(dest, src)                                                \
  sh4_emit16(0x200B | ((dest) << 8) | ((src) << 4));                          \
  sh4_res_reg = (dest)

/* xor Rm, Rn (0x200A) */
#define generate_xor(dest, src)                                               \
  sh4_emit16(0x200A | ((dest) << 8) | ((src) << 4));                          \
  sh4_res_reg = (dest)

#define generate_xor_imm(ireg, imm)                                           \
  do {                                                                        \
    if ((ireg) == 0 && (u32)(imm) <= 255) {                                   \
      sh4_emit16(0xCA00 | ((u32)(imm) & 0xFF));                              \
    } else {                                                                  \
      generate_load_imm(0, imm);                                              \
      sh4_emit16(0x200A | ((ireg) << 8) | (0 << 4));                         \
    }                                                                         \
  } while(0)

/* and/or/xor with memory reg slot - load into r0, operate */
#define generate_and_mem(imm, b, off)                                         \
  do { generate_load_imm(0, imm); generate_load_reg(1, off);                  \
       sh4_emit16(0x2009 | (1 << 8) | (0 << 4));                             \
       generate_store_reg(1, off); } while(0)

#define generate_or_mem(ireg, ri)                                             \
  do { generate_load_reg(0, ri);                                              \
       sh4_emit16(0x200B | (0 << 8) | ((ireg) << 4));                        \
       generate_store_reg(0, ri); } while(0)

#define generate_xor_mem(ireg, ri)                                            \
  do { generate_load_reg(0, ri);                                              \
       sh4_emit16(0x200A | (0 << 8) | ((ireg) << 4));                        \
       generate_store_reg(0, ri); } while(0)

/* ---- Comparisons / tests ---- */
/* These set the SH4 T-bit which flag update macros can read via movt */

/* tst Rm, Rn -> T = (Rn & Rm) == 0  (0x2008) */
#define generate_test_imm(ireg, imm)                                          \
  do { generate_load_imm(0, imm);                                            \
       sh4_emit16(0x2008 | ((ireg) << 8) | (0 << 4)); } while(0)

#define generate_test_memreg(ireg, ri)                                        \
  do { generate_load_reg(0, ri);                                              \
       sh4_emit16(0x2008 | (0 << 8) | ((ireg) << 4)); } while(0)

/* cmp/eq #imm, R0 (0x8800) - only works with R0, 8-bit signed */
/* For general: load imm, cmp/eq */
#define generate_cmp_imm(ireg, imm)                                           \
  do {                                                                        \
    if ((ireg) == 0 && (s32)(imm) >= -128 && (s32)(imm) <= 127) {            \
      sh4_emit16(0x8800 | ((u32)(imm) & 0xFF));                              \
    } else {                                                                  \
      generate_load_imm(0, imm);                                              \
      sh4_emit16(0x3000 | ((ireg) << 8) | (0 << 4));                         \
    }                                                                         \
  } while(0)

/* cmp/gt Rm, Rn -> T = (signed)Rn > (signed)Rm  (0x3007) */
#define generate_cmp_reg(dest, src)                                           \
  sh4_emit16(0x3007 | ((dest) << 8) | ((src) << 4))

#define generate_cmp_memreg(ireg, ri)                                         \
  do { generate_load_reg(0, ri);                                              \
       sh4_emit16(0x3007 | ((ireg) << 8) | (0 << 4)); } while(0)

/* ---- Shifts ---- */
/* SH4 SHLD Rm,Rn (0x400D): logical shift Rn by Rm (positive=left, negative=right)
 * SH4 SHAD Rm,Rn (0x400C): arithmetic shift, same convention */

#define generate_shift_left(ireg, imm)                                        \
  do { sh4_emit16(0xE000 | (0 << 8) | ((u32)(imm) & 0xFF));                 \
       sh4_emit16(0x400D | ((ireg) << 8) | (0 << 4)); } while(0)

#define generate_shift_right(ireg, imm)                                       \
  do { sh4_emit16(0xE000 | (0 << 8) | ((u32)(-(s32)(imm)) & 0xFF));         \
       sh4_emit16(0x400D | ((ireg) << 8) | (0 << 4)); } while(0)

#define generate_shift_right_arithmetic(ireg, imm)                            \
  do { sh4_emit16(0xE000 | (0 << 8) | ((u32)(-(s32)(imm)) & 0xFF));         \
       sh4_emit16(0x400C | ((ireg) << 8) | (0 << 4)); } while(0)

/* Rotate right by imm: no direct SH4 instruction for arbitrary rotate.
 * Implement as: (x >> imm) | (x << (32-imm)) */
#define generate_rotate_right(ireg, imm)                                      \
  do {                                                                        \
    generate_mov(1, ireg);                                                    \
    sh4_emit16(0xE000 | (0 << 8) | ((u32)(-(s32)(imm)) & 0xFF));            \
    sh4_emit16(0x400D | ((ireg) << 8) | (0 << 4));                           \
    sh4_emit16(0xE000 | (0 << 8) | ((u32)(32 - (imm)) & 0xFF));             \
    sh4_emit16(0x400D | (1 << 8) | (0 << 4));                                \
    sh4_emit16(0x200B | ((ireg) << 8) | (1 << 4));                           \
  } while(0)

/* Variable shifts: shift amount already in a1 (r5) */
#define generate_shift_left_var(ireg)                                         \
  sh4_emit16(0x400D | ((ireg) << 8) | (a1 << 4))

#define generate_shift_right_var(ireg)                                        \
  do { sh4_emit16(0x600B | (a1 << 8) | (a1 << 4)); /* neg a1, a1 */         \
       sh4_emit16(0x400D | ((ireg) << 8) | (a1 << 4)); } while(0)

#define generate_shift_right_arithmetic_var(ireg)                             \
  do { sh4_emit16(0x600B | (a1 << 8) | (a1 << 4)); /* neg a1, a1 */         \
       sh4_emit16(0x400C | ((ireg) << 8) | (a1 << 4)); } while(0)

#define generate_rotate_right_var(ireg)                                       \
  do {                                                                        \
    generate_mov(2, ireg); /* save in r2(a2) */                               \
    sh4_emit16(0x600B | (0 << 8) | (a1 << 4));  /* neg a1, r0 */             \
    sh4_emit16(0x400D | ((ireg) << 8) | (0 << 4)); /* shld r0, ireg (>>n) */ \
    sh4_emit16(0xE000 | (0 << 8) | 32);          /* mov #32, r0 */           \
    sh4_emit16(0x3008 | (0 << 8) | (a1 << 4));   /* sub a1, r0 -> 32-n */   \
    sh4_emit16(0x400D | (2 << 8) | (0 << 4));    /* shld r0, r2 (<<32-n) */ \
    sh4_emit16(0x200B | ((ireg) << 8) | (2 << 4)); /* or r2, ireg */         \
  } while(0)

/* Rotate right through carry (1-bit) */
/* rotcr Rn: T->MSB, LSB->T (0x4025) */
#define generate_rcr(ireg)                                                    \
  sh4_emit16(0x4025 | ((ireg) << 8))

#define generate_rcr1(ireg)                                                   \
  sh4_emit16(0x4025 | ((ireg) << 8))

/* ---- Inline shift+carry for immediate amounts ---- */
/* Replaces C function calls for the common case (known shift amount).
 * Extracts carry bit inline using SH4 instructions.
 * ireg = value register (a0), shift = amount (compile-time constant).
 * After: ireg = shifted result, reg[REG_C_FLAG] = carry (0 or 1). */

/* LSL #N carry: bit (32-N) of original → C. Result = val << N. */
#define generate_inline_lsl_carry(ireg, shift)                                \
  do {                                                                        \
    if ((shift) == 1) {                                                       \
      sh4_emit16(0x4000 | ((ireg) << 8)); /* shll ireg: MSB→T */            \
      sh4_emit16(0x0029 | (0 << 8)); /* movt r0 = carry */                  \
      generate_store_reg(0, REG_C_FLAG);                                      \
    } else {                                                                  \
      generate_mov(2, ireg); /* r2 = original */                              \
      generate_shift_left(ireg, shift); /* ireg <<= N */                     \
      sh4_emit16(0xE000 | (0 << 8) | ((u32)((shift) - 32) & 0xFF));         \
      sh4_emit16(0x400D | (2 << 8) | (0 << 4)); /* shld r0, r2: r2 >>= (32-N) */ \
      sh4_emit16(0x4001 | (2 << 8)); /* shlr r2: T = bit 0 = carry */       \
      sh4_emit16(0x0029 | (0 << 8)); /* movt r0 */                          \
      generate_store_reg(0, REG_C_FLAG);                                      \
    }                                                                         \
  } while(0)

/* LSR #N carry: bit (N-1) of original → C. Result = val >> N (logical). */
#define generate_inline_lsr_carry(ireg, shift)                                \
  do {                                                                        \
    if ((shift) == 1) {                                                       \
      sh4_emit16(0x4001 | ((ireg) << 8)); /* shlr ireg: LSB→T */            \
      sh4_emit16(0x0029 | (0 << 8)); /* movt r0 */                          \
      generate_store_reg(0, REG_C_FLAG);                                      \
    } else {                                                                  \
      generate_mov(2, ireg); /* r2 = original */                              \
      generate_shift_right(ireg, shift); /* ireg >>= N */                    \
      sh4_emit16(0xE000 | (0 << 8) | ((u32)(1 - (s32)(shift)) & 0xFF));     \
      sh4_emit16(0x400D | (2 << 8) | (0 << 4)); /* shld: r2 >>= (N-1) */   \
      sh4_emit16(0x4001 | (2 << 8)); /* shlr r2: T = carry */               \
      sh4_emit16(0x0029 | (0 << 8)); /* movt r0 */                          \
      generate_store_reg(0, REG_C_FLAG);                                      \
    }                                                                         \
  } while(0)

/* ASR #N carry: bit (N-1) of original → C. Result = (s32)val >> N. */
#define generate_inline_asr_carry(ireg, shift)                                \
  do {                                                                        \
    if ((shift) == 1) {                                                       \
      sh4_emit16(0x4021 | ((ireg) << 8)); /* shar ireg: LSB→T */            \
      sh4_emit16(0x0029 | (0 << 8)); /* movt r0 */                          \
      generate_store_reg(0, REG_C_FLAG);                                      \
    } else {                                                                  \
      generate_mov(2, ireg); /* r2 = original */                              \
      generate_shift_right_arithmetic(ireg, shift);                          \
      sh4_emit16(0xE000 | (0 << 8) | ((u32)(1 - (s32)(shift)) & 0xFF));     \
      sh4_emit16(0x400D | (2 << 8) | (0 << 4)); /* shld: r2 >>= (N-1) */   \
      sh4_emit16(0x4001 | (2 << 8)); /* shlr r2: T = carry */               \
      sh4_emit16(0x0029 | (0 << 8)); /* movt r0 */                          \
      generate_store_reg(0, REG_C_FLAG);                                      \
    }                                                                         \
  } while(0)

/* ROR #N carry: bit (N-1) of original → C. */
#define generate_inline_ror_carry(ireg, shift)                                \
  do {                                                                        \
    generate_mov(2, ireg); /* r2 = original */                                \
    generate_rotate_right(ireg, shift);                                      \
    sh4_emit16(0xE000 | (0 << 8) | ((u32)(1 - (s32)(shift)) & 0xFF));       \
    sh4_emit16(0x400D | (2 << 8) | (0 << 4)); /* shld: r2 >>= (N-1) */     \
    sh4_emit16(0x4001 | (2 << 8)); /* shlr r2: T = carry */                 \
    sh4_emit16(0x0029 | (0 << 8)); /* movt r0 */                            \
    generate_store_reg(0, REG_C_FLAG);                                        \
  } while(0)

/* ---- Safe C shift+carry helpers (kept for register-amount shifts) ---- */
/* These compute the shift result AND the correct carry flag in one call,
 * bypassing the broken generate_update_flag(c) which uses stale regs. */
static u32 function_cc sh4_lsl_carry(u32 val, u32 amount) {
  if (amount == 0) return val;  /* no shift, no carry change */
  if (amount < 32) {
    reg[REG_C_FLAG] = (val >> (32 - amount)) & 1;
    return val << amount;
  }
  reg[REG_C_FLAG] = (amount == 32) ? (val & 1) : 0;
  return 0;
}
static u32 function_cc sh4_lsr_carry(u32 val, u32 amount) {
  if (amount == 0) return val;
  if (amount < 32) {
    reg[REG_C_FLAG] = (val >> (amount - 1)) & 1;
    return val >> amount;
  }
  reg[REG_C_FLAG] = (amount == 32) ? ((val >> 31) & 1) : 0;
  return 0;
}
static u32 function_cc sh4_asr_carry(u32 val, u32 amount) {
  if (amount == 0) return val;
  if (amount < 32) {
    reg[REG_C_FLAG] = ((s32)val >> (amount - 1)) & 1;
    return (u32)((s32)val >> amount);
  }
  reg[REG_C_FLAG] = (val >> 31) & 1;
  return (u32)((s32)val >> 31);
}
static u32 function_cc sh4_ror_carry(u32 val, u32 amount) {
  if (amount == 0) return val;
  amount &= 31;
  if (amount == 0) { reg[REG_C_FLAG] = (val >> 31) & 1; return val; }
  reg[REG_C_FLAG] = (val >> (amount - 1)) & 1;
  return (val >> amount) | (val << (32 - amount));
}
static u32 function_cc sh4_rrx_carry(u32 val, u32 old_carry) {
  reg[REG_C_FLAG] = val & 1;
  return (val >> 1) | (old_carry ? 0x80000000u : 0);
}

/* ADC/SBC with full flag computation.
 * These bypass the broken inline flag macros which can't handle 3-operand
 * carry/overflow correctly. a0=operand2, a1=Rn for ADC; reversed for SBC. */
static u32 function_cc sh4_adc_flags(u32 op2, u32 rn) {
  u32 c_in = reg[REG_C_FLAG] & 1;
  u64 result64 = (u64)rn + (u64)op2 + c_in;
  u32 result = (u32)result64;
  reg[REG_N_FLAG] = (result >> 31) & 1;
  reg[REG_Z_FLAG] = (result == 0) ? 1 : 0;
  reg[REG_C_FLAG] = (result64 >> 32) & 1;
  reg[REG_V_FLAG] = ((~(rn ^ op2) & (rn ^ result)) >> 31) & 1;
  return result;
}

static u32 function_cc sh4_sbc_flags(u32 op2, u32 rn) {
  /* ARM SBC: Rd = Rn - Op2 - !C */
  u32 not_c = (reg[REG_C_FLAG] & 1) ^ 1;
  u64 result64 = (u64)rn - (u64)op2 - not_c;
  u32 result = (u32)result64;
  reg[REG_N_FLAG] = (result >> 31) & 1;
  reg[REG_Z_FLAG] = (result == 0) ? 1 : 0;
  /* ARM C = no borrow = (Rn >= Op2 + !C) */
  reg[REG_C_FLAG] = (result64 <= 0xFFFFFFFFULL) ? 1 : 0;
  reg[REG_V_FLAG] = (((rn ^ op2) & (rn ^ result)) >> 31) & 1;
  return result;
}

static u32 function_cc sh4_rsc_flags(u32 op2, u32 rn) {
  /* ARM RSC: Rd = Op2 - Rn - !C (reverse subtract with carry) */
  u32 not_c = (reg[REG_C_FLAG] & 1) ^ 1;
  u64 result64 = (u64)op2 - (u64)rn - not_c;
  u32 result = (u32)result64;
  reg[REG_N_FLAG] = (result >> 31) & 1;
  reg[REG_Z_FLAG] = (result == 0) ? 1 : 0;
  reg[REG_C_FLAG] = (result64 <= 0xFFFFFFFFULL) ? 1 : 0;
  reg[REG_V_FLAG] = (((op2 ^ rn) & (op2 ^ result)) >> 31) & 1;
  return result;
}

/* Emit: a0=value already loaded, set a1=shift_amount, call helper,
 * move result from rv(r0) to ireg(a0). */
#define generate_shift_carry_call(ireg, amount, func)                         \
  do {                                                                        \
    generate_load_imm(a1, amount);                                            \
    generate_function_call(func);                                             \
    if ((ireg) != rv) generate_mov(ireg, rv);                                 \
  } while(0)

/* ---- Multiply ---- */
/* mul.l Rm, Rn -> MACL = Rm * Rn (0x0007), then sts MACL, Rn (0x001A) */

#define generate_multiply(ireg)                                               \
  do { sh4_emit16(0x0007 | (a0 << 8) | ((ireg) << 4));  /* mul.l ireg, a0 */\
       sh4_emit16(0x001A | (a0 << 8)); } while(0)         /* sts MACL, a0 */

/* dmuls.l Rm, Rn -> MACH:MACL = signed Rm*Rn (0x300D) */
#define generate_multiply_s64(ireg)                                           \
  do { sh4_emit16(0x300D | (a0 << 8) | ((ireg) << 4));                       \
       sh4_emit16(0x001A | (a0 << 8));  /* sts MACL, a0 (low) */             \
       sh4_emit16(0x000A | (a1 << 8));  /* sts MACH, a1 (high) */            \
  } while(0)

/* dmulu.l Rm, Rn -> MACH:MACL = unsigned Rm*Rn (0x3005) */
#define generate_multiply_u64(ireg)                                           \
  do { sh4_emit16(0x3005 | (a0 << 8) | ((ireg) << 4));                       \
       sh4_emit16(0x001A | (a0 << 8));                                        \
       sh4_emit16(0x000A | (a1 << 8));                                        \
  } while(0)

/* 64-bit multiply-accumulate: TODO, stub for now */
#define generate_multiply_s64_add(a, b, c) sh4_emit16(0x0009)
#define generate_multiply_u64_add(a, b, c) sh4_emit16(0x0009)

/* ---- Function calls ---- */
/* Save PR (clobbered by jsr), call func, restore PR.
 * r8 (reg_base) and r9 (reg_cycles) are callee-saved per SH4 ABI,
 * so C functions will preserve them — no need to save/restore.
 * Args are already in r4-r6 (a0-a2), return in r0. */
#define generate_function_call(func)                                          \
  do {                                                                        \
    sh4_emit16(0x4022 | (15 << 8)); /* sts.l pr, @-r15 (push PR) */          \
    generate_load_imm(0, (u32)(func));                                        \
    sh4_emit16(0x400B | (0 << 8));  /* jsr @r0 */                             \
    sh4_emit16(0x0009);              /* nop (delay slot) */                    \
    sh4_emit16(0x4026 | (15 << 8)); /* lds.l @r15+, pr (pop PR) */           \
  } while(0)

/* ---- Exit block ---- */
#define generate_exit_block()                                                 \
  sh4_emit16(0x000b); /* rts */                                               \
  sh4_emit16(0x0009)  /* nop (delay slot) */

/* ---- Flag update dispatch ---- */
/* generate_update_flag dispatches on type: z, s, c, nc, o, nz
 * Must be defined before any call site (shift_imm_asr_flags uses nz). */
#define generate_update_flag(type, flag)  generate_update_flag_##type(flag)

/* nz flag: non-zero test (result != 0). Needed early for shift carry. */
#define generate_update_flag_nz(flag)                                         \
  do {                                                                        \
    sh4_emit16(0x2008 | (sh4_res_reg << 8) | (sh4_res_reg << 4));            \
    sh4_emit16(0x0029 | (0 << 8));  /* movt r0 = (result==0) */              \
    sh4_emit16(0xCA00 | 1);         /* xor #1 → r0 = (result!=0) */         \
    generate_store_reg(0, flag);                                              \
  } while(0)

/* SPSR load/store via C helpers (indexed by CPU mode) */
static u32 function_cc sh4_load_spsr(u32 mode_idx) {
  return spsr[mode_idx & 0xF];
}
static void function_cc sh4_store_spsr(u32 value, u32 mode_idx) {
  spsr[mode_idx & 0xF] = value;
}

#define generate_load_spsr(ireg, idx)                                         \
  do {                                                                        \
    generate_mov(a0, idx);                                                    \
    generate_function_call(sh4_load_spsr);                                    \
    if ((ireg) != rv) generate_mov(ireg, rv);                                 \
  } while(0)

#define generate_store_spsr(ireg, idx)                                        \
  do {                                                                        \
    generate_mov(a0, ireg);                                                   \
    generate_mov(a1, idx);                                                    \
    generate_function_call(sh4_store_spsr);                                   \
  } while(0)

/* ---- Cycle counting ---- */
/* Subtract accumulated cycle_count from reg_cycles (r9). */
#define generate_cycle_update()                                               \
  do {                                                                        \
    if (cycle_count) {                                                        \
      if (cycle_count <= 127) {                                               \
        sh4_emit16(0x7000 | (reg_cycles << 8) | ((u32)(-(s32)cycle_count) & 0xFF)); \
      } else {                                                                \
        generate_load_imm(0, cycle_count);                                    \
        sh4_emit16(0x3008 | (reg_cycles << 8) | (0 << 4)); /* sub r0, r9 */  \
      }                                                                       \
      cycle_count = 0;                                                        \
    }                                                                         \
  } while(0)

/* Subtract cycles AND check for exhaustion. Used at conditional branch
 * headers (loop points) so polling loops yield to update_gba. */
#define generate_cycle_update_and_check()                                     \
  do {                                                                        \
    generate_cycle_update();                                                   \
    sh4_emit16(0x4011 | (reg_cycles << 8)); /* cmp/pz r9 */                  \
    sh4_emit16(0x8900); /* bt placeholder */                                  \
    do {                                                                      \
      u8 *_cyc_patch = translation_ptr - 2;                                   \
      generate_store_reg_i32(pc, REG_PC); /* save PC for re-entry */          \
      generate_load_imm(0, pc); /* r0 for update_gba compat */                \
      sh4_emit16(0x4022 | (15 << 8)); /* sts.l pr, @-r15 */                  \
      generate_load_imm(1, (u32)sh4_update_gba);                              \
      sh4_emit16(0x400B | (1 << 8)); /* jsr @r1 */                           \
      sh4_emit16(0x0009); /* nop */                                           \
      sh4_emit16(0x4026 | (15 << 8)); /* lds.l @r15+, pr */                  \
      *(u16*)_cyc_patch = 0x8900 |                                            \
        (((translation_ptr - _cyc_patch - 4) / 2) & 0xFF);                   \
    } while(0);                                                               \
  } while(0)

#define generate_block_prologue()
#define generate_block_extra_vars_arm()                                       \
  u32 sh4_res_reg = a0; u32 sh4_src_reg = a1; u32 sh4_is_sub = 0;
#define generate_block_extra_vars_thumb()                                     \
  u32 sh4_res_reg = a0; u32 sh4_src_reg = a1; u32 sh4_is_sub = 0;

/* ---- Branch patching ---- */
/* backpatch_address points to the bt/bf instruction itself.
 * Patch the 8-bit displacement: target = PC+4+disp*2 where PC=bt_addr */
#define generate_branch_patch_conditional(dest, target)                       \
  do {                                                                        \
    u8 *_bf = (u8*)(dest);                                                    \
    s32 _d = ((u8*)(target) - _bf - 4) / 2;                                  \
    *(u16*)_bf = (*(u16*)_bf & 0xFF00) | (_d & 0xFF);                         \
  } while(0)

/* Patchable direct jump: initially jumps to lookup_pc (fallback),
 * but can be patched to jump directly to a translated block.
 * wb is set to the .long address for patching.
 * Must handle alignment: mov.l @(d,PC) EA = (PC+4)&~3 + d*4 */
#define generate_branch_filler(wb)                                            \
  do {                                                                        \
    u8 *_mov_addr = translation_ptr;                                          \
    sh4_emit16(0);                   /* placeholder: mov.l @(d,PC), r0 */    \
    sh4_emit16(0x402B | (0 << 8));   /* jmp @r0 */                           \
    sh4_emit16(0x0009);              /* nop (delay slot) */                   \
    if ((uintptr_t)translation_ptr & 2)                                       \
      sh4_emit16(0x0009);           /* pad to 4-byte align if needed */      \
    (wb) = translation_ptr;                                                   \
    *(u32*)translation_ptr = (u32)lookup_pc;  /* default: lookup_pc */        \
    translation_ptr += 4;                                                     \
    *(u16*)_mov_addr = 0xD000 | (0 << 8) |                                   \
      (u32)(((wb) - (u8*)(((uintptr_t)_mov_addr + 4) & ~(uintptr_t)3)) >> 2); \
  } while(0)

#define generate_branch_patch_unconditional(dest, target)                     \
  *(u32*)(dest) = (u32)(target)

/* ---- Branches ---- */
#define generate_branch_cycle_update(wb, pc_val)                              \
  generate_cycle_update();                                                    \
  generate_store_reg_i32(pc_val, REG_PC);                                     \
  sh4_emit16(0x4011 | (reg_cycles << 8)); /* cmp/pz r9 */                    \
  sh4_emit16(0x8900);  /* bt placeholder (skip if cycles remain) */           \
  do {                                                                        \
    u8 *_bt_patch = translation_ptr - 2;                                      \
    generate_load_imm(0, pc_val);  /* r0 = PC for update_gba */               \
    sh4_emit16(0x4022 | (15 << 8)); /* sts.l pr, @-r15 */                    \
    generate_load_imm(1, (u32)sh4_update_gba);                                \
    sh4_emit16(0x400B | (1 << 8)); /* jsr @r1 */                              \
    sh4_emit16(0x0009); /* nop (delay slot) */                                 \
    sh4_emit16(0x4026 | (15 << 8)); /* lds.l @r15+, pr */                    \
    /* Patch bt to skip here */                                                \
    *(u16*)_bt_patch = 0x8900 |                                               \
      (((translation_ptr - _bt_patch - 4) / 2) & 0xFF);                      \
  } while(0);                                                                 \
  generate_branch_filler(wb)

/* Branch without cycle update (conditional branches) */
#define generate_branch_no_cycle_update(wb, pc_val)                           \
  generate_store_reg_i32(pc_val, REG_PC);                                     \
  generate_branch_filler(wb)

/* Indirect branches: target PC already in a0 (r4) */
#define generate_indirect_branch_cycle_update(type)                           \
  generate_indirect_branch_cycle_update_##type()

#define generate_indirect_branch_cycle_update_arm()                           \
  generate_store_reg(a0, REG_PC);                                             \
  /* Subtract cycles but DON'T store PC in exhaustion check -                \
   * REG_PC already has the correct branch target from above. */             \
  do {                                                                        \
    if (cycle_count) {                                                        \
      if (cycle_count <= 127) {                                               \
        sh4_emit16(0x7000 | (reg_cycles << 8) |                              \
                   ((u32)(-(s32)cycle_count) & 0xFF));                        \
      } else {                                                                \
        generate_load_imm(0, cycle_count);                                    \
        sh4_emit16(0x3008 | (reg_cycles << 8) | (0 << 4));                   \
      }                                                                       \
      cycle_count = 0;                                                        \
      sh4_emit16(0x4011 | (reg_cycles << 8)); /* cmp/pz r9 */                \
      sh4_emit16(0x8900); /* bt placeholder */                                \
      do {                                                                    \
        u8 *_cyc_patch = translation_ptr - 2;                                 \
        /* REG_PC already set - just load r0 for update_gba compat */         \
        generate_load_reg(0, REG_PC);                                         \
        sh4_emit16(0x4022 | (15 << 8)); /* sts.l pr, @-r15 */                \
        generate_load_imm(1, (u32)sh4_update_gba);                            \
        sh4_emit16(0x400B | (1 << 8)); /* jsr @r1 */                         \
        sh4_emit16(0x0009); /* nop */                                         \
        sh4_emit16(0x4026 | (15 << 8)); /* lds.l @r15+, pr */                \
        *(u16*)_cyc_patch = 0x8900 |                                          \
          (((translation_ptr - _cyc_patch - 4) / 2) & 0xFF);                 \
      } while(0);                                                             \
    }                                                                         \
  } while(0);                                                                 \
  generate_exit_block()

#define generate_indirect_branch_cycle_update_thumb()                         \
  generate_indirect_branch_cycle_update_arm()

/* Variant for pop+PC / ldm+PC: REG_PC already stored, just check cycles */
#define generate_indirect_branch_exit_with_cycle()                            \
  generate_cycle_update();                                                    \
  generate_exit_block()

#define generate_indirect_branch_cycle_update_dual()                          \
  generate_store_reg(a0, REG_PC);                                             \
  do {                                                                        \
    if (cycle_count) {                                                        \
      if (cycle_count <= 127) {                                               \
        sh4_emit16(0x7000 | (reg_cycles << 8) |                              \
                   ((u32)(-(s32)cycle_count) & 0xFF));                        \
      } else {                                                                \
        generate_load_imm(0, cycle_count);                                    \
        sh4_emit16(0x3008 | (reg_cycles << 8) | (0 << 4));                   \
      }                                                                       \
      cycle_count = 0;                                                        \
      sh4_emit16(0x4011 | (reg_cycles << 8)); /* cmp/pz r9 */                \
      sh4_emit16(0x8900); /* bt placeholder */                                \
      do {                                                                    \
        u8 *_cyc_patch = translation_ptr - 2;                                 \
        generate_load_reg(0, REG_PC);                                         \
        sh4_emit16(0x4022 | (15 << 8));                                       \
        generate_load_imm(1, (u32)sh4_update_gba);                            \
        sh4_emit16(0x400B | (1 << 8));                                        \
        sh4_emit16(0x0009);                                                    \
        sh4_emit16(0x4026 | (15 << 8));                                       \
        *(u16*)_cyc_patch = 0x8900 |                                          \
          (((translation_ptr - _cyc_patch - 4) / 2) & 0xFF);                 \
      } while(0);                                                             \
    }                                                                         \
  } while(0);                                                                 \
  generate_load_reg(a0, REG_PC); /* reload target from saved PC */             \
  generate_indirect_branch_dual()

#define generate_indirect_branch_no_cycle_update(type)                        \
  generate_store_reg(a0, REG_PC);                                             \
  generate_exit_block()

#define generate_indirect_branch_arm()                                        \
  generate_store_reg(rv, REG_PC);                                             \
  generate_exit_block()

#define generate_indirect_branch_dual()                                       \
  generate_mov(0, a0); /* r0 = target addr (stub expects r0) */               \
  generate_load_imm(1, (u32)sh4_indirect_branch_dual);                        \
  sh4_emit16(0x402B | (1 << 8)); /* jmp @r1 */                               \
  sh4_emit16(0x0009) /* nop (delay slot) */

/* ---- Condition codes ---- */
/* Each condition emits code that skips the instruction when FALSE.
 * Ends with a bf/bt with placeholder disp, backpatched later.
 * Simple: load flag, tst, bf/bt.
 * Compound: normalize flags, combine, single bf at the end.
 *
 * bt = 0x8900 | disp  (branch if T=1)
 * bf = 0x8B00 | disp  (branch if T=0)
 * tst Rm,Rn = 0x2008  (T = (Rn & Rm) == 0)
 * movt Rn = 0x0029     (Rn = T)
 * cmp/eq Rm,Rn = 0x3000 (T = Rn == Rm)
 */

/* All conditions must set backpatch_address before the bt/bf.
 * generate_branch_patch_conditional later patches at backpatch_address. */

/* EQ: skip if Z==0. tst Z,Z → T=(Z==0) → bt skip */
#define generate_condition_eq(ireg)                                           \
  generate_load_reg(0, REG_Z_FLAG);                                           \
  sh4_emit16(0x2008 | (0 << 8) | (0 << 4));                                  \
  backpatch_address = translation_ptr;                                        \
  sh4_emit16(0x8900)

/* NE: skip if Z!=0. tst Z,Z → T=(Z==0) → bf skip */
#define generate_condition_ne(ireg)                                           \
  generate_load_reg(0, REG_Z_FLAG);                                           \
  sh4_emit16(0x2008 | (0 << 8) | (0 << 4));                                  \
  backpatch_address = translation_ptr;                                        \
  sh4_emit16(0x8B00)

/* CS: skip if C==0. tst C,C → T=(C==0) → bt skip */
#define generate_condition_cs(ireg)                                           \
  generate_load_reg(0, REG_C_FLAG);                                           \
  sh4_emit16(0x2008 | (0 << 8) | (0 << 4));                                  \
  backpatch_address = translation_ptr;                                        \
  sh4_emit16(0x8900)

/* CC: skip if C!=0. bf skip */
#define generate_condition_cc(ireg)                                           \
  generate_load_reg(0, REG_C_FLAG);                                           \
  sh4_emit16(0x2008 | (0 << 8) | (0 << 4));                                  \
  backpatch_address = translation_ptr;                                        \
  sh4_emit16(0x8B00)

/* MI: skip if N==0. bt skip */
#define generate_condition_mi(ireg)                                           \
  generate_load_reg(0, REG_N_FLAG);                                           \
  sh4_emit16(0x2008 | (0 << 8) | (0 << 4));                                  \
  backpatch_address = translation_ptr;                                        \
  sh4_emit16(0x8900)

/* PL: skip if N!=0. bf skip */
#define generate_condition_pl(ireg)                                           \
  generate_load_reg(0, REG_N_FLAG);                                           \
  sh4_emit16(0x2008 | (0 << 8) | (0 << 4));                                  \
  backpatch_address = translation_ptr;                                        \
  sh4_emit16(0x8B00)

/* VS: skip if V==0. bt skip */
#define generate_condition_vs(ireg)                                           \
  generate_load_reg(0, REG_V_FLAG);                                           \
  sh4_emit16(0x2008 | (0 << 8) | (0 << 4));                                  \
  backpatch_address = translation_ptr;                                        \
  sh4_emit16(0x8900)

/* VC: skip if V!=0. bf skip */
#define generate_condition_vc(ireg)                                           \
  generate_load_reg(0, REG_V_FLAG);                                           \
  sh4_emit16(0x2008 | (0 << 8) | (0 << 4));                                  \
  backpatch_address = translation_ptr;                                        \
  sh4_emit16(0x8B00)

/* HI: skip unless C!=0 AND Z==0 */
#define generate_condition_hi(ireg)                                           \
  do {                                                                        \
    generate_load_reg(2, REG_C_FLAG);                                         \
    generate_load_reg(3, REG_Z_FLAG);                                         \
    sh4_emit16(0x2008 | (3 << 8) | (3 << 4)); /* tst: T=(Z==0) */           \
    sh4_emit16(0x0029 | (3 << 8));             /* movt r3=(Z==0)?1:0 */      \
    sh4_emit16(0x2008 | (3 << 8) | (2 << 4)); /* tst r2,r3: T=((C&r3)==0) */\
    backpatch_address = translation_ptr;                                      \
    sh4_emit16(0x8900); /* bt skip (C==0 or Z!=0) */                          \
  } while(0)

/* LS: skip unless C==0 OR Z!=0 */
#define generate_condition_ls(ireg)                                           \
  do {                                                                        \
    generate_load_reg(2, REG_C_FLAG);                                         \
    generate_load_reg(3, REG_Z_FLAG);                                         \
    sh4_emit16(0x2008 | (3 << 8) | (3 << 4));                                \
    sh4_emit16(0x0029 | (3 << 8));                                            \
    sh4_emit16(0x2008 | (3 << 8) | (2 << 4));                                \
    backpatch_address = translation_ptr;                                      \
    sh4_emit16(0x8B00); /* bf skip (C!=0 and Z==0) */                         \
  } while(0)

/* GE: skip unless N==V */
#define generate_condition_ge(ireg)                                           \
  do {                                                                        \
    generate_load_reg(2, REG_N_FLAG);                                         \
    sh4_emit16(0x2008 | (2 << 8) | (2 << 4));                                \
    sh4_emit16(0x0029 | (2 << 8));                                            \
    generate_load_reg(3, REG_V_FLAG);                                         \
    sh4_emit16(0x2008 | (3 << 8) | (3 << 4));                                \
    sh4_emit16(0x0029 | (3 << 8));                                            \
    sh4_emit16(0x3000 | (2 << 8) | (3 << 4)); /* cmp/eq: T=(N==V) */        \
    backpatch_address = translation_ptr;                                      \
    sh4_emit16(0x8B00); /* bf skip (N!=V) */                                  \
  } while(0)

/* LT: skip unless N!=V */
#define generate_condition_lt(ireg)                                           \
  do {                                                                        \
    generate_load_reg(2, REG_N_FLAG);                                         \
    sh4_emit16(0x2008 | (2 << 8) | (2 << 4));                                \
    sh4_emit16(0x0029 | (2 << 8));                                            \
    generate_load_reg(3, REG_V_FLAG);                                         \
    sh4_emit16(0x2008 | (3 << 8) | (3 << 4));                                \
    sh4_emit16(0x0029 | (3 << 8));                                            \
    sh4_emit16(0x3000 | (2 << 8) | (3 << 4)); /* cmp/eq: T=(N==V) */        \
    backpatch_address = translation_ptr;                                      \
    sh4_emit16(0x8900); /* bt skip (N==V) */                                  \
  } while(0)

/* GT: skip unless Z==0 AND N==V */
#define generate_condition_gt(ireg)                                           \
  do {                                                                        \
    generate_load_reg(1, REG_Z_FLAG);                                         \
    sh4_emit16(0x2008 | (1 << 8) | (1 << 4));                                \
    sh4_emit16(0x0029 | (1 << 8));                                            \
    generate_load_reg(2, REG_N_FLAG);                                         \
    sh4_emit16(0x2008 | (2 << 8) | (2 << 4));                                \
    sh4_emit16(0x0029 | (2 << 8));                                            \
    generate_load_reg(3, REG_V_FLAG);                                         \
    sh4_emit16(0x2008 | (3 << 8) | (3 << 4));                                \
    sh4_emit16(0x0029 | (3 << 8));                                            \
    sh4_emit16(0x3000 | (2 << 8) | (3 << 4));                                \
    sh4_emit16(0x0029 | (2 << 8));                                            \
    sh4_emit16(0x2008 | (1 << 8) | (2 << 4));                                \
    backpatch_address = translation_ptr;                                      \
    sh4_emit16(0x8900); /* bt skip (Z||N!=V) */                               \
  } while(0)

/* LE: skip unless Z!=0 OR N!=V */
#define generate_condition_le(ireg)                                           \
  do {                                                                        \
    generate_load_reg(1, REG_Z_FLAG);                                         \
    sh4_emit16(0x2008 | (1 << 8) | (1 << 4));                                \
    sh4_emit16(0x0029 | (1 << 8));                                            \
    generate_load_reg(2, REG_N_FLAG);                                         \
    sh4_emit16(0x2008 | (2 << 8) | (2 << 4));                                \
    sh4_emit16(0x0029 | (2 << 8));                                            \
    generate_load_reg(3, REG_V_FLAG);                                         \
    sh4_emit16(0x2008 | (3 << 8) | (3 << 4));                                \
    sh4_emit16(0x0029 | (3 << 8));                                            \
    sh4_emit16(0x3000 | (2 << 8) | (3 << 4));                                \
    sh4_emit16(0x0029 | (2 << 8));                                            \
    sh4_emit16(0x2008 | (1 << 8) | (2 << 4));                                \
    backpatch_address = translation_ptr;                                      \
    sh4_emit16(0x8B00); /* bf skip (Z==0 and N==V) */                         \
  } while(0)

#define generate_condition(ireg)                                              \
  switch(condition) {                                                         \
    case 0x0: generate_condition_eq(ireg); break;                             \
    case 0x1: generate_condition_ne(ireg); break;                             \
    case 0x2: generate_condition_cs(ireg); break;                             \
    case 0x3: generate_condition_cc(ireg); break;                             \
    case 0x4: generate_condition_mi(ireg); break;                             \
    case 0x5: generate_condition_pl(ireg); break;                             \
    case 0x6: generate_condition_vs(ireg); break;                             \
    case 0x7: generate_condition_vc(ireg); break;                             \
    case 0x8: generate_condition_hi(ireg); break;                             \
    case 0x9: generate_condition_ls(ireg); break;                             \
    case 0xA: generate_condition_ge(ireg); break;                             \
    case 0xB: generate_condition_lt(ireg); break;                             \
    case 0xC: generate_condition_gt(ireg); break;                             \
    case 0xD: generate_condition_le(ireg); break;                             \
    case 0xE: break;                                                          \
    case 0xF: break;                                                          \
  }

/* ---- Store with PC handling ---- */
#define generate_store_reg_pc_no_flags(ireg, reg_index)                       \
  generate_store_reg(ireg, reg_index);                                        \
  if(reg_index == 15) {                                                       \
    generate_exit_block();                                                    \
  }

#define generate_store_reg_pc_flags(ireg, reg_index)                          \
  generate_store_reg(ireg, reg_index);                                        \
  if(reg_index == 15) {                                                       \
    generate_mov(arg0, ireg);                                                 \
    generate_function_call(execute_spsr_restore);                             \
    generate_mov(a0, rv);  /* move return addr from r0 to r4 for branch */   \
    generate_indirect_branch_dual();                                          \
  }

#define generate_store_reg_pc_thumb(ireg, rd)                                 \
  generate_store_reg(ireg, rd);                                               \
  if(rd == 15) {                                                              \
    generate_indirect_branch_exit_with_cycle();                               \
  }

/* ---- Branch ---- */
#define generate_branch()                                                     \
{                                                                             \
  if(condition == 0x0E) {                                                     \
    generate_branch_cycle_update(                                             \
     block_exits[block_exit_position].branch_source,                          \
     block_exits[block_exit_position].branch_target);                         \
  } else {                                                                    \
    generate_branch_no_cycle_update(                                          \
     block_exits[block_exit_position].branch_source,                          \
     block_exits[block_exit_position].branch_target);                         \
  }                                                                           \
  block_exit_position++;                                                      \
}

/* ---- Shift operand loading ---- */
#define generate_shift_load_operands_reg()                                    \
  generate_load_reg(a0, rd);                                                  \
  generate_load_reg(a1, rs)

#define generate_shift_load_operands_imm()                                    \
  generate_load_reg(a0, rs);                                                  \
  generate_load_imm(a1, imm)

#define get_shift_imm()                                                       \
  u32 shift = (opcode >> 7) & 0x1F

/* Shift by immediate amount */
#define generate_shift_imm(ireg, name, flags_op)                              \
  get_shift_imm();                                                            \
  generate_shift_imm_##name##_##flags_op(ireg)

/* Shift by register amount */
#define generate_shift_reg(ireg, name, flags_op)                              \
  generate_load_reg_pc(ireg, rm, 12);                                         \
  generate_load_reg(a1, ((opcode >> 8) & 0x0F));                              \
  generate_and_imm(a1, 0xFF);                                                 \
  generate_##name##_##flags_op##_reg(ireg)

#define generate_shift_imm_lsl_no_flags(ireg)                                 \
  generate_load_reg_pc(ireg, rm, 8);                                          \
  if(shift != 0) { generate_shift_left(ireg, shift); }

#define generate_shift_imm_lsr_no_flags(ireg)                                 \
  if(shift != 0) {                                                            \
    generate_load_reg_pc(ireg, rm, 8);                                        \
    generate_shift_right(ireg, shift);                                        \
  } else {                                                                    \
    generate_load_imm(ireg, 0);                                               \
  }

#define generate_shift_imm_asr_no_flags(ireg)                                 \
  generate_load_reg_pc(ireg, rm, 8);                                          \
  if(shift != 0) {                                                            \
    generate_shift_right_arithmetic(ireg, shift);                             \
  } else {                                                                    \
    generate_shift_right_arithmetic(ireg, 31);                                \
  }

#define generate_shift_imm_ror_no_flags(ireg)                                 \
  if(shift != 0) {                                                            \
    generate_load_reg_pc(ireg, rm, 8);                                        \
    generate_rotate_right(ireg, shift);                                       \
  } else {                                                                    \
    generate_load_reg_pc(ireg, rm, 8);                                        \
    generate_rrx(ireg);                                                       \
  }

#define generate_shift_imm_lsl_flags(ireg)                                    \
  generate_load_reg_pc(ireg, rm, 8);                                          \
  if(shift != 0) {                                                            \
    generate_inline_lsl_carry(ireg, shift);                                   \
  }

#define generate_shift_imm_lsr_flags(ireg)                                    \
  generate_load_reg_pc(ireg, rm, 8);                                          \
  if(shift != 0) {                                                            \
    generate_inline_lsr_carry(ireg, shift);                                   \
  } else {                                                                    \
    generate_shift_right(ireg, 31);                                           \
    generate_store_reg(ireg, REG_C_FLAG);                                     \
    generate_load_imm(ireg, 0);                                               \
  }

#define generate_shift_imm_asr_flags(ireg)                                    \
  generate_load_reg_pc(ireg, rm, 8);                                          \
  if(shift != 0) {                                                            \
    generate_inline_asr_carry(ireg, shift);                                   \
  } else {                                                                    \
    generate_shift_right_arithmetic(ireg, 31);                                \
    generate_update_flag(nz, REG_C_FLAG);                                     \
  }

#define generate_shift_imm_ror_flags(ireg)                                    \
  generate_load_reg_pc(ireg, rm, 8);                                          \
  if(shift != 0) {                                                            \
    generate_inline_ror_carry(ireg, shift);                                   \
  } else {                                                                    \
    generate_rrx_flags(ireg);                                                 \
  }

/* Register-based shifts: shift amount already in a1.
 * generate_shift_*_var does the actual SH4 shift instruction.
 * _flags variants also update flags after the shift. */
#define generate_lsl_no_flags_reg(ireg)   generate_shift_left_var(ireg)
#define generate_lsr_no_flags_reg(ireg)   generate_shift_right_var(ireg)
#define generate_asr_no_flags_reg(ireg)   generate_shift_right_arithmetic_var(ireg)
#define generate_ror_no_flags_reg(ireg)   generate_rotate_right_var(ireg)

/* Register shift with flags: a1 already has shift amount from
 * generate_shift_reg. Call C helper for correct carry. */
#define generate_lsl_flags_reg(ireg)                                          \
  generate_function_call(sh4_lsl_carry);                                      \
  if ((ireg) != rv) generate_mov(ireg, rv);                                   \
  generate_or(ireg, ireg);                                                    \
  update_logical_flags()

#define generate_lsr_flags_reg(ireg)                                          \
  generate_function_call(sh4_lsr_carry);                                      \
  if ((ireg) != rv) generate_mov(ireg, rv);                                   \
  generate_or(ireg, ireg);                                                    \
  update_logical_flags()

#define generate_asr_flags_reg(ireg)                                          \
  generate_function_call(sh4_asr_carry);                                      \
  if ((ireg) != rv) generate_mov(ireg, rv);                                   \
  generate_or(ireg, ireg);                                                    \
  update_logical_flags()

#define generate_ror_flags_reg(ireg)                                          \
  generate_function_call(sh4_ror_carry);                                      \
  if ((ireg) != rv) generate_mov(ireg, rv);                                   \
  generate_or(ireg, ireg);                                                    \
  update_logical_flags()

/* RRX: rotate right through carry (1-bit). Old C becomes MSB, old LSB
 * becomes new C. Use C helper for correctness. */
#define generate_rrx_flags(ireg)                                              \
  do {                                                                        \
    /* a0 already has the value. a1 = old carry. */                            \
    generate_load_reg(a1, REG_C_FLAG);                                        \
    generate_function_call(sh4_rrx_carry);                                    \
    if ((ireg) != rv) generate_mov(ireg, rv);                                 \
  } while(0)

#define generate_rrx(ireg)                                                    \
  generate_load_reg(a2, REG_C_FLAG);                                          \
  generate_shift_right(ireg, 1);                                              \
  generate_shift_left(a2, 31);                                                \
  generate_or(ireg, a2);

#define generate_load_rm_sh(flags_op)                                         \
  switch((opcode >> 4) & 0x07)                                                \
  {                                                                           \
    case 0x0:                                                                 \
    { generate_shift_imm(a0, lsl, flags_op); break; }                         \
    case 0x1:                                                                 \
    { generate_shift_reg(a0, lsl, flags_op); break; }                         \
    case 0x2:                                                                 \
    { generate_shift_imm(a0, lsr, flags_op); break; }                         \
    case 0x3:                                                                 \
    { generate_shift_reg(a0, lsr, flags_op); break; }                         \
    case 0x4:                                                                 \
    { generate_shift_imm(a0, asr, flags_op); break; }                         \
    case 0x5:                                                                 \
    { generate_shift_reg(a0, asr, flags_op); break; }                         \
    case 0x6:                                                                 \
    { generate_shift_imm(a0, ror, flags_op); break; }                         \
    case 0x7:                                                                 \
    { generate_shift_reg(a0, ror, flags_op); break; }                         \
  }

#define generate_load_offset_sh()                                             \
  switch((opcode >> 4) & 0x07)                                                \
  {                                                                           \
    case 0x0:                                                                 \
    { generate_shift_imm(a1, lsl, no_flags); break; }                         \
    case 0x2:                                                                 \
    { generate_shift_imm(a1, lsr, no_flags); break; }                         \
    case 0x4:                                                                 \
    { generate_shift_imm(a1, asr, no_flags); break; }                         \
    case 0x6:                                                                 \
    { generate_shift_imm(a1, ror, no_flags); break; }                         \
  }

/* ---- Flag collapse: pack N,Z,C,V back into CPSR bits 31-28 ---- */
#define collapse_flags(a, b)                                                  \
  generate_function_call(sh4_collapse_flags)

/* ---- Flag update macros (continued) ---- */
/* generate_update_flag already defined above (before shift macros).
 * Result register tracked in sh4_res_reg (set by arithmetic macros).
 * Second operand tracked in sh4_src_reg (for carry computation). */

/* Z flag: T = (result == 0), store T */
#define generate_update_flag_z(flag)                                          \
  do {                                                                        \
    sh4_emit16(0x2008 | (sh4_res_reg << 8) | (sh4_res_reg << 4));            \
    sh4_emit16(0x0029 | (0 << 8));                                            \
    generate_store_reg(0, flag);                                              \
  } while(0)

/* S/N flag: bit 31 of result → flag (non-zero = negative) */
#define generate_update_flag_s(flag)                                          \
  do {                                                                        \
    sh4_emit16(0x6003 | (0 << 8) | (sh4_res_reg << 4)); /* mov res, r0 */   \
    sh4_emit16(0xE000 | (1 << 8) | ((-31) & 0xFF)); /* mov #-31, r1 */      \
    sh4_emit16(0x400D | (0 << 8) | (1 << 4)); /* shld r1, r0: r0>>=31 */    \
    generate_store_reg(0, flag);                                              \
  } while(0)

/* C flag from addition: carry = (result < src) unsigned.
 * cmp/hs src, res → T = (res >= src) = !carry */
#define generate_update_flag_c(flag)                                          \
  do {                                                                        \
    sh4_emit16(0x3002 | (sh4_res_reg << 8) | (sh4_src_reg << 4));            \
    sh4_emit16(0x0029 | (0 << 8));  /* movt r0 = !carry */                   \
    sh4_emit16(0xCA00 | 1);         /* xor #1, r0 → carry */                \
    generate_store_reg(0, flag);                                              \
  } while(0)

/* C flag from subtraction (nc = NOT borrow = ARM carry).
 * Before sub, original dest was saved in r2 by data_proc macro.
 * ARM C = (original_dest >= src) unsigned.
 * cmp/hs src, r2 → T = (r2 >= src) = ARM C */
#define generate_update_flag_nc(flag)                                         \
  do {                                                                        \
    sh4_emit16(0x3002 | (2 << 8) | (sh4_src_reg << 4));                      \
    sh4_emit16(0x0029 | (0 << 8));  /* movt r0 = ARM C */                    \
    generate_store_reg(0, flag);                                              \
  } while(0)

/* V flag (overflow): use addv/subv on reconstructed/saved operands.
 * For add: reconstruct first operand (result - src), then addv.
 * For sub: saved in r2, use subv.
 * We detect add vs sub from context (sh4_is_sub flag). */
#define generate_update_flag_o(flag)                                          \
  do {                                                                        \
    if (sh4_is_sub) {                                                         \
      /* Sub: r2 = saved original dest, sh4_src_reg = subtrahend */           \
      sh4_emit16(0x6003 | (3 << 8) | (2 << 4)); /* mov r2, r3 */            \
      sh4_emit16(0x300B | (3 << 8) | (sh4_src_reg << 4)); /* subv src, r3 */\
    } else {                                                                  \
      /* Add: reconstruct first = result - src */                             \
      sh4_emit16(0x6003 | (3 << 8) | (sh4_res_reg << 4)); /* mov res, r3 */ \
      sh4_emit16(0x3008 | (3 << 8) | (sh4_src_reg << 4)); /* sub src, r3 */ \
      sh4_emit16(0x300F | (3 << 8) | (sh4_src_reg << 4)); /* addv src, r3 */\
    }                                                                         \
    sh4_emit16(0x0029 | (0 << 8));  /* movt r0 = overflow */                  \
    generate_store_reg(0, flag);                                              \
  } while(0)

/* nz flag: defined earlier (before shift macros) */

#define update_logical_flags()                                                \
  if (check_generate_z_flag) { generate_update_flag(z, REG_Z_FLAG); }         \
  if (check_generate_n_flag) { generate_update_flag(s, REG_N_FLAG); }

#define update_add_flags()                                                    \
  update_logical_flags()                                                      \
  if (check_generate_c_flag) { generate_update_flag(c, REG_C_FLAG); }         \
  if (check_generate_v_flag) { generate_update_flag(o, REG_V_FLAG); }

#define update_sub_flags()                                                    \
  update_logical_flags()                                                      \
  if (check_generate_c_flag) { generate_update_flag(nc, REG_C_FLAG); }        \
  if (check_generate_v_flag) { generate_update_flag(o, REG_V_FLAG); }

/* ==================================================================== */
/* ARM data processing leaf operations (match x86 signatures exactly)   */
/* ==================================================================== */

#define arm_data_proc_and(rd, storefnc)     generate_and(a0, a1); storefnc(a0, rd);
#define arm_data_proc_ands(rd, storefnc)    generate_and(a0, a1); update_logical_flags(); storefnc(a0, rd);
#define arm_data_proc_eor(rd, storefnc)     generate_xor(a0, a1); storefnc(a0, rd);
#define arm_data_proc_eors(rd, storefnc)    generate_xor(a0, a1); update_logical_flags(); storefnc(a0, rd);
#define arm_data_proc_orr(rd, storefnc)     generate_or(a0, a1); storefnc(a0, rd);
#define arm_data_proc_orrs(rd, storefnc)    generate_or(a0, a1); update_logical_flags(); storefnc(a0, rd);
#define arm_data_proc_bic(rd, storefnc)     generate_not(a0); generate_and(a0, a1); storefnc(a0, rd);
#define arm_data_proc_bics(rd, storefnc)    generate_not(a0); generate_and(a0, a1); update_logical_flags(); storefnc(a0, rd);

#define arm_data_proc_add(rd, storefnc)     generate_add(a0, a1); storefnc(a0, rd);
#define arm_data_proc_adds(rd, storefnc)    generate_add(a0, a1); update_add_flags(); storefnc(a0, rd);
#define arm_data_proc_sub(rd, storefnc)     generate_sub(a1, a0); storefnc(a1, rd);
#define arm_data_proc_subs(rd, storefnc)    generate_mov(2, a1); generate_sub(a1, a0); update_sub_flags(); storefnc(a1, rd);
#define arm_data_proc_rsb(rd, storefnc)     generate_sub(a0, a1); storefnc(a0, rd);
#define arm_data_proc_rsbs(rd, storefnc)    generate_mov(2, a0); generate_sub(a0, a1); update_sub_flags(); storefnc(a0, rd);

#define arm_data_proc_mul(rd, storefnc)     generate_multiply(a1); storefnc(a0, rd);
#define arm_data_proc_muls(rd, storefnc)    generate_multiply(a1); generate_and(a0, a0); update_logical_flags(); storefnc(a0, rd);

/* Set SH4 T = ARM C flag. shlr r0 shifts LSB into T. */
#define load_c_flag(tmpreg)                                                   \
  generate_load_reg(0, REG_C_FLAG);                                           \
  sh4_emit16(0x4001 | (0 << 8)); /* shlr r0: T = r0[0] = C */

/* Set SH4 T = !ARM C flag. tst r0,r0 → T = (r0==0) = !C. */
#define load_inv_c_flag(tmpreg)                                               \
  generate_load_reg(0, REG_C_FLAG);                                           \
  sh4_emit16(0x2008 | (0 << 8) | (0 << 4)); /* tst r0,r0: T = !C */

#define arm_data_proc_adc(rd, storefnc)     load_c_flag(a2) generate_adc(a0, a1); storefnc(a0, rd);
#define arm_data_proc_adcs(rd, storefnc)    generate_function_call(sh4_adc_flags); generate_mov(a0, rv); storefnc(a0, rd);
#define arm_data_proc_sbc(rd, storefnc)     load_inv_c_flag(a2) generate_sbb(a1, a0); storefnc(a1, rd);
#define arm_data_proc_sbcs(rd, storefnc)    generate_function_call(sh4_sbc_flags); generate_mov(a1, rv); storefnc(a1, rd);
#define arm_data_proc_rsc(rd, storefnc)     load_inv_c_flag(a2) generate_sbb(a0, a1); storefnc(a0, rd);
#define arm_data_proc_rscs(rd, storefnc)    generate_function_call(sh4_rsc_flags); generate_mov(a0, rv); storefnc(a0, rd);

#define arm_data_proc_test_tst()   generate_and(a0, a1); update_logical_flags()
#define arm_data_proc_test_teq()   generate_xor(a0, a1); update_logical_flags()
#define arm_data_proc_test_cmp()   generate_mov(2, a1); generate_sub(a1, a0); update_sub_flags()
#define arm_data_proc_test_cmn()   generate_add(a1, a0); update_add_flags()

#define arm_data_proc_unary_mov(rd, storefnc)    storefnc(a0, rd);
#define arm_data_proc_unary_movs(rd, storefnc)   arm_data_proc_unary_mov(rd, storefnc); generate_or(a0, a0); update_logical_flags()
#define arm_data_proc_unary_mvn(rd, storefnc)    generate_not(a0); storefnc(a0, rd);
#define arm_data_proc_unary_mvns(rd, storefnc)   arm_data_proc_unary_mvn(rd, storefnc); generate_or(a0, a0); update_logical_flags()
#define arm_data_proc_unary_neg(rd, storefnc)    generate_xor(a1, a1); arm_data_proc_subs(rd, storefnc)

/* ---- ARM data proc loaders ---- */
#define rm_op_reg rm
#define rm_op_imm imm

#define arm_data_proc_reg_flags()                                             \
  arm_decode_data_proc_reg(opcode);                                           \
  if(flag_status & 0x02) { generate_load_rm_sh(flags) }                       \
  else { generate_load_rm_sh(no_flags); }

#define arm_data_proc_reg()                                                   \
  arm_decode_data_proc_reg(opcode);                                           \
  generate_load_rm_sh(no_flags)

#define arm_data_proc_imm()                                                   \
  arm_decode_data_proc_imm(opcode);                                           \
  ror(imm, imm, imm_ror);                                                     \
  generate_load_imm(a0, imm)

#define arm_data_proc_imm_flags()                                             \
  arm_decode_data_proc_imm(opcode);                                           \
  if((flag_status & 0x02) && (imm_ror != 0)) {                                \
    generate_load_imm(a0, ((imm >> (imm_ror - 1)) & 0x01));                   \
    generate_store_reg(a0, REG_C_FLAG);                                       \
  }                                                                           \
  ror(imm, imm, imm_ror);                                                     \
  generate_load_imm(a0, imm)

/* ---- ARM data proc wrappers (exact x86 signature) ---- */
#define arm_data_proc(name, type, flags_op)                                   \
{                                                                             \
  arm_data_proc_##type();                                                     \
  generate_load_reg_pc(a1, rn, 8);                                            \
  arm_data_proc_##name(rd, generate_store_reg_pc_##flags_op);                 \
}

#define arm_data_proc_test(name, type)                                        \
{                                                                             \
  arm_data_proc_##type();                                                     \
  generate_load_reg_pc(a1, rn, 8);                                            \
  arm_data_proc_test_##name();                                                \
}

#define arm_data_proc_unary(name, type, flags_op)                             \
{                                                                             \
  arm_data_proc_##type();                                                     \
  arm_data_proc_unary_##name(rd, generate_store_reg_pc_##flags_op);           \
}

#define arm_data_proc_mov(type)                                               \
{                                                                             \
  arm_data_proc_##type();                                                     \
  generate_store_reg_pc_no_flags(a0, rd);                                     \
}

/* ---- Multiply ---- */
#define arm_multiply_flags_yes()                                              \
  generate_and(a0, a0);                                                       \
  generate_update_flag(z, REG_Z_FLAG);                                        \
  generate_update_flag(s, REG_N_FLAG);

#define arm_multiply_flags_no(_dest)

#define arm_multiply_add_no()
#define arm_multiply_add_yes()                                                \
  generate_load_reg(a1, rn);                                                  \
  generate_add(a0, a1)

#define arm_multiply(add_op, flags)                                           \
{                                                                             \
  arm_decode_multiply();                                                      \
  generate_load_reg(a0, rm);                                                  \
  generate_load_reg(a1, rs);                                                  \
  generate_multiply(a1);                                                      \
  arm_multiply_add_##add_op();                                                \
  arm_multiply_flags_##flags();                                               \
  generate_store_reg(a0, rd);                                                 \
}

#define arm_multiply_long_flags_yes()                                         \
  generate_mov(t0, a1);                                                       \
  generate_and(t0, t0);                                                       \
  generate_update_flag(s, REG_N_FLAG);                                        \
  generate_or(t0, a0);                                                        \
  generate_update_flag(z, REG_Z_FLAG);

#define arm_multiply_long_flags_no(_dest)

#define arm_multiply_long_add_yes(name)                                       \
  generate_load_reg(a2, rdlo);                                                \
  generate_load_reg(t0, rdhi);                                                \
  generate_multiply_##name(a1, a2, t0)

#define arm_multiply_long_add_no(name)                                        \
  generate_multiply_##name(a1)

#define arm_multiply_long(name, add_op, flags)                                \
{                                                                             \
  arm_decode_multiply_long();                                                 \
  generate_load_reg(a0, rm);                                                  \
  generate_load_reg(a1, rs);                                                  \
  arm_multiply_long_add_##add_op(name);                                       \
  generate_store_reg(a0, rdlo);                                               \
  generate_store_reg(a1, rdhi);                                               \
  arm_multiply_long_flags_##flags();                                          \
}

/* ---- PSR ---- */
#define execute_read_cpsr(oreg)    collapse_flags(oreg, a2)
#define execute_read_spsr(oreg)                                               \
  collapse_flags(oreg, a2);                                                   \
  generate_load_reg(oreg, CPU_MODE);                                          \
  generate_and_imm(oreg, 0xF);                                                \
  generate_load_spsr(oreg, oreg);

#define arm_psr_read(op_type, psr_reg)                                        \
  execute_read_##psr_reg(rv);                                                 \
  generate_store_reg(rv, rd)

#define arm_psr_load_new_reg()   generate_load_reg(a0, rm)
#define arm_psr_load_new_imm()   ror(imm, imm, imm_ror); generate_load_imm(a0, imm)

#define execute_store_cpsr()                                                  \
  generate_load_imm(a1, cpsr_masks[psr_pfield][0]);                           \
  generate_load_imm(a2, cpsr_masks[psr_pfield][1]);                           \
  generate_store_reg_i32(pc, REG_PC);                                         \
  generate_function_call(sh4_execute_store_cpsr);                             \
  /* rv (r0) = 0 or IRQ addr. If non-zero, reg[REG_PC] is already set. */   \
  sh4_emit16(0x2008 | (rv << 8) | (rv << 4)); /* tst r0,r0 */               \
  sh4_emit16(0x8900 | 1); /* bt +1 (skip rts if no IRQ) */                  \
  generate_exit_block() /* rts+nop: exit to lookup_pc → reg[REG_PC] */

#define execute_store_spsr()                                                  \
  generate_load_reg(a2, CPU_MODE);                                            \
  generate_and_imm(a2, 0xF);                                                  \
  generate_store_reg(a0, REG_SAVE);  /* save new value across func call */    \
  generate_store_reg(a2, REG_SAVE2); /* save mode index across func call */   \
  generate_load_spsr(a1, a2);        /* a1 = old SPSR (clobbers a0, a2) */   \
  generate_load_reg(a0, REG_SAVE);   /* restore new value */                  \
  generate_load_reg(a2, REG_SAVE2);  /* restore mode index */                 \
  generate_and_imm(a0,  spsr_masks[psr_pfield]);                              \
  generate_and_imm(a1, ~spsr_masks[psr_pfield]);                              \
  generate_or(a0, a1);                                                        \
  generate_store_spsr(a0, a2);

#define arm_psr_store(op_type, psr_reg)                                       \
  arm_psr_load_new_##op_type();                                               \
  execute_store_##psr_reg();

#define arm_psr(op_type, transfer_type, psr_reg)                              \
{                                                                             \
  arm_decode_psr_##op_type(opcode);                                           \
  arm_psr_##transfer_type(op_type, psr_reg);                                  \
}

/* ---- Memory access ---- */

/* Inline read fast path: emit memory_map_read[] table lookup directly into
 * the generated code.  Covers EWRAM, IWRAM, ROM with zero function-call
 * overhead.  Falls back to C for IO/palette/VRAM/OAM (NULL map entry).
 *
 * a0 (r4) = address on entry (set by caller).  Result in r0.
 * Clobbers r0, r1, r2 only.  r4 (address) and r5 (pc) preserved.
 *
 * align_mask: 0 = byte, 1 = halfword, 3 = word (0 skips check)
 * load_op:    mov.{b,w,l} @(r0,r1),r0 opcode
 * ext_op:     extu.{b,w} r0,r0 opcode (0 = none for 32-bit)
 * cfunc:      C fallback
 */
/* Inline read fast path via memory_map_read[].  Region guard filters out
 * BIOS(00, 16KB) and IO(04, 1KB) whose arrays are smaller than the 32KB
 * page mask.  Palette/OAM are NULL in the table so the NULL check handles
 * them.  VRAM(06) has entries but minor mirror inaccuracy is acceptable. */
#define generate_inline_mem_read(align_mask, load_op, ext_op, cfunc)          \
  do {                                                                        \
    u8 *_af = NULL, *_rf = NULL, *_io = NULL;                                 \
    /* r0 = address */                                                        \
    sh4_emit16(0x6003 | (0 << 8) | (reg_a0 << 4)); /* mov a0, r0 */         \
    if (align_mask) {                                                         \
      sh4_emit16(0xC800 | (align_mask)); /* tst #mask, r0 */                 \
      _af = translation_ptr;                                                  \
      sh4_emit16(0x8B00); /* bf -> slow (misaligned) */                      \
    }                                                                         \
    /* Region guard: only BIOS(00) and IO(04) are dangerous (small arrays   \
     * mapped to 32KB pages). Everything else is safe or NULL-caught. */     \
    sh4_emit16(0x4029 | (0 << 8)); /* shlr16 r0 */                           \
    sh4_emit16(0x4019 | (0 << 8)); /* shlr8 r0 -> region byte */            \
    sh4_emit16(0xE000 | (2 << 8) | 2); /* mov #2, r2 */                     \
    sh4_emit16(0x3002 | (0 << 8) | (2 << 4)); /* cmp/hs r2, r0 (>=2?) */   \
    _rf = translation_ptr;                                                    \
    sh4_emit16(0x8B00); /* bf -> slow (region 0x00-0x01) */                  \
    sh4_emit16(0x8800 | 0x04); /* cmp/eq #4, r0 (IO?) */                    \
    _io = translation_ptr;                                                    \
    sh4_emit16(0x8900); /* bt -> slow (IO regs) */                           \
    /* page lookup: reload address from a0 */                                 \
    sh4_emit16(0x6003 | (0 << 8) | (reg_a0 << 4)); /* mov a0, r0 */        \
    sh4_emit16(0xE000 | (1 << 8) | ((-15) & 0xFF)); /* mov #-15, r1 */      \
    sh4_emit16(0x400D | (0 << 8) | (1 << 4)); /* shld r1, r0 */             \
    sh4_emit16(0x4008 | (0 << 8)); /* shll2 r0 */                           \
    generate_load_imm(1, (u32)memory_map_read);                               \
    sh4_emit16(0x000E | (1 << 8) | (1 << 4)); /* mov.l @(r0,r1), r1 */      \
    sh4_emit16(0x2008 | (1 << 8) | (1 << 4)); /* tst r1, r1 */              \
    u8 *_nf = translation_ptr;                                                \
    sh4_emit16(0x8900); /* bt -> slow (NULL map) */                          \
    /* fast: map[address & 0x7FFF] */                                         \
    sh4_emit16(0xE0FF); /* mov #-1, r0 -> 0xFFFFFFFF */                      \
    sh4_emit16(0x4029 | (0 << 8)); /* shlr16 r0 -> 0x0000FFFF */            \
    sh4_emit16(0x4001 | (0 << 8)); /* shlr r0   -> 0x00007FFF */            \
    sh4_emit16(0x2009 | (0 << 8) | (reg_a0 << 4)); /* and a0, r0 */         \
    sh4_emit16(load_op);                                                      \
    if (ext_op) sh4_emit16(ext_op);                                           \
    u8 *_db = translation_ptr;                                                \
    sh4_emit16(0xA000); /* bra -> done (placeholder) */                      \
    sh4_emit16(0x0009); /* nop */                                             \
    /* -- slow path -- */                                                     \
    u8 *_sp = translation_ptr;                                                \
    if (_af) *(u16*)_af = 0x8B00 | (((_sp - _af - 4) / 2) & 0xFF);         \
    if (_rf) *(u16*)_rf = 0x8B00 | (((_sp - _rf - 4) / 2) & 0xFF);         \
    if (_io) *(u16*)_io = 0x8900 | (((_sp - _io - 4) / 2) & 0xFF);         \
    *(u16*)_nf = 0x8900 | (((_sp - _nf - 4) / 2) & 0xFF);                   \
    sh4_emit16(0x4022 | (15 << 8)); /* sts.l pr, @-r15 */                    \
    generate_load_imm(0, (u32)(cfunc));                                       \
    sh4_emit16(0x400B | (0 << 8)); /* jsr @r0 */                             \
    sh4_emit16(0x0009); /* nop */                                             \
    sh4_emit16(0x4026 | (15 << 8)); /* lds.l @r15+, pr */                    \
    /* -- patch bra -> done -- */                                             \
    *(u16*)_db = 0xA000 | (((translation_ptr - _db - 4) / 2) & 0xFFF);      \
  } while(0)

/* Byte-safe inline read: uses only mov.b to avoid SH4 alignment faults.
 * memory_map_read page pointers may not be halfword/word aligned. */
#define generate_inline_load_u8()                                             \
  generate_inline_mem_read(0, 0x001C, 0x600C, read_memory8)

#define generate_inline_load_u16()                                            \
  generate_inline_mem_read(1, 0x001D, 0x600D, read_memory16)
#define generate_inline_load_u32()                                            \
  generate_inline_mem_read(3, 0x001E, 0, read_memory32)
/* signed: uncommon, keep as C call */
#define generate_inline_load_s8()                                             \
  generate_function_call(execute_load_s8)
#define generate_inline_load_s16()                                            \
  generate_function_call(execute_load_s16)

#define arm_access_memory_load(mem_type)                                       \
  cycle_count += 2;                                                           \
  generate_load_pc(a1, pc);                                                   \
  generate_inline_load_##mem_type();                                          \
  generate_store_reg_pc_no_flags(rv, rd)

/* Inline write fast path.  Same memory_map_read[] lookup as reads — the
 * page pointers for EWRAM, IWRAM, VRAM point to the same backing buffers.
 * VRAM mirroring is handled by the page table entries (page 3 → vram base).
 * Palette(05)/OAM(07) are NULL → caught by NULL check → fall to C.
 *
 * Extra guard vs reads: ROM writes (0x08+) have save/GPIO side effects.
 * Guard: region < 2 → slow, region == 4 → slow, region >= 8 → slow. */
#define generate_inline_mem_write(store_op, cfunc)                            \
  do {                                                                        \
    u8 *_rf = NULL, *_io = NULL, *_rom = NULL;                                \
    sh4_emit16(0x6003 | (0 << 8) | (reg_a0 << 4)); /* mov a0, r0 */         \
    sh4_emit16(0x4029 | (0 << 8)); /* shlr16 r0 */                           \
    sh4_emit16(0x4019 | (0 << 8)); /* shlr8 r0 -> region */                 \
    sh4_emit16(0xE000 | (2 << 8) | 2); /* mov #2, r2 */                     \
    sh4_emit16(0x3002 | (0 << 8) | (2 << 4)); /* cmp/hs r2, r0 (>=2?) */   \
    _rf = translation_ptr;                                                    \
    sh4_emit16(0x8B00); /* bf -> slow (region 0x00-0x01) */                  \
    sh4_emit16(0x8800 | 0x04); /* cmp/eq #4, r0 (IO?) */                    \
    _io = translation_ptr;                                                    \
    sh4_emit16(0x8900); /* bt -> slow */                                     \
    sh4_emit16(0xE000 | (2 << 8) | 8); /* mov #8, r2 */                     \
    sh4_emit16(0x3002 | (0 << 8) | (2 << 4)); /* cmp/hs r2, r0 (>=8?) */   \
    _rom = translation_ptr;                                                   \
    sh4_emit16(0x8900); /* bt -> slow (ROM writes) */                        \
    /* page lookup */                                                         \
    sh4_emit16(0x6003 | (0 << 8) | (reg_a0 << 4)); /* mov a0, r0 */        \
    sh4_emit16(0xE000 | (1 << 8) | ((-15) & 0xFF)); /* mov #-15, r1 */      \
    sh4_emit16(0x400D | (0 << 8) | (1 << 4)); /* shld r1, r0 */             \
    sh4_emit16(0x4008 | (0 << 8)); /* shll2 r0 */                           \
    generate_load_imm(1, (u32)memory_map_read);                               \
    sh4_emit16(0x000E | (1 << 8) | (1 << 4)); /* mov.l @(r0,r1), r1 */      \
    sh4_emit16(0x2008 | (1 << 8) | (1 << 4)); /* tst r1, r1 */              \
    u8 *_nf = translation_ptr;                                                \
    sh4_emit16(0x8900); /* bt -> slow (NULL = palette/OAM) */               \
    /* store: map[address & 0x7FFF] = value */                                \
    sh4_emit16(0xE0FF); /* mov #-1, r0 */                                    \
    sh4_emit16(0x4029 | (0 << 8)); /* shlr16 r0 -> 0xFFFF */                \
    sh4_emit16(0x4001 | (0 << 8)); /* shlr r0   -> 0x7FFF */                \
    sh4_emit16(0x2009 | (0 << 8) | (reg_a0 << 4)); /* and a0, r0 */         \
    sh4_emit16(store_op); /* mov.x a1, @(r0, r1) */                          \
    sh4_emit16(0xE000 | (0 << 8) | 0); /* mov #0, r0 (ALERT_NONE) */        \
    u8 *_db = translation_ptr;                                                \
    sh4_emit16(0xA000); /* bra -> done */                                    \
    sh4_emit16(0x0009); /* nop */                                             \
    /* -- slow path -- */                                                     \
    u8 *_sp = translation_ptr;                                                \
    *(u16*)_rf = 0x8B00 | (((_sp - _rf - 4) / 2) & 0xFF);                   \
    *(u16*)_io = 0x8900 | (((_sp - _io - 4) / 2) & 0xFF);                   \
    *(u16*)_rom = 0x8900 | (((_sp - _rom - 4) / 2) & 0xFF);                 \
    *(u16*)_nf = 0x8900 | (((_sp - _nf - 4) / 2) & 0xFF);                   \
    sh4_emit16(0x4022 | (15 << 8)); /* sts.l pr, @-r15 */                    \
    generate_load_imm(0, (u32)(cfunc));                                       \
    sh4_emit16(0x400B | (0 << 8)); /* jsr @r0 */                             \
    sh4_emit16(0x0009); /* nop */                                             \
    sh4_emit16(0x4026 | (15 << 8)); /* lds.l @r15+, pr */                    \
    *(u16*)_db = 0xA000 | (((translation_ptr - _db - 4) / 2) & 0xFFF);      \
  } while(0)

/* store opcodes: mov.b r5,@(r0,r1)=0x0154  mov.w=0x0155  mov.l=0x0156 */
#define generate_inline_store_u8()                                            \
  generate_inline_mem_write(0x0154, write_memory8)
#define generate_inline_store_u16()                                           \
  generate_inline_mem_write(0x0155, write_memory16)
#define generate_inline_store_u32()                                           \
  generate_inline_mem_write(0x0156, write_memory32)

#define arm_access_memory_store(mem_type)                                      \
  cycle_count++;                                                              \
  generate_load_reg_pc(a1, rd, 12);                                           \
  generate_store_reg_i32(pc + 4, REG_PC);                                     \
  generate_inline_store_##mem_type();                                         \
  generate_store_alert_check()

#define no_op

#define arm_access_memory_writeback_yes(off_op)                               \
  reg[rn] = address off_op

#define arm_access_memory_writeback_no(off_op)

#define arm_access_memory_adjust_op_up      add
#define arm_access_memory_adjust_op_down    sub
#define arm_access_memory_reverse_op_up     sub
#define arm_access_memory_reverse_op_down   add

#define arm_access_memory_reg_pre(adj, rev)                                   \
  generate_load_reg_pc(a0, rn, 8);                                            \
  generate_##adj(a0, a1)

#define arm_access_memory_reg_pre_wb(adj, rev)                                \
  arm_access_memory_reg_pre(adj, rev);                                        \
  generate_store_reg(a0, rn)

#define arm_access_memory_reg_post(adj, rev)                                  \
  generate_load_reg(a0, rn);                                                  \
  generate_##adj(a0, a1);                                                     \
  generate_store_reg(a0, rn);                                                 \
  generate_##rev(a0, a1)

#define arm_access_memory_imm_pre(adj, rev)                                   \
  generate_load_reg_pc(a0, rn, 8);                                            \
  generate_##adj##_imm(a0, offset)

#define arm_access_memory_imm_pre_wb(adj, rev)                                \
  arm_access_memory_imm_pre(adj, rev);                                        \
  generate_store_reg(a0, rn)

#define arm_access_memory_imm_post(adj, rev)                                  \
  generate_load_reg(a0, rn);                                                  \
  generate_##adj##_imm(a0, offset);                                           \
  generate_store_reg(a0, rn);                                                 \
  generate_##rev##_imm(a0, offset)

#define arm_data_trans_reg(adjust_op, adj_dir, rev_dir)                        \
  arm_decode_data_trans_reg();                                                \
  generate_load_offset_sh();                                                  \
  arm_access_memory_reg_##adjust_op(adj_dir, rev_dir)

#define arm_data_trans_imm(adjust_op, adj_dir, rev_dir)                       \
  arm_decode_data_trans_imm();                                                \
  arm_access_memory_imm_##adjust_op(adj_dir, rev_dir)

#define arm_data_trans_half_reg(adjust_op, adj_dir, rev_dir)                  \
  arm_decode_half_trans_r();                                                  \
  generate_load_reg(a1, rm);                                                  \
  arm_access_memory_reg_##adjust_op(adj_dir, rev_dir)

#define arm_data_trans_half_imm(adjust_op, adj_dir, rev_dir)                  \
  arm_decode_half_trans_of();                                                 \
  arm_access_memory_imm_##adjust_op(adj_dir, rev_dir)

#define arm_access_memory(access_type, direction, adjust_op, mem_type,        \
 offset_type)                                                                 \
{                                                                             \
  arm_data_trans_##offset_type(adjust_op,                                     \
   arm_access_memory_adjust_op_##direction,                                   \
   arm_access_memory_reverse_op_##direction);                                 \
  arm_access_memory_##access_type(mem_type);                                  \
}

/* ---- Block memory ---- */
#define word_bit_count(word)                                                  \
  (bit_count[word >> 8] + bit_count[word & 0xFF])

#define arm_block_memory_load()                                               \
  generate_load_pc(a1, pc);                                                   \
  generate_function_call(execute_load_u32);                                   \
  generate_store_reg(rv, i)

#define arm_block_memory_store()                                              \
  generate_load_reg_pc(a1, i, 8);                                             \
  generate_function_call(execute_store_aligned_u32)

#define arm_block_memory_final_load(wb_type)   arm_block_memory_load()
#define arm_block_memory_final_store(wb_type)                                 \
  generate_load_reg_pc(a1, i, 12);                                            \
  arm_block_memory_writeback_post_store(wb_type);                             \
  generate_store_reg_i32(pc + 4, REG_PC);                                     \
  generate_function_call(execute_store_u32);                                  \
  generate_store_alert_check()

#define arm_block_memory_adjust_pc_store()
#define arm_block_memory_adjust_pc_load()                                     \
  if(reg_list & 0x8000) {                                                     \
    generate_exit_block();                                                    \
  }

#define arm_block_memory_offset_down_a()                                      \
  generate_add_imm(a0, -((word_bit_count(reg_list) * 4) - 4))
#define arm_block_memory_offset_down_b()                                      \
  generate_add_imm(a0, -(word_bit_count(reg_list) * 4))
#define arm_block_memory_offset_no()
#define arm_block_memory_offset_up()   generate_add_imm(a0, 4)

#define arm_block_memory_writeback_down()                                     \
  generate_load_reg(a2, rn);                                                  \
  generate_add_imm(a2, -(word_bit_count(reg_list) * 4));                      \
  generate_store_reg(a2, rn)

#define arm_block_memory_writeback_up()                                       \
  generate_load_reg(a2, rn);                                                  \
  generate_add_imm(a2, (word_bit_count(reg_list) * 4));                       \
  generate_store_reg(a2, rn)

#define arm_block_memory_writeback_no()

#define arm_block_memory_writeback_pre_load(wb_type)                          \
  if(!((reg_list >> rn) & 0x01)) { arm_block_memory_writeback_##wb_type(); }

#define arm_block_memory_writeback_pre_store(wb_type)

#define arm_block_memory_writeback_post_store(wb_type)                        \
  arm_block_memory_writeback_##wb_type()

#define arm_block_memory(access_type, offset_type, writeback_type, s_bit)     \
{                                                                             \
  arm_decode_block_trans();                                                   \
  u32 offset = 0;                                                             \
  u32 i;                                                                      \
  generate_load_reg(a0, rn);                                                  \
  arm_block_memory_offset_##offset_type();                                    \
  generate_and_imm(a0, ~0x03);                                                \
  generate_store_reg(a0, REG_SAVE3);                                          \
  arm_block_memory_writeback_pre_##access_type(writeback_type);               \
  for(i = 0; i < 16; i++) {                                                   \
    if((reg_list >> i) & 0x01) {                                              \
      cycle_count++;                                                          \
      generate_load_reg(a0, REG_SAVE3);                                       \
      generate_add_imm(a0, offset);                                           \
      if(reg_list & ~((2 << i) - 1)) {                                        \
        arm_block_memory_##access_type();                                     \
        offset += 4;                                                          \
      } else {                                                                \
        arm_block_memory_final_##access_type(writeback_type);                 \
      }                                                                       \
    }                                                                         \
  }                                                                           \
  arm_block_memory_adjust_pc_##access_type();                                 \
}

/* ---- Swap ---- */
#define arm_swap(type)                                                        \
{                                                                             \
  arm_decode_swap();                                                          \
  cycle_count += 3;                                                           \
  generate_load_reg(a0, rn);                                                  \
  generate_load_pc(a1, pc);                                                   \
  generate_function_call(execute_load_##type);                                \
  generate_mov(a2, rv);                                                       \
  generate_load_reg(a0, rn);                                                  \
  generate_load_reg(a1, rm);                                                  \
  generate_store_reg(a2, rd);                                                 \
  generate_store_reg_i32(pc + 4, REG_PC);                                     \
  generate_function_call(execute_store_##type);                               \
  generate_store_alert_check();                                               \
}

/* Collapse separate flag slots back into CPSR (same as interpreter's
 * collapse_flags but reading from reg[] memory slots). Must be called
 * before any C code that reads reg[REG_CPSR] for flag bits. */
void function_cc sh4_collapse_flags(void)
{
  reg[REG_CPSR] = (reg[REG_N_FLAG] << 31) | (reg[REG_Z_FLAG] << 30) |
                  (reg[REG_C_FLAG] << 29) | (reg[REG_V_FLAG] << 28) |
                  (reg[REG_CPSR] & 0xFF);
}

/* Extract flags from CPSR into separate slots. Called on entry to
 * execute_arm_translate_internal so flags survive across frame boundaries. */
void function_cc sh4_extract_flags(void)
{
  reg[REG_N_FLAG] = (reg[REG_CPSR] >> 31) & 1;
  reg[REG_Z_FLAG] = (reg[REG_CPSR] >> 30) & 1;
  reg[REG_C_FLAG] = (reg[REG_CPSR] >> 29) & 1;
  reg[REG_V_FLAG] = (reg[REG_CPSR] >> 28) & 1;
}

/* After a store function call, check r0 (return value).
 * If non-zero, jump to write_epilogue in sh4_stub.S which handles
 * SMC/IRQ/HALT in assembly with access to r9 (reg_cycles). */
#define generate_store_alert_check()                                          \
  do {                                                                        \
    sh4_emit16(0x2008 | (0 << 8) | (0 << 4)); /* tst r0, r0 */             \
    sh4_emit16(0x8900); /* bt skip (no alert, T=1 means r0==0) */           \
    u8 *_alert_patch = translation_ptr - 2;                                  \
    generate_mov(a0, rv); /* a0 = alert type for write_epilogue */           \
    generate_load_imm(0, (u32)write_epilogue);                               \
    sh4_emit16(0x402B | (0 << 8)); /* jmp @r0 */                            \
    sh4_emit16(0x0009); /* nop (delay slot) */                               \
    *(u16*)_alert_patch = 0x8900 |                                           \
      (((translation_ptr - _alert_patch - 4) / 2) & 0xFF);                  \
  } while(0)

/* ---- SWI and execute helpers ---- */
static void function_cc execute_swi(u32 pc)
{
  reg[REG_BUS_VALUE] = 0xe3a02004;
  REG_MODE(MODE_SUPERVISOR)[6] = pc;
  REG_SPSR(MODE_SUPERVISOR) = reg[REG_CPSR];
  reg[REG_CPSR] = (reg[REG_CPSR] & ~0x3F) | 0x13 | 0x80;
  set_cpu_mode(MODE_SUPERVISOR);
}

static u32 execute_store_cpsr_body(void);

/* Called from JIT: a0=new_cpsr, a1=user_mask, a2=privileged_mask */
u32 function_cc sh4_execute_store_cpsr(u32 new_cpsr, u32 user_mask, u32 priv_mask)
{
  u32 store_mask = (reg[CPU_MODE] & 0x10) ? priv_mask : user_mask;
  u32 old_cpsr = reg[REG_CPSR];
  reg[REG_CPSR] = (new_cpsr & store_mask) | (old_cpsr & ~store_mask);
  sh4_extract_flags();
  if (store_mask & 0xFF) {
    u32 new_pc = execute_store_cpsr_body();
    if (new_pc) {
      reg[REG_PC] = new_pc;
      sh4_extract_flags();
      return new_pc;
    }
  }
  return 0;
}

u32 execute_store_cpsr_body()
{
  u32 address = 0;
  set_cpu_mode(cpu_modes[reg[REG_CPSR] & 0xF]);
  if((io_registers[REG_IE] & io_registers[REG_IF]) &&
      io_registers[REG_IME] && ((reg[REG_CPSR] & 0x80) == 0))
  {
    REG_MODE(MODE_IRQ)[6] = reg[REG_PC] + 4;
    REG_SPSR(MODE_IRQ) = reg[REG_CPSR];
    reg[REG_CPSR] = (reg[REG_CPSR] & 0xFFFFFF00) | 0xD2;
    set_cpu_mode(MODE_IRQ);
    return 0x00000018;
  }
  return 0;
}

u32 execute_spsr_restore(u32 address)
{
  if(reg[CPU_MODE] != MODE_USER && reg[CPU_MODE] != MODE_SYSTEM)
  {
    reg[REG_CPSR] = REG_SPSR(reg[CPU_MODE]);
    sh4_extract_flags();
    set_cpu_mode(cpu_modes[reg[REG_CPSR] & 0xF]);

    if((io_registers[REG_IE] & io_registers[REG_IF]) &&
       io_registers[REG_IME] && ((reg[REG_CPSR] & 0x80) == 0))
    {
      REG_MODE(MODE_IRQ)[6] = reg[REG_PC] + 4;
      REG_SPSR(MODE_IRQ) = reg[REG_CPSR];
      reg[REG_CPSR] = 0xD2;
      address = 0x00000018;
      set_cpu_mode(MODE_IRQ);
    }

    if(reg[REG_CPSR] & 0x20)
      address |= 0x01;
  }
  return address;
}

/* ---- ARM branches ---- */
#define arm_conditional_block_header()                                        \
  generate_cycle_update_and_check();                                          \
  generate_condition(a0);

#define arm_b()            generate_branch()
#define arm_bl()                                                              \
  generate_load_pc(a0, (pc + 4));                                             \
  generate_store_reg(a0, REG_LR);                                             \
  generate_branch()

#define arm_bx()                                                              \
  arm_decode_branchx(opcode);                                                 \
  generate_load_reg_pc(a0, rn, 8);                                            \
  generate_indirect_branch_dual();

#define arm_swi()                                                             \
  collapse_flags(a0, a1);                                                     \
  generate_load_pc(arg0, (pc + 4));                                           \
  generate_function_call(execute_swi);                                        \
  generate_branch()

#define arm_process_cheats()    generate_function_call(process_cheats);

/* ---- ARM HLE div ---- */
static void function_cc sh4_hle_div(void)
{
  s32 num = (s32)reg[0];
  s32 den = (s32)reg[1];
  if (den == 0) { /* Division by zero: match BIOS behavior */
    reg[0] = (num < 0) ? -1 : 1;
    reg[1] = (u32)num;
    reg[3] = 1;
  } else {
    reg[0] = (u32)(num / den);
    reg[1] = (u32)(num % den);
    reg[3] = (u32)(num / den < 0 ? -(num / den) : num / den);
  }
}

static void function_cc sh4_hle_div_arm(void)
{
  s32 num = (s32)reg[1];
  s32 den = (s32)reg[0];
  if (den == 0) {
    reg[0] = (num < 0) ? -1 : 1;
    reg[1] = (u32)num;
    reg[3] = 1;
  } else {
    reg[0] = (u32)(num / den);
    reg[1] = (u32)(num % den);
    reg[3] = (u32)(num / den < 0 ? -(num / den) : num / den);
  }
}

#define arm_hle_div(cpu_mode)                                                 \
{                                                                             \
  generate_function_call(sh4_hle_div);                                        \
}

#define arm_hle_div_arm(cpu_mode)                                             \
{                                                                             \
  generate_function_call(sh4_hle_div_arm);                                    \
}

/* ---- Translation gate ---- */
#define generate_translation_gate(type)                                       \
  generate_load_pc(a0, pc);                                                   \
  generate_indirect_branch_no_cycle_update(type)

/* ==================================================================== */
/* Thumb macros (exact x86 signatures)                                   */
/* ==================================================================== */

#define thumb_rn_op_reg(_rn)     generate_load_reg(a0, _rn)
#define thumb_rn_op_imm(_imm)    generate_load_imm(a0, _imm)

#define thumb_data_proc(type, name, rn_type, _rd, _rs, _rn)                   \
{                                                                             \
  thumb_decode_##type();                                                      \
  thumb_rn_op_##rn_type(_rn);                                                 \
  generate_load_reg(a1, _rs);                                                 \
  arm_data_proc_##name(_rd, generate_store_reg);                              \
}

#define thumb_data_proc_test(type, name, rn_type, _rs, _rn)                   \
{                                                                             \
  thumb_decode_##type();                                                      \
  thumb_rn_op_##rn_type(_rn);                                                 \
  generate_load_reg(a1, _rs);                                                 \
  arm_data_proc_test_##name();                                                \
}

#define thumb_data_proc_unary(type, name, rn_type, _rd, _rn)                  \
{                                                                             \
  thumb_decode_##type();                                                      \
  thumb_rn_op_##rn_type(_rn);                                                 \
  arm_data_proc_unary_##name(_rd, generate_store_reg);                        \
}

#define thumb_data_proc_mov(type, rn_type, _rd, _rn)                          \
{                                                                             \
  thumb_decode_##type();                                                      \
  thumb_rn_op_##rn_type(_rn);                                                 \
  generate_store_reg(a0, _rd);                                                \
}

#define thumb_data_proc_hi(name)                                              \
{                                                                             \
  thumb_decode_hireg_op();                                                    \
  generate_load_reg_pc(a0, rs, 4);                                            \
  generate_load_reg_pc(a1, rd, 4);                                            \
  arm_data_proc_##name(rd, generate_store_reg_pc_thumb);                      \
}

#define thumb_data_proc_test_hi(name)                                         \
{                                                                             \
  thumb_decode_hireg_op();                                                    \
  generate_load_reg_pc(a0, rs, 4);                                            \
  generate_load_reg_pc(a1, rd, 4);                                            \
  arm_data_proc_test_##name();                                                \
}

#define thumb_data_proc_unary_hi(name)                                        \
{                                                                             \
  thumb_decode_hireg_op();                                                    \
  generate_load_reg_pc(a0, rn, 4);                                            \
  arm_data_proc_unary_##name(rd, generate_store_reg_pc_thumb);                \
}

#define thumb_data_proc_mov_hi()                                              \
{                                                                             \
  thumb_decode_hireg_op();                                                    \
  generate_load_reg_pc(a0, rs, 4);                                            \
  generate_store_reg_pc_thumb(a0, rd);                                        \
}

#define thumb_load_pc(_rd)                                                    \
{                                                                             \
  thumb_decode_imm();                                                         \
  generate_load_pc(a0, (((pc & ~2) + 4) + (imm * 4)));                       \
  generate_store_reg(a0, _rd);                                                \
}

#define thumb_load_sp(_rd)                                                    \
{                                                                             \
  thumb_decode_imm();                                                         \
  generate_load_reg(a0, 13);                                                  \
  generate_add_imm(a0, (imm * 4));                                            \
  generate_store_reg(a0, _rd);                                                \
}

#define thumb_adjust_sp_up()    generate_add_imm(a0, imm * 4)
#define thumb_adjust_sp_down()  generate_sub_imm(a0, imm * 4)

#define thumb_adjust_sp(direction)                                            \
{                                                                             \
  thumb_decode_add_sp();                                                      \
  generate_load_reg(a0, REG_SP);                                              \
  thumb_adjust_sp_##direction();                                              \
  generate_store_reg(a0, REG_SP);                                             \
}

/* Thumb shifts — use inline carry for immediate amounts */
#define thumb_lsl_imm_op()                                                    \
  if (imm) { generate_inline_lsl_carry(a0, imm); }                            \
  else { generate_or(a0, a0); }                                               \
  sh4_res_reg = a0;                                                           \
  update_logical_flags()

#define thumb_lsr_imm_op()                                                    \
  if (imm) { generate_inline_lsr_carry(a0, imm); }                            \
  else { generate_shift_right(a0, 31); generate_update_flag(nz, REG_C_FLAG); generate_xor(a0, a0); } \
  sh4_res_reg = a0;                                                           \
  update_logical_flags()

#define thumb_asr_imm_op()                                                    \
  if (imm) { generate_inline_asr_carry(a0, imm); }                            \
  else { generate_shift_right_arithmetic(a0, 31); generate_update_flag(s, REG_C_FLAG); } \
  sh4_res_reg = a0;                                                           \
  update_logical_flags()

#define thumb_ror_imm_op()                                                    \
  if (imm) { generate_inline_ror_carry(a0, imm); }                            \
  else { generate_rrx_flags(a0); }                                            \
  sh4_res_reg = a0;                                                           \
  update_logical_flags()

#define thumb_shift_operation_imm(op_type)   thumb_##op_type##_imm_op()
#define thumb_shift_operation_reg(op_type)                                    \
  generate_##op_type##_flags_reg(a0);                                         \
  generate_or(a0, a0);                                                        \
  sh4_res_reg = a0;                                                           \
  update_logical_flags()

#define thumb_shift(decode_type, op_type, value_type)                         \
{                                                                             \
  thumb_decode_##decode_type();                                               \
  generate_shift_load_operands_##value_type();                                \
  thumb_shift_operation_##value_type(op_type);                                \
  generate_store_reg(a0, rd);                                                 \
}

/* Thumb memory */
#define thumb_load_pc_pool_const(reg_rd, value)                               \
  generate_store_reg_i32(value, reg_rd)

#define thumb_access_memory_load(mem_type, reg_rd)                            \
  cycle_count += 2;                                                           \
  generate_load_pc(a1, pc);                                                   \
  generate_inline_load_##mem_type();                                          \
  generate_store_reg(rv, reg_rd)

#define thumb_access_memory_store(mem_type, reg_rd)                           \
  cycle_count++;                                                              \
  generate_load_reg(a1, reg_rd);                                              \
  generate_store_reg_i32(pc + 2, REG_PC);                                     \
  generate_inline_store_##mem_type();                                         \
  generate_store_alert_check()

#define thumb_access_memory_generate_address_pc_relative(off, _rb, _ro)       \
  generate_load_pc(a0, (off))

#define thumb_access_memory_generate_address_reg_imm_sp(off, _rb, _ro)        \
  generate_load_reg(a0, _rb);                                                 \
  generate_add_imm(a0, (off * 4))

#define thumb_access_memory_generate_address_reg_imm(off, _rb, _ro)           \
  generate_load_reg(a0, _rb);                                                 \
  generate_add_imm(a0, (off))

#define thumb_access_memory_generate_address_reg_reg(off, _rb, _ro)           \
  generate_load_reg(a0, _rb);                                                 \
  generate_load_reg(a1, _ro);                                                 \
  generate_add(a0, a1)

#define thumb_access_memory(access_type, op_type, _rd, _rb, _ro,              \
 address_type, offset, mem_type)                                              \
{                                                                             \
  thumb_decode_##op_type();                                                   \
  thumb_access_memory_generate_address_##address_type(offset, _rb, _ro);      \
  thumb_access_memory_##access_type(mem_type, _rd);                           \
}

/* Thumb block memory */
#define thumb_block_address_preadjust_up()                                    \
  generate_add_imm(a0, (bit_count[reg_list] * 4))
#define thumb_block_address_preadjust_down()                                  \
  generate_sub_imm(a0, (bit_count[reg_list] * 4))
#define thumb_block_address_preadjust_push_lr()                               \
  generate_sub_imm(a0, ((bit_count[reg_list] + 1) * 4))
#define thumb_block_address_preadjust_no()

#define thumb_block_address_postadjust_no(base_reg)                           \
  generate_store_reg(a0, base_reg)
#define thumb_block_address_postadjust_up(base_reg)                           \
  generate_add_imm(a0, (bit_count[reg_list] * 4));                            \
  generate_store_reg(a0, base_reg)
#define thumb_block_address_postadjust_down(base_reg)                         \
  generate_sub_imm(a0, (bit_count[reg_list] * 4));                            \
  generate_store_reg(a0, base_reg)
#define thumb_block_address_postadjust_pop_pc(base_reg)                       \
  generate_add_imm(a0, ((bit_count[reg_list] + 1) * 4));                      \
  generate_store_reg(a0, base_reg)
#define thumb_block_address_postadjust_push_lr(base_reg)                      \
  generate_store_reg(a0, base_reg)

#define thumb_block_memory_extra_no()
#define thumb_block_memory_extra_up()
#define thumb_block_memory_extra_down()

#define thumb_block_memory_extra_pop_pc()                                     \
  generate_load_reg(a0, REG_SAVE3);                                           \
  generate_add_imm(a0, (bit_count[reg_list] * 4));                            \
  generate_load_pc(a1, pc);                                                   \
  generate_function_call(execute_load_u32);                                   \
  generate_store_reg(rv, REG_PC);                                             \
  generate_indirect_branch_exit_with_cycle()

#define thumb_block_memory_extra_push_lr(base_reg)                            \
  generate_load_reg(a0, REG_SAVE3);                                           \
  generate_add_imm(a0, (bit_count[reg_list] * 4));                            \
  generate_load_reg(a1, REG_LR);                                              \
  generate_function_call(execute_store_aligned_u32)

#define thumb_block_memory_load()                                             \
  generate_load_pc(a1, pc);                                                   \
  generate_function_call(execute_load_u32);                                   \
  generate_store_reg(rv, i)

#define thumb_block_memory_store()                                            \
  generate_load_reg(a1, i);                                                   \
  generate_function_call(execute_store_aligned_u32)

#define thumb_block_memory_final_load()    thumb_block_memory_load()
#define thumb_block_memory_final_store()                                      \
  generate_load_reg(a1, i);                                                   \
  generate_store_reg_i32(pc + 2, REG_PC);                                     \
  generate_function_call(execute_store_u32);                                  \
  generate_store_alert_check()

#define thumb_block_memory_final_no(access_type)                              \
  thumb_block_memory_final_##access_type()
#define thumb_block_memory_final_up(access_type)                              \
  thumb_block_memory_final_##access_type()
#define thumb_block_memory_final_down(access_type)                            \
  thumb_block_memory_final_##access_type()
#define thumb_block_memory_final_push_lr(access_type)                         \
  thumb_block_memory_##access_type()
#define thumb_block_memory_final_pop_pc(access_type)                          \
  thumb_block_memory_##access_type()

#define thumb_block_memory(access_type, pre_op, post_op, base_reg)            \
{                                                                             \
  thumb_decode_rlist();                                                       \
  u32 i;                                                                      \
  u32 offset = 0;                                                             \
  generate_load_reg(a0, base_reg);                                            \
  generate_and_imm(a0, ~0x03);                                                \
  thumb_block_address_preadjust_##pre_op();                                   \
  generate_store_reg(a0, REG_SAVE3);                                          \
  thumb_block_address_postadjust_##post_op(base_reg);                         \
  for(i = 0; i < 8; i++) {                                                    \
    if((reg_list >> i) & 0x01) {                                              \
      cycle_count++;                                                          \
      generate_load_reg(a0, REG_SAVE3);                                       \
      generate_add_imm(a0, offset);                                           \
      if(reg_list & ~((2 << i) - 1)) {                                        \
        thumb_block_memory_##access_type();                                   \
        offset += 4;                                                          \
      } else {                                                                \
        thumb_block_memory_final_##post_op(access_type);                      \
      }                                                                       \
    }                                                                         \
  }                                                                           \
  thumb_block_memory_extra_##post_op();                                       \
}

/* Thumb branches */
#define thumb_conditional_branch(condition)                                   \
{                                                                             \
  generate_cycle_update_and_check();                                          \
  generate_condition_##condition(a0);                                         \
  generate_branch_no_cycle_update(                                            \
   block_exits[block_exit_position].branch_source,                            \
   block_exits[block_exit_position].branch_target);                           \
  generate_branch_patch_conditional(backpatch_address, translation_ptr);      \
  block_exit_position++;                                                      \
}

#define thumb_b()                                                             \
  generate_branch_cycle_update(                                               \
   block_exits[block_exit_position].branch_source,                            \
   block_exits[block_exit_position].branch_target);                           \
  block_exit_position++

#define thumb_bl()                                                            \
  generate_load_pc(a0, ((pc + 2) | 0x01));                                    \
  generate_store_reg(a0, REG_LR);                                             \
  generate_branch_cycle_update(                                               \
   block_exits[block_exit_position].branch_source,                            \
   block_exits[block_exit_position].branch_target);                           \
  block_exit_position++

#define thumb_blh()                                                           \
{                                                                             \
  thumb_decode_branch();                                                      \
  generate_load_pc(a0, ((pc + 2) | 0x01));                                    \
  generate_load_reg(a1, REG_LR);                                              \
  generate_store_reg(a0, REG_LR);                                             \
  generate_mov(a0, a1);                                                       \
  generate_add_imm(a0, (offset * 2));                                         \
  generate_indirect_branch_cycle_update(thumb);                               \
}

#define thumb_bx()                                                            \
{                                                                             \
  thumb_decode_hireg_op();                                                    \
  generate_load_reg_pc(a0, rs, 4);                                            \
  generate_indirect_branch_cycle_update(dual);                                \
}

#define thumb_process_cheats()   generate_function_call(process_cheats);

#define thumb_swi()                                                           \
  collapse_flags(a0, a1);                                                     \
  generate_load_pc(arg0, (pc + 2));                                           \
  generate_function_call(execute_swi);                                        \
  generate_branch_cycle_update(                                               \
   block_exits[block_exit_position].branch_source,                            \
   block_exits[block_exit_position].branch_target);                           \
  block_exit_position++

/* ==================================================================== */
/* Init and entry point                                                  */
/* ==================================================================== */

/* BIOS interpreter removed — all BIOS code runs through the dynarec now.
 * See bugs 15-21 in dreamcast.md for the dynarec fixes that made this possible. */

void init_emitter(bool must_swap) {
  rom_cache_watermark = INITIAL_ROM_WATERMARK;
  init_bios_hooks();
}

u32 function_cc execute_arm_translate_internal(u32 cycles, void *regptr);

u32 execute_arm_translate(u32 cycles) {
  return execute_arm_translate_internal(cycles, &reg[0]);
}

#endif /* SH4_EMIT_H */
