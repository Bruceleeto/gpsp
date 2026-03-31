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

#include "common.h"

unsigned turbo_period      = TURBO_PERIOD_MIN;
unsigned turbo_pulse_width = TURBO_PULSE_WIDTH_MIN;
unsigned turbo_a_counter   = 0;
unsigned turbo_b_counter   = 0;

static u32 old_key = 0;
static input_state_fn_t input_state_cb = NULL;

void set_input_state(input_state_fn_t cb) { input_state_cb = cb; }

static void trigger_key(u32 key)
{
  u32 p1_cnt = read_ioreg(REG_P1CNT);

  if((p1_cnt >> 14) & 0x01)
  {
    u32 key_intersection = (p1_cnt & key) & 0x3FF;

    if(p1_cnt >> 15)
    {
      if(key_intersection == (p1_cnt & 0x3FF))
      {
        flag_interrupt(IRQ_KEYPAD);
        check_and_raise_interrupts();
      }
    }
    else
    {
      if(key_intersection)
      {
        flag_interrupt(IRQ_KEYPAD);
        check_and_raise_interrupts();
      }
    }
  }
}

u32 update_input(void)
{
   unsigned i;
   uint32_t new_key = 0;

   if (!input_state_cb)
      return 0;

   for (i = 0; i < sizeof(btn_map) / sizeof(map); i++)
      new_key |= input_state_cb(0, 0, 0, btn_map[i].id) ? btn_map[i].gba : 0;

   // GBP keypad detection hack (only at game startup!)
   if (serial_mode == SERIAL_MODE_GBP) {
     // During the startup screen (aproximate)
     if (frame_counter > 20 && frame_counter < 100) {
       // Emulate 4 keypad buttons pressed (which is impossible).
       new_key = (frame_counter % 3) ? 0x3FF : 0x30F;
     }
   }

   if ((new_key | old_key) != old_key)
      trigger_key(new_key);

   old_key = new_key;
   write_ioreg(REG_P1, (~old_key) & 0x3FF);

   return 0;
}

bool input_check_savestate(const u8 *src)
{
  const u8 *p = bson_find_key(src, "input");
  return (p && bson_contains_key(p, "prevkey", BSON_TYPE_INT32));
}

bool input_read_savestate(const u8 *src)
{
  const u8 *p = bson_find_key(src, "input");
  if (p)
    return bson_read_int32(p, "prevkey", &old_key);
  return false;
}

unsigned input_write_savestate(u8 *dst)
{
  u8 *wbptr1, *startp = dst;
  bson_start_document(dst, "input", wbptr1);
  bson_write_int32(dst, "prevkey", old_key);
  bson_finish_document(dst, wbptr1);
  return (unsigned int)(dst - startp);
}
