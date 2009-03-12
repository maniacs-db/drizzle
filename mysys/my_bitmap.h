/* Copyright (C) 2000 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef _my_bitmap_h_
#define _my_bitmap_h_

#define MY_BIT_NONE (~(uint) 0)

#include <mystrings/m_string.h>

typedef uint32_t my_bitmap_map;

typedef struct st_bitmap
{
  my_bitmap_map *bitmap;
  uint32_t n_bits; /* number of bits occupied by the above */
  my_bitmap_map last_word_mask;
  my_bitmap_map *last_word_ptr;
  /*
     mutex will be acquired for the duration of each bitmap operation if
     thread_safe flag in bitmap_init was set.  Otherwise, we optimize by not
     acquiring the mutex
   */
  pthread_mutex_t *mutex;
} MY_BITMAP;

#ifdef	__cplusplus
extern "C" {
#endif
extern void create_last_word_mask(MY_BITMAP *map);
extern bool bitmap_init(MY_BITMAP *map, my_bitmap_map *buf, uint32_t n_bits,
                           bool thread_safe);
extern bool bitmap_is_clear_all(const MY_BITMAP *map);
extern bool bitmap_is_prefix(const MY_BITMAP *map, uint32_t prefix_size);
extern bool bitmap_is_set_all(const MY_BITMAP *map);
extern bool bitmap_is_subset(const MY_BITMAP *map1, const MY_BITMAP *map2);
extern bool bitmap_is_overlapping(const MY_BITMAP *map1,
                                     const MY_BITMAP *map2);
extern bool bitmap_test_and_set(MY_BITMAP *map, uint32_t bitmap_bit);
extern bool bitmap_test_and_clear(MY_BITMAP *map, uint32_t bitmap_bit);
extern bool bitmap_fast_test_and_clear(MY_BITMAP *map, uint32_t bitmap_bit);
extern bool bitmap_fast_test_and_set(MY_BITMAP *map, uint32_t bitmap_bit);
extern uint32_t bitmap_set_next(MY_BITMAP *map);
extern uint32_t bitmap_get_first(const MY_BITMAP *map);
extern uint32_t bitmap_get_first_set(const MY_BITMAP *map);
extern uint32_t bitmap_bits_set(const MY_BITMAP *map);
extern void bitmap_free(MY_BITMAP *map);
extern void bitmap_set_above(MY_BITMAP *map, uint32_t from_byte, uint32_t use_bit);
extern void bitmap_set_prefix(MY_BITMAP *map, uint32_t prefix_size);
extern void bitmap_intersect(MY_BITMAP *map, const MY_BITMAP *map2);
extern void bitmap_subtract(MY_BITMAP *map, const MY_BITMAP *map2);
extern void bitmap_union(MY_BITMAP *map, const MY_BITMAP *map2);
extern void bitmap_xor(MY_BITMAP *map, const MY_BITMAP *map2);
extern void bitmap_invert(MY_BITMAP *map);
extern void bitmap_copy(MY_BITMAP *map, const MY_BITMAP *map2);

extern uint32_t bitmap_lock_set_next(MY_BITMAP *map);
extern void bitmap_lock_clear_bit(MY_BITMAP *map, uint32_t bitmap_bit);
#ifdef NOT_USED
extern uint32_t bitmap_lock_bits_set(const MY_BITMAP *map);
extern bool bitmap_lock_is_set_all(const MY_BITMAP *map);
extern uint32_t bitmap_lock_get_first(const MY_BITMAP *map);
extern uint32_t bitmap_lock_get_first_set(const MY_BITMAP *map);
extern bool bitmap_lock_is_subset(const MY_BITMAP *map1,
                                     const MY_BITMAP *map2);
extern bool bitmap_lock_is_prefix(const MY_BITMAP *map, uint32_t prefix_size);
extern bool bitmap_lock_is_set(const MY_BITMAP *map, uint32_t bitmap_bit);
extern bool bitmap_lock_is_clear_all(const MY_BITMAP *map);
extern bool bitmap_lock_cmp(const MY_BITMAP *map1, const MY_BITMAP *map2);
extern void bitmap_lock_set_all(MY_BITMAP *map);
extern void bitmap_lock_clear_all(MY_BITMAP *map);
extern void bitmap_lock_set_bit(MY_BITMAP *map, uint32_t bitmap_bit);
extern void bitmap_lock_flip_bit(MY_BITMAP *map, uint32_t bitmap_bit);
extern void bitmap_lock_set_prefix(MY_BITMAP *map, uint32_t prefix_size);
extern void bitmap_lock_intersect(MY_BITMAP *map, const MY_BITMAP *map2);
extern void bitmap_lock_subtract(MY_BITMAP *map, const MY_BITMAP *map2);
extern void bitmap_lock_union(MY_BITMAP *map, const MY_BITMAP *map2);
extern void bitmap_lock_xor(MY_BITMAP *map, const MY_BITMAP *map2);
extern void bitmap_lock_invert(MY_BITMAP *map);
#endif
/* Fast, not thread safe, bitmap functions */
#define bitmap_buffer_size(bits) (((bits)+31)/32)*4
#define no_bytes_in_map(map) (((map)->n_bits + 7)/8)
#define no_words_in_map(map) (((map)->n_bits + 31)/32)
#define bytes_word_aligned(bytes) (4*((bytes + 3)/4))
#define _bitmap_set_bit(MAP, BIT) (((unsigned char*)(MAP)->bitmap)[(BIT) / 8] \
                                  |= (1 << ((BIT) & 7)))
