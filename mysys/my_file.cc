/* Copyright (C) 2000 MySQL AB

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

#include "mysys_priv.h"
#include "my_static.h"
#include <mystrings/m_string.h>
#include <stdlib.h>
#include <string.h>

/*
  set how many open files we want to be able to handle

  SYNOPSIS
    set_maximum_open_files()
    max_file_limit		Files to open

  NOTES
    The request may not fulfilled becasue of system limitations

  RETURN
    Files available to open.
    May be more or less than max_file_limit!
*/

#if defined(HAVE_GETRLIMIT) && defined(RLIMIT_NOFILE)

#ifndef RLIM_INFINITY
#define RLIM_INFINITY ((uint) 0xffffffff)
#endif

static uint32_t set_max_open_files(uint32_t max_file_limit)
{
  struct rlimit rlimit;
  rlim_t old_cur;

  if (!getrlimit(RLIMIT_NOFILE,&rlimit))
  {
    old_cur= rlimit.rlim_cur;
    if (rlimit.rlim_cur == RLIM_INFINITY)
      rlimit.rlim_cur = max_file_limit;
    if (rlimit.rlim_cur >= max_file_limit)
    {
      if (rlimit.rlim_cur > UINT32_MAX)
        return UINT32_MAX;
      else
        return((uint32_t)rlimit.rlim_cur);
    }
    rlimit.rlim_cur= rlimit.rlim_max= max_file_limit;
    if (setrlimit(RLIMIT_NOFILE, &rlimit))
      max_file_limit= (old_cur < UINT32_MAX) ? (uint32_t)old_cur : UINT32_MAX;
    else
    {
      rlimit.rlim_cur= 0;			/* Safety if next call fails */
      (void) getrlimit(RLIMIT_NOFILE,&rlimit);
      if (rlimit.rlim_cur)			/* If call didn't fail */
	max_file_limit= (uint32_t) rlimit.rlim_cur;
    }
  }
  return(max_file_limit);
}

#else
static int set_max_open_files(uint32_t max_file_limit)
{
  /* We don't know the limit. Return best guess */
  return cmin(max_file_limit, OS_FILE_LIMIT);
}
#endif


/*
  Change number of open files

  SYNOPSIS:
    my_set_max_open_files()
    files		Number of requested files

  RETURN
    number of files available for open
*/

uint32_t my_set_max_open_files(uint32_t files)
{
  struct st_my_file_info *tmp;

  files= set_max_open_files(cmin(files, OS_FILE_LIMIT));
  if (files <= MY_NFILE)
    return(files);

  if (!(tmp= (st_my_file_info*) malloc(sizeof(st_my_file_info) * files)))
    return(MY_NFILE);

  /* Copy any initialized files */
  memcpy(tmp, my_file_info, sizeof(*tmp) * cmin(my_file_limit, files));
  /*
    The int cast is necessary since 'my_file_limits' might be greater
    than 'files'.
  */
  memset(tmp + my_file_limit, 0,
         cmax((int) (files - my_file_limit), 0)*sizeof(*tmp));
  my_free_open_file_info();			/* Free if already allocated */
  my_file_info= tmp;
  my_file_limit= files;
  return(files);
}


void my_free_open_file_info()
{
  if (my_file_info != my_file_info_default)
  {
    /* Copy data back for my_print_open_files */
    memcpy(my_file_info_default, my_file_info,
           sizeof(*my_file_info_default)* MY_NFILE);
    free((char*) my_file_info);
    my_file_info= my_file_info_default;
    my_file_limit= MY_NFILE;
  }
  return;
}