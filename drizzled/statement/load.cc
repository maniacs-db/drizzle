/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2009 Sun Microsystems, Inc.
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
#include <drizzled/show.h>
#include <drizzled/session.h>
#include <drizzled/sql_load.h>
#include <drizzled/statement/load.h>

namespace drizzled
{

bool statement::Load::execute()
{
  TableList *first_table= (TableList *) getSession()->lex->select_lex.table_list.first;
  TableList *all_tables= getSession()->lex->query_tables;
  assert(first_table == all_tables && first_table != 0);
  bool res= load(getSession(),
                 getSession()->lex->exchange,
                 first_table,
                 getSession()->lex->field_list,
                 getSession()->lex->update_list,
                 getSession()->lex->value_list,
                 getSession()->lex->duplicates,
                 getSession()->lex->ignore);

  return res;
}

} /* namespace drizzled */

