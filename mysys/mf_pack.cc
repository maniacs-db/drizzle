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
#include <mystrings/m_string.h>
#include "my_static.h"
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

static char * expand_tilde(char * *path);

	/* Pack a dirname ; Changes HOME to ~/ and current dev to ./ */
	/* from is a dirname (from dirname() ?) ending with FN_LIBCHAR */
	/* to may be == from */

void pack_dirname(char * to, const char *from)
{
  int getcwd_error= 0;
  size_t d_length,length,buff_length= 0;
  char * start;
  char buff[FN_REFLEN];

  (void) intern_filename(to,from);		/* Change to intern name */

#ifdef FN_DEVCHAR
  if ((start=strrchr(to,FN_DEVCHAR)) != 0)	/* Skip device part */
    start++;
  else
#endif
    start=to;

  if (!(getcwd(buff, FN_REFLEN)))
  {
    getcwd_error= errno;
    buff_length= strlen(buff);
    d_length= (size_t) (start-to);
    if ((start == to ||
	 (buff_length == d_length && !memcmp(buff,start,d_length))) &&
	*start != FN_LIBCHAR && *start)
    {						/* Put current dir before */
      bchange((unsigned char*) to, d_length, (unsigned char*) buff, buff_length, strlen(to)+1);
    }
  }

  if ((d_length= cleanup_dirname(to,to)) != 0)
  {
    length=0;
    if (home_dir)
    {
      length= strlen(home_dir);
      if (home_dir[length-1] == FN_LIBCHAR)
	length--;				/* Don't test last '/' */
    }
    if (length > 1 && length < d_length)
    {						/* test if /xx/yy -> ~/yy */
      if (memcmp(to,home_dir,length) == 0 && to[length] == FN_LIBCHAR)
      {
	to[0]=FN_HOMELIB;			/* Filename begins with ~ */
	(void) strmov_overlapp(to+1,to+length);
      }
    }
    if (! getcwd_error)
    {						/* Test if cwd is ~/... */
      if (length > 1 && length < buff_length)
      {
	if (memcmp(buff,home_dir,length) == 0 && buff[length] == FN_LIBCHAR)
	{
	  buff[0]=FN_HOMELIB;
	  (void) strmov_overlapp(buff+1,buff+length);
	}
      }
      if (is_prefix(to,buff))
      {
	length= strlen(buff);
	if (to[length])
	  (void) strmov_overlapp(to,to+length);	/* Remove everything before */
	else
	{
	  to[0]= FN_CURLIB;			/* Put ./ instead of cwd */
	  to[1]= FN_LIBCHAR;
	  to[2]= '\0';
	}
      }
    }
  }
  return;
} /* pack_dirname */


/*
  remove unwanted chars from dirname

  SYNOPSIS
     cleanup_dirname()
     to		Store result here
     from	Dirname to fix.  May be same as to

  IMPLEMENTATION
  "/../" removes prev dir
  "/~/" removes all before ~
  //" is same as "/", except on Win32 at start of a file
  "/./" is removed
  Unpacks home_dir if "~/.." used
  Unpacks current dir if if "./.." used

  RETURN
    #  length of new name   
*/

