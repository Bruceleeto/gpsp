/* gpSP standalone frontend
 *
 * Dreamcast: PVR rendering, maple input, KOS timer
 * Linux:     SDL 1.2 video/audio/input
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DREAMCAST
#include <kos.h>
#include <dc/pvr.h>
#include <dc/sq.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#else
#include <SDL/SDL.h>
#endif

#include "common.h"
#include "gba_memory.h"
#include "gba_cc_lut.h"
#include "memmap.h"

/* Usually ~59.73 Hz */
#define GBA_FPS (((float)GBC_BASE_RATE) / (308 * 228 * 4))

/* ---- Globals the core expects ---- */
u32 skip_next_frame = 0;
u32 num_skipped_frames = 0;
#ifdef HAVE_DYNAREC
int dynarec_enable = 1;
#else
int dynarec_enable = 0;
#endif
boot_mode selected_boot_mode = boot_game;
int sprite_limit = 1;

u32 idle_loop_target_pc = 0xFFFFFFFF;
u32 translation_gate_target_pc[MAX_TRANSLATION_GATES];
u32 translation_gate_targets = 0;

u32 netplay_num_clients = 0;
u32 netplay_client_id = 0;

/* ---- Stubs for features not used in standalone mode ---- */
void netpacket_send(uint16_t client_id, const void *buf, size_t len)
{
   (void)client_id; (void)buf; (void)len;
}

void netpacket_poll_receive(void) {}

void set_fastforward_override(bool fastforward)
{
   (void)fastforward;
}

static int running = 1;

/* ========================================================================= */
#ifdef DREAMCAST
/* Dreamcast: PVR + Maple                                                    */
/* ========================================================================= */

#define PVR_TEX_W 256
#define PVR_TEX_H 256

static pvr_ptr_t pvr_txr;
static void *pvr_txr_sq;
static u32 dc_key_state = 0;

static frameskip_type current_frameskip_type = no_frameskip;
static u32 frameskip_value = 1;
static u32 frameskip_counter = 0;

/* ---- DC audio via snd_stream ---- */
#include <dc/sound/sound.h>
#include <dc/sound/stream.h>

static snd_stream_hnd_t dc_snd_hnd = SND_STREAM_INVALID;
static s16 dc_snd_buf[16384] __attribute__((aligned(32)));

static void *dc_snd_callback(snd_stream_hnd_t hnd, int bytes_req, int *bytes_recv)
{
   (void)hnd;
   /* bytes_req is in bytes. stereo s16 = 4 bytes per frame */
   u32 frames = bytes_req / 4;
   if (frames > sizeof(dc_snd_buf) / 4)
      frames = sizeof(dc_snd_buf) / 4;

   u32 produced = sound_read_samples(dc_snd_buf, frames);

   if (produced < frames)
      memset(dc_snd_buf + produced * 2, 0, (frames - produced) * 2 * sizeof(s16));

   *bytes_recv = frames * 4;
   return dc_snd_buf;
}

static int16_t dc_input_state_cb(unsigned port, unsigned device,
                                  unsigned index, unsigned id)
{
   (void)device; (void)index;
   if (port != 0)
      return 0;

   switch (id)
   {
      case BUTTON_ID_A:      return (dc_key_state & BUTTON_A)      ? 1 : 0;
      case BUTTON_ID_B:      return (dc_key_state & BUTTON_B)      ? 1 : 0;
      case BUTTON_ID_SELECT: return (dc_key_state & BUTTON_SELECT) ? 1 : 0;
      case BUTTON_ID_START:  return (dc_key_state & BUTTON_START)  ? 1 : 0;
      case BUTTON_ID_RIGHT:  return (dc_key_state & BUTTON_RIGHT)  ? 1 : 0;
      case BUTTON_ID_LEFT:   return (dc_key_state & BUTTON_LEFT)   ? 1 : 0;
      case BUTTON_ID_UP:     return (dc_key_state & BUTTON_UP)     ? 1 : 0;
      case BUTTON_ID_DOWN:   return (dc_key_state & BUTTON_DOWN)   ? 1 : 0;
      case BUTTON_ID_R:      return (dc_key_state & BUTTON_R)      ? 1 : 0;
      case BUTTON_ID_L:      return (dc_key_state & BUTTON_L)      ? 1 : 0;
      default: return 0;
   }
}

