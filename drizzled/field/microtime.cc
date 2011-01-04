/* -*- mode: c++ c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems, Inc.
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

#include "config.h"
#include <boost/lexical_cast.hpp>
#include <drizzled/field/microtime.h>
#include <drizzled/error.h>
#include <drizzled/tztime.h>
#include <drizzled/table.h>
#include <drizzled/session.h>

#include <math.h>

#include <sstream>

#include "drizzled/temporal.h"

namespace drizzled
{

namespace field
{
Microtime::Microtime(unsigned char *ptr_arg,
                     unsigned char *null_ptr_arg,
                     unsigned char null_bit_arg,
                     enum utype unireg_check_arg,
                     const char *field_name_arg,
                     drizzled::TableShare *share) :
  Epoch(ptr_arg,
        null_ptr_arg,
        null_bit_arg,
        unireg_check_arg,
        field_name_arg,
        share)
  {
  }

Microtime::Microtime(bool maybe_null_arg,
                     const char *field_name_arg) :
  Epoch(maybe_null_arg,
        field_name_arg)
{
}

/**
  Get auto-set type for TIMESTAMP field.

  Returns value indicating during which operations this TIMESTAMP field
  should be auto-set to current timestamp.
*/
timestamp_auto_set_type Microtime::get_auto_set_type() const
{
  switch (unireg_check)
  {
  case TIMESTAMP_DN_FIELD:
    return TIMESTAMP_AUTO_SET_ON_INSERT;
  case TIMESTAMP_UN_FIELD:
    return TIMESTAMP_AUTO_SET_ON_UPDATE;
  case TIMESTAMP_OLD_FIELD:
    /*
      Although we can have several such columns in legacy tables this
      function should be called only for first of them (i.e. the one
      having auto-set property).
    */
    assert(getTable()->timestamp_field == this);
    /* Fall-through */
  case TIMESTAMP_DNUN_FIELD:
    return TIMESTAMP_AUTO_SET_ON_BOTH;
  default:
    /*
      Normally this function should not be called for TIMESTAMPs without
      auto-set property.
    */
    assert(0);
    return TIMESTAMP_NO_AUTO_SET;
  }
}

int Microtime::store(const char *from,
                 uint32_t len,
                 const CHARSET_INFO * const )
{
  Timestamp temporal;

  ASSERT_COLUMN_MARKED_FOR_WRITE;

  if (not temporal.from_string(from, (size_t) len))
  {
    my_error(ER_INVALID_UNIX_TIMESTAMP_VALUE, MYF(ME_FATALERROR), from);
    return 1;
  }

  time_t tmp;
  temporal.to_time_t(tmp);

  pack_num(tmp);
  return 0;
}

int Microtime::store(double from)
{
  ASSERT_COLUMN_MARKED_FOR_WRITE;

  if (from < 0 || from > 99991231235959.0)
  {
    /* Convert the double to a string using stringstream */
    std::stringstream ss;
    std::string tmp;
    ss.precision(18); /* 18 places should be fine for error display of double input. */
    ss << from; 
    ss >> tmp;

    my_error(ER_INVALID_UNIX_TIMESTAMP_VALUE, MYF(ME_FATALERROR), tmp.c_str());
    return 2;
  }
  return Microtime::store((int64_t) rint(from), false);
}

int Microtime::store(int64_t from, bool)
{
  ASSERT_COLUMN_MARKED_FOR_WRITE;

  /* 
   * Try to create a DateTime from the supplied integer.  Throw an error
   * if unable to create a valid DateTime.  
   */
  Timestamp temporal;
  if (! temporal.from_int64_t(from))
  {
    /* Convert the integer to a string using boost::lexical_cast */
    std::string tmp(boost::lexical_cast<std::string>(from));

    my_error(ER_INVALID_UNIX_TIMESTAMP_VALUE, MYF(ME_FATALERROR), tmp.c_str());
    return 2;
  }

  time_t tmp;
  temporal.to_time_t(tmp);

  pack_num(tmp);

  return 0;
}

double Microtime::val_real(void)
{
  return (double) Microtime::val_int();
}

int64_t Microtime::val_int(void)
{
  uint64_t temp;

  ASSERT_COLUMN_MARKED_FOR_READ;

  unpack_num(temp);

  Timestamp temporal;
  (void) temporal.from_time_t((time_t) temp);

  /* We must convert into a "timestamp-formatted integer" ... */
  int64_t result;
  temporal.to_int64_t(&result);
  return result;
}

String *Microtime::val_str(String *val_buffer, String *)
{
  uint64_t temp= 0;
  char *to;
  int to_len= field_length + 1;

  val_buffer->alloc(to_len);
  to= (char *) val_buffer->ptr();

  unpack_num(temp);

  val_buffer->set_charset(&my_charset_bin);	/* Safety */

  Timestamp temporal;
  (void) temporal.from_time_t((time_t) temp);

  int rlen;
  rlen= temporal.to_string(to, to_len);
  assert(rlen < to_len);

  val_buffer->length(rlen);
  return val_buffer;
}

bool Microtime::get_date(type::Time *ltime, uint32_t)
{
  uint64_t temp;

  unpack_num(temp);
  
  memset(ltime, 0, sizeof(*ltime));

  Timestamp temporal;
  (void) temporal.from_time_t((time_t) temp);

  /* @TODO Goodbye the below code when type::Time is finally gone.. */

  ltime->time_type= DRIZZLE_TIMESTAMP_DATETIME;
  ltime->year= temporal.years();
  ltime->month= temporal.months();
  ltime->day= temporal.days();
  ltime->hour= temporal.hours();
  ltime->minute= temporal.minutes();
  ltime->second= temporal.seconds();

  return 0;
}

bool Microtime::get_time(type::Time *ltime)
{
  return Microtime::get_date(ltime,0);
}

int Microtime::cmp(const unsigned char *a_ptr, const unsigned char *b_ptr)
{
  uint64_t a,b;

  unpack_num(a, a_ptr);
  unpack_num(b, b_ptr);

  return (a < b) ? -1 : (a > b) ? 1 : 0;
}


void Microtime::sort_string(unsigned char *to,uint32_t )
{
#ifdef WORDS_BIGENDIAN
  if ((not getTable()) or (not getTable()->getShare()->db_low_byte_first))
  {
    std::reverse_copy(to, to+pack_length(), ptr);
  }
  else
#endif
  {
    memcpy(to, ptr, pack_length());
  }
}

void Microtime::sql_type(String &res) const
{
  res.set_ascii(STRING_WITH_LEN("microtime"));
}

void Microtime::set_time()
{
  Session *session= getTable() ? getTable()->in_use : current_session;
  time_t tmp= session->query_start();
  set_notnull();
  pack_num(tmp);
}

void Microtime::set_default()
{
  if (getTable()->timestamp_field == this &&
      unireg_check != TIMESTAMP_UN_FIELD)
  {
    set_time();
  }
  else
  {
    Field::set_default();
  }
}

long Microtime::get_timestamp(bool *null_value)
{
  if ((*null_value= is_null()))
    return 0;

  uint64_t tmp;
  return unpack_num(tmp);
}

} /* namespace field */
} /* namespace drizzled */
