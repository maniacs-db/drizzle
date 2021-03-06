/* Copyright (C) 2000-2001, 2003-2004 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */


/* Mallocs for used in threads */

#include <config.h>

#include <string.h>

#include <drizzled/errmsg_print.h>
#include <drizzled/memory/sql_alloc.h>
#include <drizzled/current_session.h>
#include <drizzled/error.h>
#include <drizzled/definitions.h>
#include <drizzled/internal/my_sys.h>

namespace drizzled {
namespace memory {

void* sql_alloc(size_t Size)
{
  return current_mem_root()->alloc(Size);
}

void* sql_calloc(size_t size)
{
  return current_mem_root()->calloc(size);
}

char* sql_strdup(const char* str)
{
  return current_mem_root()->strdup(str);
}

char* sql_strdup(str_ref str)
{
  return current_mem_root()->strdup(str);
}

void* sql_memdup(const void* ptr, size_t len)
{
  return current_mem_root()->memdup(ptr, len);
}

}
} /* namespace drizzled */
