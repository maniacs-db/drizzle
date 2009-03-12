/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems, Inc.
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

/*
** Ask for a password from tty
** This is an own file to avoid conflicts with curses
*/
#include <drizzled/global.h>
#include "libdrizzle.h"
#include <libdrizzle/get_password.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>

#include <sys/ioctl.h>
#ifdef HAVE_TERMIOS_H				/* For tty-password */
# include	<termios.h>
#  define TERMIO	struct termios
#else
#  ifdef HAVE_TERMIO_H				/* For tty-password */
#    include	<termio.h>
#    define TERMIO	struct termio
#  else
#    include	<sgtty.h>
#    define TERMIO	struct sgttyb
#  endif
#endif

/*
  Can't use fgets, because readline will get confused
  length is max number of chars in to, not counting \0
  to will not include the eol characters.
*/

static void get_password(char *to, uint32_t length,int fd, bool echo)
{
  char *pos=to,*end=to+length;

  for (;;)
  {
    char tmp;
    if (read(fd,&tmp,1) != 1)
      break;
    if (tmp == '\b' || (int) tmp == 127)
    {
      if (pos != to)
      {
	if (echo)
	{
	  fputs("\b \b",stdout);
	  fflush(stdout);
	}
	pos--;
	continue;
      }
    }
    if (tmp == '\n' || tmp == '\r' || tmp == 3)
      break;
    if (iscntrl(tmp) || pos == end)
      continue;
    if (echo)
    {
      fputc('*',stdout);
      fflush(stdout);
    }
    *(pos++) = tmp;
  }
  while (pos != to && isspace(pos[-1]) == ' ')
    pos--;					/* Allow dummy space at end */
  *pos=0;
  return;
}


char *get_tty_password(const char *opt_message)
{
  TERMIO org,tmp;
  char buff[80];

  if (isatty(fileno(stdout)))
  {
    fputs(opt_message ? opt_message : "Enter password: ",stdout);
    fflush(stdout);
  }
#  if defined(HAVE_TERMIOS_H)
  tcgetattr(fileno(stdin), &org);
  tmp = org;
  tmp.c_lflag &= ~(ECHO | ISIG | ICANON);
  tmp.c_cc[VMIN] = 1;
  tmp.c_cc[VTIME] = 0;
  tcsetattr(fileno(stdin), TCSADRAIN, &tmp);
  get_password(buff, sizeof(buff)-1, fileno(stdin), isatty(fileno(stdout)));
  tcsetattr(fileno(stdin), TCSADRAIN, &org);
#  elif defined(HAVE_TERMIO_H)
  ioctl(fileno(stdin), (int) TCGETA, &org);
  tmp=org;
  tmp.c_lflag &= ~(ECHO | ISIG | ICANON);
  tmp.c_cc[VMIN] = 1;
  tmp.c_cc[VTIME]= 0;
  ioctl(fileno(stdin),(int) TCSETA, &tmp);
  get_password(buff,sizeof(buff)-1,fileno(stdin),isatty(fileno(stdout)));
  ioctl(fileno(stdin),(int) TCSETA, &org);
#  else
  gtty(fileno(stdin), &org);
  tmp=org;
  tmp.sg_flags &= ~ECHO;
  tmp.sg_flags |= RAW;
  stty(fileno(stdin), &tmp);
  get_password(buff,sizeof(buff)-1,fileno(stdin),isatty(fileno(stdout)));
  stty(fileno(stdin), &org);
#  endif
  if (isatty(fileno(stdout)))
    fputc('\n',stdout);

  return strdup(buff);
}