/* Ground truth from interpreter run of stripes.gba (boot_game mode)
 * Compare dynarec reg[] at key PCs to catch the first divergence.
 *
 * Usage: call sh4_validate_regs(pc) from block_lookup_address_arm
 * when SH4_DYNAREC_DEBUG is enabled.
 */

#ifndef SH4_VALIDATE_H
#define SH4_VALIDATE_H

#ifdef SH4_DYNAREC_DEBUG

typedef struct {
  u32 pc;
  u32 r0, r1, r2, r3, r4;
  u32 sp, lr, cpsr;
  const char *desc;
} validate_point_t;

/* Key checkpoints from interpreter trace (pre-instruction state) */
static const validate_point_t validate_points[] = {
  /* PC          r0          r1          r2          r3    r4    sp          lr    cpsr        desc */
  {0x08000000, 0x00000000, 0x00000000, 0x00000000, 0, 0, 0x03007f00, 0, 0x0000001f, "ROM entry"},
  {0x080000c0, 0x00000000, 0x00000000, 0x00000000, 0, 0, 0x03007f00, 0, 0x0000001f, "main start"},
  {0x080000c8, 0x00000100, 0x04000000, 0x00000000, 0, 0, 0x03007f00, 0, 0x0000001f, "after DISPCNT mov"},
  {0x080000d8, 0x00000104, 0x04000000, 0x00000000, 0, 0, 0x03007f00, 0, 0x0000001f, "before color1 setup"},
  {0x08000110, 0x11111111, 0x06004000, 0x00000020, 0, 0, 0x03007f00, 0, 0x0000001f, "before tile loop"},
  {0x08000120, 0x11111111, 0x06004000, 0x00000000, 0, 0, 0x03007f00, 0, 0x6000001f, "after tile loop (Z+C set)"},
  {0x0800012c, 0x00000001, 0x06000800, 0x00000800, 0, 0, 0x03007f00, 0, 0x6000001f, "before map loop"},
  {0x08000140, 0x00000001, 0x06000ffc, 0x00000000, 0, 0, 0x03007f00, 0, 0x6000001f, "idle loop"},
  {0, 0, 0, 0, 0, 0, 0, 0, 0, NULL}  /* sentinel */
};

static int sh4_validate_done = 0;

static inline void sh4_validate_regs(u32 pc)
{
  if (sh4_validate_done) return;

  for (int i = 0; validate_points[i].desc; i++) {
    if (pc != validate_points[i].pc) continue;

    const validate_point_t *v = &validate_points[i];
    int ok = 1;
    #define CHK(name, idx) \
      if (reg[idx] != v->name) { \
        printf("[VALIDATE FAIL] PC=%08x %s: " #name " got %08x expected %08x\n", \
               pc, v->desc, reg[idx], v->name); \
        ok = 0; \
      }

    CHK(r0, 0)
    CHK(r1, 1)
    CHK(r2, 2)
    CHK(sp, 13)
    CHK(cpsr, 16)
    #undef CHK

    if (ok) {
      printf("[VALIDATE OK] PC=%08x %s\n", pc, v->desc);
    } else {
      printf("[VALIDATE] Stopping validation at first failure\n");
      sh4_validate_done = 1;
    }
    break;
  }
}

#else
#define sh4_validate_regs(pc)
#endif

#endif /* SH4_VALIDATE_H */
