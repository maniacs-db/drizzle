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
#include <drizzled/functions/time/from_unixtime.h>
#include <drizzled/tztime.h>

void Item_func_from_unixtime::fix_length_and_dec()
{
  session= current_session;
  collation.set(&my_charset_bin);
  decimals= DATETIME_DEC;
  max_length=MAX_DATETIME_WIDTH*MY_CHARSET_BIN_MB_MAXLEN;
  maybe_null= 1;
  session->time_zone_used= 1;
}

String *Item_func_from_unixtime::val_str(String *str)
{
  DRIZZLE_TIME time_tmp;

  assert(fixed == 1);

  if (get_date(&time_tmp, 0))
    return 0;

  if (str->alloc(MAX_DATE_STRING_REP_LENGTH))
  {
    null_value= 1;
    return 0;
  }

  make_datetime((DATE_TIME_FORMAT *) 0, &time_tmp, str);

  return str;
}

int64_t Item_func_from_unixtime::val_int()
{
  DRIZZLE_TIME time_tmp;

  assert(fixed == 1);

  if (get_date(&time_tmp, 0))
    return 0;

  return (int64_t) TIME_to_uint64_t_datetime(&time_tmp);
}

bool Item_func_from_unixtime::get_date(DRIZZLE_TIME *ltime,
                                       uint32_t fuzzy_date __attribute__((unused)))
{
  uint64_t tmp= (uint64_t)(args[0]->val_int());
  /*
    "tmp > TIMESTAMP_MAX_VALUE" check also covers case of negative
    from_unixtime() argument since tmp is unsigned.
  */
  if ((null_value= (args[0]->null_value || tmp > TIMESTAMP_MAX_VALUE)))
    return 1;

  session->variables.time_zone->gmt_sec_to_TIME(ltime, (my_time_t)tmp);

  return 0;
}


