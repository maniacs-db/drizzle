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
#include "mysys_err.h"
#include <my_dir.h>
#include <errno.h>
#include <stdlib.h>

/*
  Open a file

  SYNOPSIS
    my_open()
      FileName	Fully qualified file name
      Flags	Read | write 
      MyFlags	Special flags

  RETURN VALUE
    File descriptor
*/

File my_open(const char *FileName, int Flags, myf MyFlags)
				/* Path-name of file */
				/* Read | write .. */
				/* Special flags */
{
  File fd;

#if !defined(NO_OPEN_3)
  fd = open(FileName, Flags, my_umask);	/* Normal unix */
#else
  fd = open((char *) FileName, Flags);
#endif

  return(my_register_filename(fd, FileName, FILE_BY_OPEN,
				   EE_FILENOTFOUND, MyFlags));
} /* my_open */


/*
  Close a file

  SYNOPSIS
    my_close()
      fd	File sescriptor
      myf	Special Flags

*/

int my_close(File fd, myf MyFlags)
{
  int err;

  pthread_mutex_lock(&THR_LOCK_open);
  do
  {
    err= close(fd);
  } while (err == -1 && errno == EINTR);

  if (err)
  {
    my_errno=errno;
    if (MyFlags & (MY_FAE | MY_WME))
      my_error(EE_BADCLOSE, MYF(ME_BELL+ME_WAITTANG),my_filename(fd),errno);
  }
  if ((uint) fd < my_file_limit && my_file_info[fd].type != UNOPEN)
  {
    free(my_file_info[fd].name);
#if !defined(HAVE_PREAD)
    pthread_mutex_destroy(&my_file_info[fd].mutex);
#endif
    my_file_info[fd].type = UNOPEN;
  }
  my_file_opened--;
  pthread_mutex_unlock(&THR_LOCK_open);
  return(err);
} /* my_close */


/*
  Register file in my_file_info[]
   
  SYNOPSIS
    my_register_filename()
    fd			   File number opened, -1 if error on open
    FileName		   File name
    type_file_type	   How file was created
    error_message_number   Error message number if caller got error (fd == -1)
    MyFlags		   Flags for my_close()

  RETURN
    -1   error
     #   Filenumber

*/

File my_register_filename(File fd, const char *FileName, enum file_type
			  type_of_file, uint32_t error_message_number, myf MyFlags)
{
  if ((int) fd >= 0)
  {
    if ((uint) fd >= my_file_limit)
    {
#if !defined(HAVE_PREAD)
      my_errno= EMFILE;
#else
      thread_safe_increment(my_file_opened,&THR_LOCK_open);
      return(fd);				/* safeguard */
#endif
    }
    else
    {
      pthread_mutex_lock(&THR_LOCK_open);
      if ((my_file_info[fd].name = (char*) my_strdup(FileName,MyFlags)))
      {
        my_file_opened++;
        my_file_total_opened++;
        my_file_info[fd].type = type_of_file;
#if !defined(HAVE_PREAD)
        pthread_mutex_init(&my_file_info[fd].mutex,MY_MUTEX_INIT_FAST);
#endif
        pthread_mutex_unlock(&THR_LOCK_open);
        return(fd);
      }
      pthread_mutex_unlock(&THR_LOCK_open);
      my_errno= ENOMEM;
    }
    (void) my_close(fd, MyFlags);
  }
  else
    my_errno= errno;

  if (MyFlags & (MY_FFNF | MY_FAE | MY_WME))
  {
    if (my_errno == EMFILE)
      error_message_number= EE_OUT_OF_FILERESOURCES;
    my_error(error_message_number, MYF(ME_BELL+ME_WAITTANG),
             FileName, my_errno);
  }
  return(-1);
}


#ifdef EXTRA_DEBUG

void my_print_open_files(void)
{
  if (my_file_opened | my_stream_opened)
  {
    uint32_t i;
    for (i= 0 ; i < my_file_limit ; i++)
    {
      if (my_file_info[i].type != UNOPEN)
      {
        fprintf(stderr, EE(EE_FILE_NOT_CLOSED), my_file_info[i].name, i);
        fputc('\n', stderr);
      }
    }
  }
}

#endif