static void dc_poll_input(void)
{
   maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
   if (!cont) return;
   cont_state_t *state = (cont_state_t *)maple_dev_status(cont);
   if (!state) return;

   dc_key_state = 0;
   if (state->buttons & CONT_A)          dc_key_state |= BUTTON_A;
   if (state->buttons & CONT_B)          dc_key_state |= BUTTON_B;
   if (state->buttons & CONT_START)      dc_key_state |= BUTTON_START;
   if (state->buttons & CONT_Y)          dc_key_state |= BUTTON_SELECT;
   if (state->buttons & CONT_DPAD_UP)    dc_key_state |= BUTTON_UP;
   if (state->buttons & CONT_DPAD_DOWN)  dc_key_state |= BUTTON_DOWN;
   if (state->buttons & CONT_DPAD_LEFT)  dc_key_state |= BUTTON_LEFT;
   if (state->buttons & CONT_DPAD_RIGHT) dc_key_state |= BUTTON_RIGHT;
   if (state->ltrig > 16)                dc_key_state |= BUTTON_L;
   if (state->rtrig > 16)                dc_key_state |= BUTTON_R;
}

/* Pre-computed PVR header — built once at init, reused every frame */
static pvr_poly_hdr_t dc_poly_hdr;
static float dc_u_max, dc_v_max;

static void dc_init_pvr_state(void)
{
   pvr_poly_cxt_t cxt;
   pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY,
      PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED,
      PVR_TEX_W, PVR_TEX_H, pvr_txr, PVR_FILTER_NONE);
   pvr_poly_compile(&dc_poly_hdr, &cxt);
   dc_u_max = (float)GBA_SCREEN_WIDTH / PVR_TEX_W;
   dc_v_max = (float)GBA_SCREEN_HEIGHT / PVR_TEX_H;
}

static void dc_submit_frame(void)
{
   /* Upload framebuffer to VRAM */
   sq_cpy(pvr_txr_sq, gba_screen_pixels,
          GBA_SCREEN_PITCH * GBA_SCREEN_HEIGHT * sizeof(u16));

   /* Wait for PVR to finish previous frame, then submit new one */
   pvr_wait_ready();
   pvr_scene_begin();
   pvr_list_begin(PVR_LIST_OP_POLY);

   pvr_prim(&dc_poly_hdr, sizeof(dc_poly_hdr));

   pvr_vertex_t vert;
   vert.argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
   vert.oargb = 0;
   vert.flags = PVR_CMD_VERTEX;

   vert.x = 0.0f;   vert.y = 0.0f;   vert.z = 1.0f;
   vert.u = 0.0f;   vert.v = 0.0f;
   pvr_prim(&vert, sizeof(vert));

   vert.x = 640.0f; vert.y = 0.0f;
   vert.u = dc_u_max; vert.v = 0.0f;
   pvr_prim(&vert, sizeof(vert));

   vert.x = 0.0f;   vert.y = 480.0f;
   vert.u = 0.0f;   vert.v = dc_v_max;
   pvr_prim(&vert, sizeof(vert));

   vert.flags = PVR_CMD_VERTEX_EOL;
   vert.x = 640.0f; vert.y = 480.0f;
   vert.u = dc_u_max; vert.v = dc_v_max;
   pvr_prim(&vert, sizeof(vert));

   pvr_list_finish();
   pvr_scene_finish();  /* Non-blocking — PVR renders while we emulate next frame */
}

/* ========================================================================= */
#else
/* Linux: SDL 1.2                                                            */
/* ========================================================================= */

#define SCREEN_WIDTH  640
#define SCREEN_HEIGHT 480

static SDL_Surface *screen = NULL;
static u32 sdl_key_state = 0;

