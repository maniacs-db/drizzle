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

#ifndef DRIZZLED_FUNCTION_REAL_H
#define DRIZZLED_FUNCTION_REAL_H

#include <drizzled/item.h>
#include <drizzled/function/func.h>

class Item_real_func :public Item_func
{
public:
  Item_real_func() :Item_func() {}
  Item_real_func(Item *a) :Item_func(a) {}
  Item_real_func(Item *a,Item *b) :Item_func(a,b) {}
  Item_real_func(List<Item> &list) :Item_func(list) {}
  String *val_str(String*str);
  my_decimal *val_decimal(my_decimal *decimal_value);
  int64_t val_int();
  enum Item_result result_type () const { return REAL_RESULT; }
  void fix_length_and_dec();
};

#endif /* DRIZZLED_FUNCTION_REAL_H */