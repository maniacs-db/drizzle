/* Copyright (C) 2000-2002 MySQL AB

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

/*
  Rename a table
*/

#include "heap_priv.h"
#include <string.h>

int heap_rename(const char *old_name, const char *new_name)
{
  register HP_SHARE *info;
  char *name_buff;

  pthread_mutex_lock(&THR_LOCK_heap);
  if ((info = hp_find_named_heap(old_name)))
  {
    if (!(name_buff=(char*) strdup(new_name)))
    {
      pthread_mutex_unlock(&THR_LOCK_heap);
      return(my_errno);
    }
    free(info->name);
    info->name=name_buff;
  }
  pthread_mutex_unlock(&THR_LOCK_heap);
  return(0);
}
