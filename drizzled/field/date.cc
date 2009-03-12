/* - mode: c++ c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 MySQL
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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
#include <drizzled/field/date.h>
#include <drizzled/error.h>
#include <drizzled/table.h>
#include <drizzled/session.h>

#include CMATH_H

#if defined(CMATH_NAMESPACE)
using namespace CMATH_NAMESPACE;
#endif

/****************************************************************************
** Drizzle date type stored in 3 bytes
** In number context: YYYYMMDD
****************************************************************************/

/*
  Store string into a date field

  SYNOPSIS
    Field_date::store()
    from                Date string
    len                 Length of date field
    cs                  Character set (not used)

  RETURN
    0  ok
    1  Value was cut during conversion
    2  Wrong date string
    3  Datetime value that was cut (warning level NOTE)
       This is used by opt_range.cc:get_mm_leaf(). Note that there is a
       nearly-identical class Field_date doesn't ever return 3 from its
       store function.
*/

int Field_date::store(const char *from,
                         uint32_t len,
                         const CHARSET_INFO * const cs __attribute__((unused)))
{
  long tmp;
  DRIZZLE_TIME l_time;
  int error;
  Session *session= table ? table->in_use : current_session;
  enum enum_drizzle_timestamp_type ret;
  if ((ret= str_to_datetime(from, len, &l_time,
                            (TIME_FUZZY_DATE |
                             (session->variables.sql_mode &
                              (MODE_NO_ZERO_DATE | MODE_INVALID_DATES))),
                            &error)) <= DRIZZLE_TIMESTAMP_ERROR)
  {
    tmp= 0;
    error= 2;
  }
  else
  {
    tmp= l_time.day + l_time.month*32 + l_time.year*16*32;
    if (!error && (ret != DRIZZLE_TIMESTAMP_DATE) &&
        (l_time.hour || l_time.minute || l_time.second || l_time.second_part))
      error= 3;                                 // Datetime was cut (note)
  }

  if (error)
    set_datetime_warning(error == 3 ? DRIZZLE_ERROR::WARN_LEVEL_NOTE :
                         DRIZZLE_ERROR::WARN_LEVEL_WARN,
                         ER_WARN_DATA_TRUNCATED,
                         from, len, DRIZZLE_TIMESTAMP_DATE, 1);

  int3store(ptr, tmp);
  return error;
}


int Field_date::store(double nr)
{
  if (nr < 0.0 || nr > 99991231235959.0)
  {
    int3store(ptr,(int32_t) 0);
    set_datetime_warning(DRIZZLE_ERROR::WARN_LEVEL_WARN,
                         ER_WARN_DATA_TRUNCATED, nr, DRIZZLE_TIMESTAMP_DATE);
    return 1;
  }
  return Field_date::store((int64_t) rint(nr), false);
}


int Field_date::store(int64_t nr,
                         bool unsigned_val __attribute__((unused)))
{
  DRIZZLE_TIME l_time;
  int64_t tmp;
  int error;
  Session *session= table ? table->in_use : current_session;
  if (number_to_datetime(nr, &l_time,
                         (TIME_FUZZY_DATE |
                          (session->variables.sql_mode &
                           (MODE_NO_ZERO_DATE | MODE_INVALID_DATES))),
                         &error) == INT64_C(-1))
  {
    tmp= 0L;
    error= 2;
  }
  else
    tmp= l_time.day + l_time.month*32 + l_time.year*16*32;

  if (!error && l_time.time_type != DRIZZLE_TIMESTAMP_DATE &&
      (l_time.hour || l_time.minute || l_time.second || l_time.second_part))
    error= 3;

  if (error)
    set_datetime_warning(error == 3 ? DRIZZLE_ERROR::WARN_LEVEL_NOTE :
                         DRIZZLE_ERROR::WARN_LEVEL_WARN,
                         error == 2 ? 
                         ER_WARN_DATA_OUT_OF_RANGE : ER_WARN_DATA_TRUNCATED,
                         nr,DRIZZLE_TIMESTAMP_DATE, 1);

  int3store(ptr,tmp);
  return error;
}