/* ---- Scale GBA framebuffer to screen ---- */
static void scale_screen(u16 *src, SDL_Surface *dst)
{
   /* Nearest-neighbor scale 240x160 -> 640x480 (2.66x x 3x) */
   u32 x_step = (GBA_SCREEN_WIDTH << 16) / SCREEN_WIDTH;
   u32 y_step = (GBA_SCREEN_HEIGHT << 16) / SCREEN_HEIGHT;

   SDL_LockSurface(dst);
   u16 *dst_pixels = (u16 *)dst->pixels;
   u32 dst_pitch = dst->pitch / 2;  /* pitch in pixels */

   u32 sy = 0;
   for (int y = 0; y < SCREEN_HEIGHT; y++)
   {
      u16 *src_row = &src[(sy >> 16) * GBA_SCREEN_PITCH];
      u16 *dst_row = &dst_pixels[y * dst_pitch];
      u32 sx = 0;
      for (int x = 0; x < SCREEN_WIDTH; x++)
      {
         dst_row[x] = src_row[sx >> 16];
         sx += x_step;
      }
      sy += y_step;
   }
   SDL_UnlockSurface(dst);
}

/* ---- SDL input callback for input.c's update_input() ---- */
static int16_t sdl_input_state_cb(unsigned port, unsigned device,
                                  unsigned index, unsigned id)
{
   (void)device; (void)index;
   if (port != 0)
      return 0;

   switch (id)
   {
      case BUTTON_ID_A:      return (sdl_key_state & BUTTON_A)      ? 1 : 0;
      case BUTTON_ID_B:      return (sdl_key_state & BUTTON_B)      ? 1 : 0;
      case BUTTON_ID_SELECT: return (sdl_key_state & BUTTON_SELECT) ? 1 : 0;
      case BUTTON_ID_START:  return (sdl_key_state & BUTTON_START)  ? 1 : 0;
      case BUTTON_ID_RIGHT:  return (sdl_key_state & BUTTON_RIGHT)  ? 1 : 0;
      case BUTTON_ID_LEFT:   return (sdl_key_state & BUTTON_LEFT)   ? 1 : 0;
      case BUTTON_ID_UP:     return (sdl_key_state & BUTTON_UP)     ? 1 : 0;
      case BUTTON_ID_DOWN:   return (sdl_key_state & BUTTON_DOWN)   ? 1 : 0;
      case BUTTON_ID_R:      return (sdl_key_state & BUTTON_R)      ? 1 : 0;
      case BUTTON_ID_L:      return (sdl_key_state & BUTTON_L)      ? 1 : 0;
      default: return 0;
   }
}

/* ---- Audio callback ---- */
static void sdl_audio_callback(void *userdata, Uint8 *stream, int len)
{
   (void)userdata;
   u32 frames = len / (2 * sizeof(s16));   /* stereo s16 */
   s16 *buf = (s16 *)stream;
   u32 produced = sound_read_samples(buf, frames);

   if (produced < frames)
      memset(buf + produced * 2, 0, (frames - produced) * 2 * sizeof(s16));
}

/* ---- Input polling ---- */
static void sdl_poll_input(void)
{
   SDL_Event ev;
   while (SDL_PollEvent(&ev))
   {
      switch (ev.type)
      {
         case SDL_QUIT:
            running = 0;
            break;

         case SDL_KEYDOWN:
         case SDL_KEYUP:
         {
            u32 btn = 0;
            switch (ev.key.keysym.sym)
            {
               case SDLK_x:      btn = BUTTON_A;      break;
               case SDLK_z:      btn = BUTTON_B;      break;
               case SDLK_RETURN: btn = BUTTON_START;  break;
               case SDLK_RSHIFT: btn = BUTTON_SELECT; break;
               case SDLK_UP:     btn = BUTTON_UP;     break;
               case SDLK_DOWN:   btn = BUTTON_DOWN;   break;
               case SDLK_LEFT:   btn = BUTTON_LEFT;   break;
               case SDLK_RIGHT:  btn = BUTTON_RIGHT;  break;
               case SDLK_a:      btn = BUTTON_L;      break;
               case SDLK_s:      btn = BUTTON_R;      break;
               case SDLK_ESCAPE:
                  if (ev.type == SDL_KEYDOWN) running = 0;
                  break;
               default: break;
            }
            if (btn)
            {
               if (ev.type == SDL_KEYDOWN)
                  sdl_key_state |= btn;
               else
                  sdl_key_state &= ~btn;
            }
            break;
         }
      }
   }
}