size_t cleanup_dirname(register char *to, const char *from)
{
  register size_t length;
  register char * pos;
  register const char * from_ptr;
  register char * start;
  char parent[5],				/* for "FN_PARENTDIR" */
       buff[FN_REFLEN+1],*end_parentdir;
#ifdef BACKSLASH_MBTAIL
  CHARSET_INFO *fs= fs_character_set();
#endif

  start=buff;
  from_ptr= from;
#ifdef FN_DEVCHAR
  if ((pos=strrchr(from_ptr,FN_DEVCHAR)) != 0)
  {						/* Skip device part */
    length=(size_t) (pos-from_ptr)+1;
    start=my_stpncpy(buff,from_ptr,length); from_ptr+=length;
  }
#endif

  parent[0]=FN_LIBCHAR;
  length=(size_t) (my_stpcpy(parent+1,FN_PARENTDIR)-parent);
  for (pos=start ; (*pos= *from_ptr++) != 0 ; pos++)
  {
#ifdef BACKSLASH_MBTAIL
    uint32_t l;
    if (use_mb(fs) && (l= my_ismbchar(fs, from_ptr - 1, from_ptr + 2)))
    {
      for (l-- ; l ; *++pos= *from_ptr++, l--);
      start= pos + 1; /* Don't look inside multi-byte char */
      continue;
    }
#endif
    if (*pos == '/')
      *pos = FN_LIBCHAR;
    if (*pos == FN_LIBCHAR)
    {
      if ((size_t) (pos-start) > length &&
          memcmp(pos-length,parent,length) == 0)
      {						/* If .../../; skip prev */
	pos-=length;
	if (pos != start)
	{					 /* not /../ */
	  pos--;
	  if (*pos == FN_HOMELIB && (pos == start || pos[-1] == FN_LIBCHAR))
	  {
	    if (!home_dir)
	    {
	      pos+=length+1;			/* Don't unpack ~/.. */
	      continue;
	    }
	    pos=my_stpcpy(buff,home_dir)-1;	/* Unpacks ~/.. */
	    if (*pos == FN_LIBCHAR)
	      pos--;				/* home ended with '/' */
	  }
	  if (*pos == FN_CURLIB && (pos == start || pos[-1] == FN_LIBCHAR))
	  {
	    if (getcwd(curr_dir,FN_REFLEN))
	    {
	      pos+=length+1;			/* Don't unpack ./.. */
	      continue;
	    }
	    pos=my_stpcpy(buff,curr_dir)-1;	/* Unpacks ./.. */
	    if (*pos == FN_LIBCHAR)
	      pos--;				/* home ended with '/' */
	  }
	  end_parentdir=pos;
	  while (pos >= start && *pos != FN_LIBCHAR)	/* remove prev dir */
	    pos--;
	  if (pos[1] == FN_HOMELIB || memcmp(pos,parent,length) == 0)
	  {					/* Don't remove ~user/ */
	    pos=my_stpcpy(end_parentdir+1,parent);
	    *pos=FN_LIBCHAR;
	    continue;
	  }
	}
      }
      else if ((size_t) (pos-start) == length-1 &&
	       !memcmp(start,parent+1,length-1))
	start=pos;				/* Starts with "../" */
      else if (pos-start > 0 && pos[-1] == FN_LIBCHAR)
      {
#ifdef FN_NETWORK_DRIVES
	if (pos-start != 1)
#endif
	  pos--;			/* Remove dupplicate '/' */
      }
      else if (pos-start > 1 && pos[-1] == FN_CURLIB && pos[-2] == FN_LIBCHAR)
	pos-=2;					/* Skip /./ */
      else if (pos > buff+1 && pos[-1] == FN_HOMELIB && pos[-2] == FN_LIBCHAR)
      {					/* Found ..../~/  */
	buff[0]=FN_HOMELIB;
	buff[1]=FN_LIBCHAR;
	start=buff; pos=buff+1;
      }
    }
  }
  (void) my_stpcpy(to,buff);
  return((size_t) (pos-buff));
} /* cleanup_dirname */


/*
  On system where you don't have symbolic links, the following
  code will allow you to create a file: 
  directory-name.sym that should contain the real path
  to the directory.  This will be used if the directory name
  doesn't exists
*/


bool my_use_symdir=0;	/* Set this if you want to use symdirs */

