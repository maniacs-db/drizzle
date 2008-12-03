/******************************************************
This file contains functions that need to be added to
MySQL code but have not been added yet.

Whenever you add a function here submit a MySQL bug
report (feature request) with the implementation. Then
write the bug number in the comment before the
function in this file.

When MySQL commits the function it can be deleted from
here. In a perfect world this file exists but is empty.

(c) 2007 Innobase Oy

Created November 07, 2007 Vasil Dimov
*******************************************************/

#ifndef MYSQL_SERVER
#define MYSQL_SERVER
#endif /* MYSQL_SERVER */

#if defined(BUILD_DRIZZLE)
# include <drizzled/common_includes.h>
#else
# include <mysql_priv.h>
#endif

#include "mysql_addons.h"
#include "univ.i"

/***********************************************************************
Retrieve Session::thread_id
http://bugs.mysql.com/30930 */
extern "C" UNIV_INTERN
unsigned long
ib_thd_get_thread_id(
/*=================*/
				/* out: Session::thread_id */
	const void*	session)	/* in: Session */
{
  return(session_get_thread_id((const Session*)session));
}

/* http://bugs.mysql.com/40360 */
/* http://lists.mysql.com/commits/57450 */
/**
  See if the binary log is engaged for a thread, i.e., open and
  LOG_BIN is set.

  @return @c true if the binlog is active, @c false otherwise.
*/
my_bool ib_bin_log_is_engaged(const MYSQL_THD thd)
{
  return mysql_bin_log.is_open() && (thd->options & OPTION_BIN_LOG);
}