#endif /* DREAMCAST */

/* ========================================================================= */
/* Main                                                                      */
/* ========================================================================= */

int main(int argc, char *argv[])
{
   const char *rom_path  = (argc > 1) ? argv[1] : "pc/assets/test.bin";
   const char *bios_path = (argc > 2) ? argv[2] : NULL;

#ifdef DREAMCAST
   pvr_init_defaults();
   pvr_txr = pvr_mem_malloc(PVR_TEX_W * PVR_TEX_H * sizeof(u16));
   pvr_txr_sq = (void *)(((uintptr_t)pvr_txr & 0xFFFFFF) | PVR_TA_TEX_MEM);
   dc_init_pvr_state();

   /* ---- DC audio ---- */
   snd_stream_init();
   dc_snd_hnd = snd_stream_alloc(dc_snd_callback, SND_STREAM_BUFFER_MAX);
#else
   /* ---- SDL init ---- */
   if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
   {
      fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
      return 1;
   }

   SDL_WM_SetCaption("gpSP", NULL);
   SDL_ShowCursor(0);

   screen = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 16,
                             SDL_HWSURFACE | SDL_DOUBLEBUF);
   if (!screen)
   {
      fprintf(stderr, "SDL_SetVideoMode: %s\n", SDL_GetError());
      SDL_Quit();
      return 1;
   }

   /* ---- Audio ---- */
   SDL_AudioSpec want;
   memset(&want, 0, sizeof(want));
   want.freq     = GBA_SOUND_FREQUENCY;
   want.format   = AUDIO_S16SYS;
   want.channels = 2;
   want.samples  = 1024;
   want.callback = sdl_audio_callback;

   if (SDL_OpenAudio(&want, NULL) < 0)
      fprintf(stderr, "SDL audio failed: %s\n", SDL_GetError());
#endif

   /* ---- Emulator init ---- */
#if defined(HAVE_DYNAREC) && defined(MMAP_JIT_CACHE)
   rom_translation_cache = map_jit_block(ROM_TRANSLATION_CACHE_SIZE +
                                         RAM_TRANSLATION_CACHE_SIZE);
   ram_translation_cache = &rom_translation_cache[ROM_TRANSLATION_CACHE_SIZE];
#endif

   init_gamepak_buffer();
   init_sound();

   gba_screen_pixels = (u16 *)malloc(GBA_SCREEN_BUFFER_SIZE);
   memset(gba_screen_pixels, 0, GBA_SCREEN_BUFFER_SIZE);

   /* Load BIOS */
   int bios_loaded = 0;
   if (bios_path)
   {
      char bios_buf[512];
      strncpy(bios_buf, bios_path, sizeof(bios_buf) - 1);
      bios_buf[sizeof(bios_buf) - 1] = '\0';

      if (load_bios(bios_buf) == 0 && bios_rom[0] == 0x18)
      {
         bios_loaded = 1;
         printf("Loaded BIOS: %s\n", bios_path);
      }
      else
         fprintf(stderr, "Bad BIOS file, using built-in\n");
   }

   if (!bios_loaded)
   {
      memcpy(bios_rom, open_gba_bios_rom, sizeof(bios_rom));
      printf("Using built-in BIOS\n");
   }

   /* Extract directory from ROM path for saves */
   {
      const char *sep = strrchr(rom_path, PATH_SEPARATOR_CHAR);
      if (sep)
      {
         size_t len = sep - rom_path;
         if (len >= sizeof(main_path)) len = sizeof(main_path) - 1;
         memcpy(main_path, rom_path, len);
         main_path[len] = '\0';
      }
      else
         strcpy(main_path, ".");
   }

   /* Load ROM */
   memset(gamepak_backup, 0xFF, sizeof(gamepak_backup));
   if (load_gamepak(rom_path, FEAT_AUTODETECT, FEAT_AUTODETECT,
                    SERIAL_MODE_AUTO) != 0)
   {
      fprintf(stderr, "Failed to load ROM: %s\n", rom_path);
#ifndef DREAMCAST
      SDL_Quit();
#endif
      return 1;
   }
   printf("Loaded ROM: %s\n", rom_path);

   /* Wire up input */