#ifdef USE_SYMDIR
void symdirget(char *dir)
{
  char buff[FN_REFLEN];
  char *pos= strchr(dir, '\0');
  if (dir[0] && pos[-1] != FN_DEVCHAR && my_access(dir, F_OK))
  {
    File file;
    size_t length;
    char temp= *(--pos);            /* May be "/" or "\" */
    my_stpcpy(pos,".sym");
    file= my_open(dir, O_RDONLY, MYF(0));
    *pos++=temp; *pos=0;	  /* Restore old filename */
    if (file >= 0)
    {
      if ((length= my_read(file, buff, sizeof(buff), MYF(0))) > 0)
      {
        for (pos= buff + length ;
             pos > buff && (iscntrl(pos[-1]) || isspace(pos[-1])) ;
             pos --);

        /* Ensure that the symlink ends with the directory symbol */
        if (pos == buff || pos[-1] != FN_LIBCHAR)
          *pos++=FN_LIBCHAR;

        strncpy(dir,buff, FN_REFLEN-1);
      }
      my_close(file, MYF(0));
    }
  }
}
#endif /* USE_SYMDIR */


/*
  Fixes a directroy name so that can be used by open()

  SYNOPSIS
    unpack_dirname()
    to			result-buffer, FN_REFLEN characters. may be == from
    from		'Packed' directory name (may contain ~)

 IMPLEMENTATION
  Make that last char of to is '/' if from not empty and
  from doesn't end in FN_DEVCHAR
  Uses cleanup_dirname and changes ~/.. to home_dir/..

  Changes a UNIX filename to system filename (replaces / with \ on windows)

  RETURN
   Length of new directory name (= length of to)
*/

size_t unpack_dirname(char * to, const char *from)
{
  size_t length, h_length;
  char buff[FN_REFLEN+1+4],*suffix,*tilde_expansion;

  (void) intern_filename(buff,from);	    /* Change to intern name */
  length= strlen(buff);                     /* Fix that '/' is last */
  if (length &&
#ifdef FN_DEVCHAR
      buff[length-1] != FN_DEVCHAR &&
#endif
      buff[length-1] != FN_LIBCHAR && buff[length-1] != '/')
  {
    buff[length]=FN_LIBCHAR;
    buff[length+1]= '\0';
  }

  length=cleanup_dirname(buff,buff);
  if (buff[0] == FN_HOMELIB)
  {
    suffix=buff+1; tilde_expansion=expand_tilde(&suffix);
    if (tilde_expansion)
    {
      length-= (size_t) (suffix-buff)-1;
      if (length+(h_length= strlen(tilde_expansion)) <= FN_REFLEN)
      {
	if (tilde_expansion[h_length-1] == FN_LIBCHAR)
	  h_length--;
	if (buff+h_length < suffix)
          memmove(buff+h_length, suffix, length);
	else
	  bmove_upp((unsigned char*) buff+h_length+length, (unsigned char*) suffix+length, length);
        memmove(buff, tilde_expansion, h_length);
      }
    }
  }
#ifdef USE_SYMDIR
  if (my_use_symdir)
    symdirget(buff);
#endif
  return(system_filename(to,buff));	/* Fix for open */
} /* unpack_dirname */


	/* Expand tilde to home or user-directory */
	/* Path is reset to point at FN_LIBCHAR after ~xxx */

static char * expand_tilde(char * *path)
{
  if (path[0][0] == FN_LIBCHAR)
    return home_dir;			/* ~/ expanded to home */
#ifdef HAVE_GETPWNAM
  {
    char *str,save;
    struct passwd *user_entry;

    if (!(str=strchr(*path,FN_LIBCHAR)))
      str= strchr(*path, '\0');
    save= *str; *str= '\0';
    user_entry=getpwnam(*path);
    *str=save;
    endpwent();
    if (user_entry)
    {
      *path=str;
      return user_entry->pw_dir;
    }
  }
#endif
  return NULL;
}


/*
  Fix filename so it can be used by open, create

  SYNOPSIS
    unpack_filename()
    to		Store result here. Must be at least of size FN_REFLEN.
    from	Filename in unix format (with ~)

  RETURN
    # length of to

  NOTES
    to may be == from
    ~ will only be expanded if total length < FN_REFLEN
*/


