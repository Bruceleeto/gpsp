/* gpSP SH4 dynarec - debug/validation utilities
 *
 * Include this from sh4_emit.h to get:
 *   - Block translation tracing (what ARM PC -> what SH4 offset)
 *   - Generated code hex dumper
 *   - Runtime execution tracing (optional, per-block)
 *   - Instruction count stats
 *
 * Enable with: #define SH4_DYNAREC_DEBUG 1
 * Verbose hex dump: #define SH4_DYNAREC_HEXDUMP 1
 */

#ifndef SH4_DEBUG_H
#define SH4_DEBUG_H

#include <stdio.h>

#ifdef SH4_DYNAREC_DEBUG

/* ---- Translation-time tracing ---- */

/* Call at the start of translate_block_arm/thumb to log what's being translated */
#define debug_block_start(type_str, pc, block_start, block_end)               \
  printf("[DYNAREC] translate_%s: ARM PC=0x%08x block=[0x%08x..0x%08x)\n",    \
         type_str, (unsigned)(pc),                                            \
         (unsigned)(block_start), (unsigned)(block_end))

/* Call after translation to show where in the cache it landed */
#define debug_block_done(type_str, cache_start, cache_end, arm_pc)            \
  printf("[DYNAREC] translated %s @ ARM 0x%08x -> SH4 cache %p..%p (%u bytes)\n", \
         type_str, (unsigned)(arm_pc),                                        \
         (void*)(cache_start), (void*)(cache_end),                            \
         (unsigned)((u8*)(cache_end) - (u8*)(cache_start)))

/* Per-ARM-instruction trace during translation */
#define debug_arm_insn(pc, opcode, cond)                                      \
  printf("  [ARM] 0x%08x: opcode=0x%08x cond=%x\n",                          \
         (unsigned)(pc), (unsigned)(opcode), (unsigned)(cond))

#define debug_thumb_insn(pc, opcode)                                          \
  printf("  [THM] 0x%08x: opcode=0x%04x\n",                                  \
         (unsigned)(pc), (unsigned)(opcode))

/* ---- Hex dump of generated SH4 code ---- */

#ifdef SH4_DYNAREC_HEXDUMP

static inline void debug_hexdump_sh4(const u8 *start, const u8 *end)
{
  const u16 *p = (const u16 *)start;
  const u16 *pe = (const u16 *)end;
  int col = 0;
  printf("  [SH4 CODE] %u bytes:\n  ", (unsigned)(end - start));
  while (p < pe) {
    u16 insn = *p++;
    /* Mark NOPs distinctly so you can spot the stubs */
    if (insn == 0x0009)
      printf("NOP  ");
    else
      printf("%04x ", insn);
    col++;
    if (col >= 8) { printf("\n  "); col = 0; }
  }
  if (col) printf("\n");
}

#define debug_hexdump_block(start, end)  debug_hexdump_sh4(start, end)

#else
#define debug_hexdump_block(start, end)
#endif /* SH4_DYNAREC_HEXDUMP */

/* ---- Runtime execution trace ---- */
/* Emits a call to this function at the start of each generated block.
 * You can later gate this behind a flag or instruction count to avoid
 * drowning in output. */

static u32 sh4_dbg_block_count = 0;
static u32 sh4_dbg_insn_count = 0;

static inline void sh4_trace_block_entry(u32 arm_pc)
{
  sh4_dbg_block_count++;
  /* Print first N block entries to avoid flooding */
  if (sh4_dbg_block_count <= 50) {
    printf("[EXEC] block #%u: ARM PC=0x%08x\n",
           sh4_dbg_block_count, arm_pc);
  } else if (sh4_dbg_block_count == 51) {
    printf("[EXEC] ... (suppressing further trace, %u blocks so far)\n",
           sh4_dbg_block_count);
  }
}

/* Call this from C periodically to see stats */
static inline void sh4_debug_stats(void)
{
  printf("[DYNAREC STATS] blocks executed: %u\n", sh4_dbg_block_count);
}

/* ---- Lookup tracing ---- */

#define debug_lookup(type_str, pc, result)                                    \
  printf("[LOOKUP] %s PC=0x%08x -> %p%s\n",                                  \
         type_str, (unsigned)(pc), (void*)(result),                           \
         (result) ? "" : " *** NULL ***")

/* ---- NULL guard for block lookup ---- */
/* Replaces the jmp @r0 when r0 might be NULL.
 * In the C lookup functions, trap NULL before returning. */
#define debug_null_guard(ptr, pc)                                             \
  if (!(ptr)) {                                                               \
    printf("[FATAL] block_lookup returned NULL for PC=0x%08x\n",              \
           (unsigned)(pc));                                                    \
    printf("[FATAL] This means translation failed 4 times. Check:\n");        \
    printf("  - Is the PC in a valid memory region?\n");                       \
    printf("  - Is the translation cache full?\n");                            \
    printf("  - Did scan_block find a valid block?\n");                        \
    fflush(stdout);                                                           \
  }

#else /* !SH4_DYNAREC_DEBUG */

#define debug_block_start(type_str, pc, block_start, block_end)
#define debug_block_done(type_str, cache_start, cache_end, arm_pc)
#define debug_arm_insn(pc, opcode, cond)
#define debug_thumb_insn(pc, opcode)
#define debug_hexdump_block(start, end)
#define debug_lookup(type_str, pc, result)
#define debug_null_guard(ptr, pc)

#endif /* SH4_DYNAREC_DEBUG */

#endif /* SH4_DEBUG_H */