#ifdef DREAMCAST
   set_input_state(dc_input_state_cb);
#else
   set_input_state(sdl_input_state_cb);
#endif

   /* Reset GBA */
   reset_gba();

#ifdef DREAMCAST
   if (dc_snd_hnd != SND_STREAM_INVALID)
      snd_stream_start(dc_snd_hnd, GBA_SOUND_FREQUENCY, 1);
#else
   /* Start audio playback */
   SDL_PauseAudio(0);
#endif

   /* ---- Main loop ---- */
#ifdef DREAMCAST
   {
      u32 frame_ms      = (u32)(1000.0f / GBA_FPS);
      u32 virtual_ticks = (u32)timer_ms_gettime64() + frame_ms;
      u32 fps_counter   = 0;
      u32 skip_counter  = 0;
      u32 fps_last      = (u32)timer_ms_gettime64();

      while (running)
      {
         dc_poll_input();
         update_input();
         rumble_frame_reset();

#ifdef HAVE_DYNAREC
         if (dynarec_enable)
            execute_arm_translate(execute_cycles);
         else
#endif
         {
            clear_gamepak_stickybits();
            execute_arm(execute_cycles);
         }

         if (!skip_next_frame)
            dc_submit_frame();

         if (dc_snd_hnd != SND_STREAM_INVALID)
            snd_stream_poll(dc_snd_hnd);

         virtual_ticks += frame_ms;
         u32 now = (u32)timer_ms_gettime64();
         if (current_frameskip_type == auto_frameskip)
         {
            if (now < virtual_ticks || num_skipped_frames >= frameskip_value)
            {
               skip_next_frame = 0;
               num_skipped_frames = 0;
            }
            else
            {
               skip_next_frame = 1;
               num_skipped_frames++;
            }
         }
         else if (current_frameskip_type == fixed_interval_frameskip)
         {
            frameskip_counter = (frameskip_counter + 1) % (frameskip_value + 1);
            skip_next_frame = (frameskip_counter != 0) ? 1 : 0;
         }

         fps_counter++;
         if (skip_next_frame)
            skip_counter++;

         if (now - fps_last >= 10000)
         {
            u32 rendered = fps_counter - skip_counter;
            float render_fps = rendered * 1000.0f / (now - fps_last);
            float emu_fps = fps_counter * 1000.0f / (now - fps_last);
            printf("render: %.1f fps  emu: %.1f fps  (%u/%u skipped)\n",
                   render_fps, emu_fps, skip_counter, fps_counter);
            fps_counter = 0;
            skip_counter = 0;
            fps_last = now;
         }

         if (now - virtual_ticks > frame_ms * 8)
            virtual_ticks = now;
      }

      if (pvr_txr)
         pvr_mem_free(pvr_txr);
      if (dc_snd_hnd != SND_STREAM_INVALID)
      {
         snd_stream_stop(dc_snd_hnd);
         snd_stream_destroy(dc_snd_hnd);
      }
      snd_stream_shutdown();
      pvr_shutdown();
   }
#else
   Uint32 frame_ms   = (Uint32)(1000.0f / GBA_FPS);
   Uint32 last_ticks = SDL_GetTicks();

   while (running)
   {
      sdl_poll_input();
      update_input();
      rumble_frame_reset();

      /* Run one frame of GBA emulation */
#ifdef HAVE_DYNAREC
      if (dynarec_enable)
         execute_arm_translate(execute_cycles);
      else
#endif
      {
         clear_gamepak_stickybits();
         execute_arm(execute_cycles);
      }

      /* Present video */
      scale_screen(gba_screen_pixels, screen);
      SDL_Flip(screen);

      /* Frame pacing */
      Uint32 now     = SDL_GetTicks();
      Uint32 elapsed = now - last_ticks;
      if (elapsed < frame_ms)
         SDL_Delay(frame_ms - elapsed);
      last_ticks = SDL_GetTicks();
   }

   /* ---- Cleanup ---- */
   SDL_CloseAudio();
   SDL_Quit();
#endif

   memory_term();
   free(gba_screen_pixels);
   gba_screen_pixels = NULL;

   return 0;
}
