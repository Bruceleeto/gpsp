/* gpSP standalone SDL2 frontend
 *
 * Replaces the libretro frontend with a built-in SDL2 backend.
 * Handles video, audio, and input directly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "common.h"
#include "gba_memory.h"
#include "gba_cc_lut.h"

/* Usually ~59.73 Hz */
#define GBA_FPS (((float)GBC_BASE_RATE) / (308 * 228 * 4))

#define WINDOW_SCALE 3

/* ---- Globals the core expects (normally in libretro.c) ---- */
u32 skip_next_frame = 0;
u32 num_skipped_frames = 0;
int dynarec_enable = 0;
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

/* ---- SDL state ---- */
static SDL_Window   *sdl_window   = NULL;
static SDL_Renderer *sdl_renderer = NULL;
static SDL_Texture  *sdl_texture  = NULL;
static SDL_AudioDeviceID sdl_audio_dev = 0;

static int running = 1;
static u32 sdl_key_state = 0;

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
            switch (ev.key.keysym.scancode)
            {
               case SDL_SCANCODE_X:      btn = BUTTON_A;      break;
               case SDL_SCANCODE_Z:      btn = BUTTON_B;      break;
               case SDL_SCANCODE_RETURN: btn = BUTTON_START;  break;
               case SDL_SCANCODE_RSHIFT: btn = BUTTON_SELECT; break;
               case SDL_SCANCODE_UP:     btn = BUTTON_UP;     break;
               case SDL_SCANCODE_DOWN:   btn = BUTTON_DOWN;   break;
               case SDL_SCANCODE_LEFT:   btn = BUTTON_LEFT;   break;
               case SDL_SCANCODE_RIGHT:  btn = BUTTON_RIGHT;  break;
               case SDL_SCANCODE_A:      btn = BUTTON_L;      break;
               case SDL_SCANCODE_S:      btn = BUTTON_R;      break;
               case SDL_SCANCODE_ESCAPE:
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

/* ---- Main ---- */
int main(int argc, char *argv[])
{
   const char *rom_path  = (argc > 1) ? argv[1] : "assets/test.bin";
   const char *bios_path = (argc > 2) ? argv[2] : NULL;

   /* ---- SDL init ---- */
   if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
   {
      fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
      return 1;
   }

   sdl_window = SDL_CreateWindow("gpSP",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      GBA_SCREEN_WIDTH * WINDOW_SCALE,
      GBA_SCREEN_HEIGHT * WINDOW_SCALE,
      SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
   if (!sdl_window)
   {
      fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
      SDL_Quit();
      return 1;
   }

   sdl_renderer = SDL_CreateRenderer(sdl_window, -1,
      SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
   if (!sdl_renderer)
      sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_SOFTWARE);

   SDL_RenderSetLogicalSize(sdl_renderer, GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT);

   sdl_texture = SDL_CreateTexture(sdl_renderer,
      SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
      GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT);

   /* ---- Audio ---- */
   SDL_AudioSpec want;
   SDL_memset(&want, 0, sizeof(want));
   want.freq     = GBA_SOUND_FREQUENCY;
   want.format   = AUDIO_S16SYS;
   want.channels = 2;
   want.samples  = 1024;
   want.callback = sdl_audio_callback;

   sdl_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
   if (!sdl_audio_dev)
      fprintf(stderr, "SDL audio failed: %s\n", SDL_GetError());

   /* ---- Emulator init ---- */
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
      SDL_Quit();
      return 1;
   }
   printf("Loaded ROM: %s\n", rom_path);

   /* Wire up input: reuse input.c by providing our SDL callback */
   set_input_state(sdl_input_state_cb);

   /* Reset GBA */
   reset_gba();

   /* Start audio playback */
   if (sdl_audio_dev)
      SDL_PauseAudioDevice(sdl_audio_dev, 0);

   /* ---- Main loop ---- */
   Uint32 frame_ms    = (Uint32)(1000.0f / GBA_FPS);
   Uint32 last_ticks  = SDL_GetTicks();

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
      SDL_UpdateTexture(sdl_texture, NULL,
                        gba_screen_pixels, GBA_SCREEN_PITCH * 2);
      SDL_RenderClear(sdl_renderer);
      SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, NULL);
      SDL_RenderPresent(sdl_renderer);

      /* Frame pacing (vsync handles most of it, this is a fallback) */
      Uint32 now     = SDL_GetTicks();
      Uint32 elapsed = now - last_ticks;
      if (elapsed < frame_ms)
         SDL_Delay(frame_ms - elapsed);
      last_ticks = SDL_GetTicks();
   }

   /* ---- Cleanup ---- */
   if (sdl_audio_dev)
      SDL_CloseAudioDevice(sdl_audio_dev);
   SDL_DestroyTexture(sdl_texture);
   SDL_DestroyRenderer(sdl_renderer);
   SDL_DestroyWindow(sdl_window);
   SDL_Quit();

   memory_term();
   free(gba_screen_pixels);
   gba_screen_pixels = NULL;

   return 0;
}
