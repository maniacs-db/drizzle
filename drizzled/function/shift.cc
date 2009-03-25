/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <drizzled/server_includes.h>
#include CSTDINT_H
#include <drizzled/function/bit.h>

// Shift-functions, same as << and >> in C/C++

int64_t Item_func_shift_left::val_int()
{
  assert(fixed == 1);
  uint32_t shift;
  uint64_t res= ((uint64_t) args[0]->val_int() <<
                  (shift=(uint32_t) args[1]->val_int()));
  if (args[0]->null_value || args[1]->null_value)
  {
    null_value=1;
    return 0;
  }
  null_value=0;
  return (shift < sizeof(int64_t)*8 ? (int64_t) res : 0L);
}

int64_t Item_func_shift_right::val_int()
{
  assert(fixed == 1);
  uint32_t shift;
  uint64_t res= (uint64_t) args[0]->val_int() >>
    (shift=(uint32_t) args[1]->val_int());
  if (args[0]->null_value || args[1]->null_value)
  {
    null_value=1;
    return 0;
  }
  null_value=0;
  return (shift < sizeof(int64_t)*8 ? (int64_t) res : 0);
}
