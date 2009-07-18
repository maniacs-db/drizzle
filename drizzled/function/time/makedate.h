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

#ifndef DRIZZLED_FUNCTION_TIME_MAKEDATE_H
#define DRIZZLED_FUNCTION_TIME_MAKEDATE_H

#include <drizzled/function/time/date.h>

class Item_func_makedate :public Item_date_func
{
public:
  Item_func_makedate(Item *a,Item *b) :Item_date_func(a,b) {}
  String *val_str(String *str);
  const char *func_name() const { return "makedate"; }
  enum_field_types field_type() const { return DRIZZLE_TYPE_DATE; }
  void fix_length_and_dec()
  {
    decimals=0;
    max_length=drizzled::Date::MAX_STRING_LENGTH*MY_CHARSET_BIN_MB_MAXLEN;
  }
  int64_t val_int();
};

#endif /* DRIZZLED_FUNCTION_TIME_MAKEDATE_H */
