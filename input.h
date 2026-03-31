/* gameplaySP
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  BUTTON_L      = 0x200,
  BUTTON_R      = 0x100,
  BUTTON_DOWN   = 0x80,
  BUTTON_UP     = 0x40,
  BUTTON_LEFT   = 0x20,
  BUTTON_RIGHT  = 0x10,
  BUTTON_START  = 0x08,
  BUTTON_SELECT = 0x04,
  BUTTON_B      = 0x02,
  BUTTON_A      = 0x01,
  BUTTON_NONE   = 0x00
} input_buttons_type;

/* Input callback: returns non-zero if button `id` is pressed */
typedef int16_t (*input_state_fn_t)(unsigned port, unsigned device,
                                    unsigned index, unsigned id);

/* Button IDs (matching old libretro constants for compatibility) */
#define BUTTON_ID_B        0
#define BUTTON_ID_Y        1
#define BUTTON_ID_SELECT   2
#define BUTTON_ID_START    3
#define BUTTON_ID_UP       4
#define BUTTON_ID_DOWN     5
#define BUTTON_ID_LEFT     6
#define BUTTON_ID_RIGHT    7
#define BUTTON_ID_A        8
#define BUTTON_ID_X        9
#define BUTTON_ID_L       10
#define BUTTON_ID_R       11
#define BUTTON_ID_R2      13

typedef struct
{
   unsigned id;
   input_buttons_type gba;
} map;

static const map btn_map[] = {
   { BUTTON_ID_L,      BUTTON_L },
   { BUTTON_ID_R,      BUTTON_R },
   { BUTTON_ID_DOWN,   BUTTON_DOWN },
   { BUTTON_ID_UP,     BUTTON_UP },
   { BUTTON_ID_LEFT,   BUTTON_LEFT },
   { BUTTON_ID_RIGHT,  BUTTON_RIGHT },
   { BUTTON_ID_START,  BUTTON_START },
   { BUTTON_ID_SELECT, BUTTON_SELECT },
   { BUTTON_ID_B,      BUTTON_B },
   { BUTTON_ID_A,      BUTTON_A }
};

/* Minimum (and default) turbo pulse train
 * is 2 frames ON, 2 frames OFF */
#define TURBO_PERIOD_MIN      4
#define TURBO_PERIOD_MAX      120
#define TURBO_PULSE_WIDTH_MIN 2
#define TURBO_PULSE_WIDTH_MAX 15

extern unsigned turbo_period;
extern unsigned turbo_pulse_width;
extern unsigned turbo_a_counter;
extern unsigned turbo_b_counter;

void set_input_state(input_state_fn_t cb);
u32 update_input(void);

bool input_check_savestate(const u8 *src);
unsigned input_write_savestate(u8* dst);
bool input_read_savestate(const u8 *src);

#endif