int Field_date::store_time(DRIZZLE_TIME *ltime,
                              enum enum_drizzle_timestamp_type time_type)
{
  long tmp;
  int error= 0;
  if (time_type == DRIZZLE_TIMESTAMP_DATE ||
      time_type == DRIZZLE_TIMESTAMP_DATETIME)
  {
    tmp=ltime->year*16*32+ltime->month*32+ltime->day;
    if (check_date(ltime, tmp != 0,
                   (TIME_FUZZY_DATE |
                    (current_session->variables.sql_mode &
                     (MODE_NO_ZERO_DATE | MODE_INVALID_DATES))), &error))
    {
      char buff[MAX_DATE_STRING_REP_LENGTH];
      String str(buff, sizeof(buff), &my_charset_utf8_general_ci);
      make_date((DATE_TIME_FORMAT *) 0, ltime, &str);
      set_datetime_warning(DRIZZLE_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_TRUNCATED,
                           str.ptr(), str.length(), DRIZZLE_TIMESTAMP_DATE, 1);
    }
    if (!error && ltime->time_type != DRIZZLE_TIMESTAMP_DATE &&
        (ltime->hour || ltime->minute || ltime->second || ltime->second_part))
    {
      char buff[MAX_DATE_STRING_REP_LENGTH];
      String str(buff, sizeof(buff), &my_charset_utf8_general_ci);
      make_datetime((DATE_TIME_FORMAT *) 0, ltime, &str);
      set_datetime_warning(DRIZZLE_ERROR::WARN_LEVEL_NOTE,
                           ER_WARN_DATA_TRUNCATED,
                           str.ptr(), str.length(), DRIZZLE_TIMESTAMP_DATE, 1);
      error= 3;
    }
  }
  else
  {
    tmp=0;
    error= 1;
    set_warning(DRIZZLE_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_TRUNCATED, 1);
  }
  int3store(ptr,tmp);
  return error;
}


bool Field_date::send_binary(Protocol *protocol)
{
  DRIZZLE_TIME tm;
  Field_date::get_date(&tm,0);
  return protocol->store_date(&tm);
}


double Field_date::val_real(void)
{
  return (double) Field_date::val_int();
}


int64_t Field_date::val_int(void)
{
  uint32_t j= uint3korr(ptr);
  j= (j % 32L)+(j / 32L % 16L)*100L + (j/(16L*32L))*10000L;
  return (int64_t) j;
}


String *Field_date::val_str(String *val_buffer,
			       String *val_ptr __attribute__((unused)))
{
  val_buffer->alloc(field_length);
  val_buffer->length(field_length);
  uint32_t tmp=(uint32_t) uint3korr(ptr);
  int part;
  char *pos=(char*) val_buffer->ptr()+10;

  /* Open coded to get more speed */
  *pos--=0;					// End NULL
  part=(int) (tmp & 31);
  *pos--= (char) ('0'+part%10);
  *pos--= (char) ('0'+part/10);
  *pos--= '-';
  part=(int) (tmp >> 5 & 15);
  *pos--= (char) ('0'+part%10);
  *pos--= (char) ('0'+part/10);
  *pos--= '-';
  part=(int) (tmp >> 9);
  *pos--= (char) ('0'+part%10); part/=10;
  *pos--= (char) ('0'+part%10); part/=10;
  *pos--= (char) ('0'+part%10); part/=10;
  *pos=   (char) ('0'+part);
  return val_buffer;
}


bool Field_date::get_date(DRIZZLE_TIME *ltime,uint32_t fuzzydate)
{
  uint32_t tmp=(uint32_t) uint3korr(ptr);
  ltime->day=   tmp & 31;
  ltime->month= (tmp >> 5) & 15;
  ltime->year=  (tmp >> 9);
  ltime->time_type= DRIZZLE_TIMESTAMP_DATE;
  ltime->hour= ltime->minute= ltime->second= ltime->second_part= ltime->neg= 0;
  return ((!(fuzzydate & TIME_FUZZY_DATE) && (!ltime->month || !ltime->day)) ?
          1 : 0);
}


bool Field_date::get_time(DRIZZLE_TIME *ltime)
{
  return Field_date::get_date(ltime,0);
}


int Field_date::cmp(const unsigned char *a_ptr, const unsigned char *b_ptr)
{
  uint32_t a,b;
  a=(uint32_t) uint3korr(a_ptr);
  b=(uint32_t) uint3korr(b_ptr);
  return (a < b) ? -1 : (a > b) ? 1 : 0;
}


void Field_date::sort_string(unsigned char *to,uint32_t length __attribute__((unused)))
{
  to[0] = ptr[2];
  to[1] = ptr[1];
  to[2] = ptr[0];
}


void Field_date::sql_type(String &res) const
{
  res.set_ascii(STRING_WITH_LEN("date"));
}