size_t unpack_filename(char * to, const char *from)
{
  size_t length, n_length, buff_length;
  char buff[FN_REFLEN];

  length=dirname_part(buff, from, &buff_length);/* copy & convert dirname */
  n_length=unpack_dirname(buff,buff);
  if (n_length+strlen(from+length) < FN_REFLEN)
  {
    (void) my_stpcpy(buff+n_length,from+length);
    length= system_filename(to,buff);		/* Fix to usably filename */
  }
  else
    length= system_filename(to,from);		/* Fix to usably filename */
  return(length);
} /* unpack_filename */


	/* Convert filename (unix standard) to system standard */
	/* Used before system command's like open(), create() .. */
	/* Returns used length of to; total length should be FN_REFLEN */

size_t system_filename(char * to, const char *from)
{
#ifndef FN_C_BEFORE_DIR
  return strlen(strncpy(to,from,FN_REFLEN-1));
#else	/* VMS */

	/* change 'dev:lib/xxx' to 'dev:[lib]xxx' */
	/* change 'dev:xxx' to 'dev:xxx' */
	/* change './xxx' to 'xxx' */
	/* change './lib/' or lib/ to '[.lib]' */
	/* change '/x/y/z to '[x.y]x' */
	/* change 'dev:/x' to 'dev:[000000]x' */

  int libchar_found;
  size_t length;
  char * to_pos,from_pos,pos;
  char buff[FN_REFLEN];

  libchar_found=0;
  (void) my_stpcpy(buff,from);			 /* If to == from */
  from_pos= buff;
  if ((pos=strrchr(from_pos,FN_DEVCHAR)))	/* Skip device part */
  {
    pos++;
    to_pos=my_stpncpy(to,from_pos,(size_t) (pos-from_pos));
    from_pos=pos;
  }
  else
    to_pos=to;

  if (from_pos[0] == FN_CURLIB && from_pos[1] == FN_LIBCHAR)
    from_pos+=2;				/* Skip './' */
  if (strchr(from_pos,FN_LIBCHAR))
  {
    *(to_pos++) = FN_C_BEFORE_DIR;
    if (strstr(from_pos,FN_ROOTDIR) == from_pos)
    {
      from_pos+=strlen(FN_ROOTDIR);		/* Actually +1 but... */
      if (! strchr(from_pos,FN_LIBCHAR))
      {						/* No dir, use [000000] */
	to_pos=my_stpcpy(to_pos,FN_C_ROOT_DIR);
	libchar_found++;
      }
    }
    else
      *(to_pos++)=FN_C_DIR_SEP;			/* '.' gives current dir */

    while ((pos=strchr(from_pos,FN_LIBCHAR)))
    {
      if (libchar_found++)
	*(to_pos++)=FN_C_DIR_SEP;		/* Add '.' between dirs */
      if (strstr(from_pos,FN_PARENTDIR) == from_pos &&
	  from_pos+strlen(FN_PARENTDIR) == pos)
	to_pos=my_stpcpy(to_pos,FN_C_PARENT_DIR);	/* Found '../' */
      else
	to_pos=my_stpncpy(to_pos,from_pos,(size_t) (pos-from_pos));
      from_pos=pos+1;
    }
    *(to_pos++)=FN_C_AFTER_DIR;
  }
  length= (size_t) (my_stpcpy(to_pos,from_pos)-to);
  return(length);
#endif
} /* system_filename */


	/* Fix a filename to intern (UNIX format) */

char *intern_filename(char *to, const char *from)
{
  size_t length, to_length;
  char buff[FN_REFLEN];
  if (from == to)
  {						/* Dirname may destroy from */
    my_stpcpy(buff,from);
    from=buff;
  }
  length= dirname_part(to, from, &to_length);	/* Copy dirname & fix chars */
  (void) my_stpcpy(to + to_length,from+length);
  return (to);
} /* intern_filename */