/* Copyright (C) 2004 MySQL AB

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

/**
  @file

  @brief
  Functions for discover of frm file from handler
*/
#include <drizzled/server_includes.h>

extern char reg_ext[FN_EXTLEN];

/**
  Read the contents of a .frm file.

  frmdata and len are set to 0 on error.

  @param name           path to table-file "db/name"
  @param frmdata        frm data
  @param len            length of the read frmdata

  @retval
    0	ok
  @retval
    1	Could not open file
  @retval
    2    Could not stat file
  @retval
    3    Could not allocate data for read.  Could not read file
*/
int readfrm(const char *name, unsigned char **frmdata, size_t *len)
{
  int    error;
  char	 index_file[FN_REFLEN];
  File	 file;
  size_t read_len;
  unsigned char *read_data;
  struct stat state;  
  
  *frmdata= NULL;      // In case of errors
  *len= 0;
  error= 1;
  if ((file=my_open(fn_format(index_file,name,"",reg_ext,
                              MY_UNPACK_FILENAME|MY_APPEND_EXT),
		    O_RDONLY,
		    MYF(0))) < 0)  
    goto err_end; 
  
  // Get length of file
  error= 2;
  if (fstat(file, &state))
    goto err;
  read_len= state.st_size;  

  // Read whole frm file
  error= 3;
  read_data= 0;                                 // Nothing to free
  if (read_string(file, &read_data, read_len))
    goto err;

  // Setup return data
  *frmdata= (unsigned char*) read_data;
  *len= read_len;
  error= 0;
  
 err:
  if (file > 0)
    my_close(file,MYF(MY_WME));
  
 err_end:		      /* Here when no file */
  return(error);
} /* readfrm */


/*
  Write the content of a frm data pointer 
  to a frm file.

  @param name           path to table-file "db/name"
  @param frmdata        frm data
  @param len            length of the frmdata

  @retval
    0	ok
  @retval
    2    Could not write file
*/
int writefrm(const char *name, const unsigned char *frmdata, size_t len)
{
  File file;
  char	 index_file[FN_REFLEN];
  int error;

  error= 0;
  if ((file=my_create(fn_format(index_file,name,"",reg_ext,
                      MY_UNPACK_FILENAME|MY_APPEND_EXT),
		      CREATE_MODE,O_RDWR | O_TRUNC,MYF(MY_WME))) >= 0)
  {
    if (my_write(file, frmdata, len,MYF(MY_WME | MY_NABP)))
      error= 2;
    my_close(file,MYF(0));
  }
  return(error);
} /* writefrm */