#define _bitmap_flip_bit(MAP, BIT) (((unsigned char*)(MAP)->bitmap)[(BIT) / 8] \
                                  ^= (1 << ((BIT) & 7)))
#define _bitmap_clear_bit(MAP, BIT) (((unsigned char*)(MAP)->bitmap)[(BIT) / 8] \
                                  &= ~ (1 << ((BIT) & 7)))
#define _bitmap_is_set(MAP, BIT) (uint) (((unsigned char*)(MAP)->bitmap)[(BIT) / 8] \
                                         & (1 << ((BIT) & 7)))
/*
  NEVER use an increment/decrement operator with the 'bit' argument.

  FORBIDDEN: bitmap_set_bit($my_bitmap, (field++)->field_index);
*/
#define bitmap_set_bit(MAP, BIT) _bitmap_set_bit(MAP, BIT)
#define bitmap_flip_bit(MAP, BIT) _bitmap_flip_bit(MAP, BIT)
#define bitmap_clear_bit(MAP, BIT) _bitmap_clear_bit(MAP, BIT)
#define bitmap_is_set(MAP, BIT) _bitmap_is_set(MAP, BIT)

static inline bool bitmap_cmp(const MY_BITMAP *map1, const MY_BITMAP *map2)
{
  *(map1)->last_word_ptr|= (map1)->last_word_mask;
  *(map2)->last_word_ptr|= (map2)->last_word_mask;
  return memcmp((map1)->bitmap, (map2)->bitmap, 4*no_words_in_map((map1)))==0;
}

#define bitmap_clear_all(MAP) \
  { memset((MAP)->bitmap, 0, 4*no_words_in_map((MAP))); }
#define bitmap_set_all(MAP) \
  (memset((MAP)->bitmap, 0xFF, 4*no_words_in_map((MAP))))

/**
   check, set and clear a bit of interest of an integer.

   If the bit is out of range @retval -1. Otherwise
   bit_is_set   @return 0 or 1 reflecting the bit is set or not;
   bit_do_set   @return 1 (bit is set 1)
   bit_do_clear @return 0 (bit is cleared to 0)
*/

#define bit_is_set(I,B)   (sizeof(I) * CHAR_BIT > (B) ?                 \
                           (((I) & (1UL << (B))) == 0 ? 0 : 1) : -1)
#define bit_do_set(I,B)   (sizeof(I) * CHAR_BIT > (B) ?         \
                           ((I) |= (1UL << (B)), 1) : -1)
#define bit_do_clear(I,B) (sizeof(I) * CHAR_BIT > (B) ?         \
                           ((I) &= ~(1UL << (B)), 0) : -1)

#ifdef	__cplusplus
}
#endif

#endif /* _my_bitmap_h_ */