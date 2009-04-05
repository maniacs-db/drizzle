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


#ifndef DRIZZLED_SELECT_RESULT_H
#define DRIZZLED_SELECT_RESULT_H

class select_result :public Sql_alloc {
protected:
  Session *session;
  Select_Lex_Unit *unit;
public:
  select_result()
  {
    session= current_session;
  }
  virtual ~select_result() {};
  virtual int prepare(List<Item> &,
                      Select_Lex_Unit *u)
  {
    unit= u;
    return 0;
  }
  /*
    Because of peculiarities of prepared statements protocol
    we need to know number of columns in the result set (if
    there is a result set) apart from sending columns metadata.
  */
  virtual uint32_t field_count(List<Item> &fields) const
  { return fields.elements; }
  virtual bool send_fields(List<Item> &list, uint32_t flags)=0;
  virtual bool send_data(List<Item> &items)=0;
  virtual bool initialize_tables (JOIN *)
  { return 0; }
  virtual bool send_eof()=0;
  virtual void abort() {}
  void set_session(Session *session_arg) { session= session_arg; }
  void begin_dataset() {}

  /*****************************************************************************
   ** Functions to provide a interface to select results
   *****************************************************************************/

  virtual void send_error(uint32_t errcode, const char *err)
  {
    my_message(errcode, err, MYF(0));
  }

  /*
    Cleanup instance of this class for next execution of a prepared
    statement/stored procedure.
  */
  virtual void cleanup()
  {
    /* do nothing */
  }

};

#endif /* DRIZZLED_SELECT_RESULT_H */