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

#include "config.h"

#include <signal.h>

#include "drizzled/signal_handler.h"
#include "drizzled/drizzled.h"
#include "drizzled/session.h"
#include "drizzled/internal/my_sys.h"
#include "drizzled/probes.h"
#include "drizzled/plugin.h"
#include "drizzled/plugin/scheduler.h"

#ifdef __GNUC__
#ifdef HAVE_BACKTRACE
#include <execinfo.h>
#include <cxxabi.h>
#endif // HAVE_BACKTRACE
#endif // __GNUC__

using namespace drizzled;

static uint32_t killed_threads;
static bool segfaulted= false;

/*
 * We declare these extern "C" because they are passed to system callback functions
 * and Sun Studio does not like it when those don't have C linkage. We prefix them
 * because extern "C"-ing something effectively removes the namespace from the
 * linker symbols, meaning they would be exporting symbols like "print_signal_warning
 */
extern "C"
{

void drizzled_print_signal_warning(int sig)
{
  if (global_system_variables.log_warnings)
    errmsg_printf(ERRMSG_LVL_WARN, _("Got signal %d from thread %"PRIu64),
                  sig, global_thread_id);
#ifndef HAVE_BSD_SIGNALS
  sigset_t set;
  sigemptyset(&set);

  struct sigaction sa;
  sa.sa_handler= drizzled_print_signal_warning;
  sa.sa_mask= set;
  sa.sa_flags= 0;
  sigaction(sig, &sa, NULL);  /* int. thread system calls */
#endif
  if (sig == SIGALRM)
    alarm(2);					/* reschedule alarm */
}

/** Called when a thread is aborted. */
void drizzled_end_thread_signal(int )
{
  Session *session=current_session;
  if (session)
  {
    killed_threads++;
    session->scheduler->killSessionNow(session);
    DRIZZLE_CONNECTION_DONE(session->thread_id);
  }
  return;
}

static void write_core(int sig)
{
  signal(sig, SIG_DFL);
#ifdef HAVE_gcov
  /*
    For GCOV build, crashing will prevent the writing of code coverage
    information from this process, causing gcov output to be incomplete.
    So we force the writing of coverage information here before terminating.
  */
  extern void __gcov_flush(void);
  __gcov_flush();
#endif
  pthread_kill(pthread_self(), sig);
#if defined(P_MYID) && !defined(SCO)
  /* On Solaris, the above kill is not enough */
  sigsend(P_PID,P_MYID,sig);
#endif
}

#define BACKTRACE_STACK_SIZE 50
void drizzled_handle_segfault(int sig)
{
  time_t curr_time;
  struct tm tm;

  /*
    Strictly speaking, one needs a mutex here
    but since we have got SIGSEGV already, things are a mess
    so not having the mutex is not as bad as possibly using a buggy
    mutex - so we keep things simple
  */
  if (segfaulted)
  {
    fprintf(stderr, _("Fatal signal %d while backtracing\n"), sig);
    exit(1);
  }

  segfaulted= true;

  curr_time= time(NULL);
  if(curr_time == (time_t)-1)
  {
    fprintf(stderr, _("Fatal: time() call failed\n"));
    exit(1);
  }

  localtime_r(&curr_time, &tm);
  
  fprintf(stderr,_("%02d%02d%02d %2d:%02d:%02d - drizzled got signal %d;\n"
          "This could be because you hit a bug. It is also possible that "
          "this binary\n or one of the libraries it was linked against is "
          "corrupt, improperly built,\n or misconfigured. This error can "
          "also be caused by malfunctioning hardware.\n"),
          tm.tm_year % 100, tm.tm_mon+1, tm.tm_mday,
          tm.tm_hour, tm.tm_min, tm.tm_sec,
          sig);
  fprintf(stderr, _("We will try our best to scrape up some info that "
                    "will hopefully help diagnose\n"
                    "the problem, but since we have already crashed, "
                    "something is definitely wrong\nand this may fail.\n\n"));
  fprintf(stderr, "read_buffer_size=%ld\n", (long) global_system_variables.read_buff_size);
  fprintf(stderr, "max_used_connections=%"PRIu64"\n", current_global_counters.max_used_connections);
  fprintf(stderr, "connection_count=%u\n", uint32_t(connection_count));
  fprintf(stderr, _("It is possible that drizzled could use up to \n"
                    "(read_buffer_size + sort_buffer_size)*thread_count\n"
                    "bytes of memory\n"
                    "Hope that's ok; if not, decrease some variables in the "
                    "equation.\n\n"));

#ifdef __GNUC__
#ifdef HAVE_BACKTRACE
  {
    void *array[BACKTRACE_STACK_SIZE];
    size_t size;
    char **strings;

    size= backtrace(array, BACKTRACE_STACK_SIZE);
    strings= backtrace_symbols(array, size);

    std::cerr << "Number of stack frames obtained: " << size <<  std::endl;

    for (size_t x= 1; x < size; x++) 
    {
      size_t sz= 200;
      char *function= (char *)malloc(sz);
      char *begin= 0;
      char *end= 0;

      for (char *j = strings[x]; *j; ++j)
      {
        if (*j == '(') {
          begin = j;
        }
        else if (*j == '+') {
          end = j;
        }
      }
      if (begin && end)
      {
        begin++;
        *end= NULL;

        int status;
        char *ret = abi::__cxa_demangle(begin, function, &sz, &status);
        if (ret) 
        {
          function= ret;
        }
        else
        {
          std::strncpy(function, begin, sz);
          std::strncat(function, "()", sz);
          function[sz-1] = NULL;
        }
        std::cerr << function << std::endl;
      }
      else
      {
        std::cerr << strings[x] << std::endl;
      }
      free(function);
    }


    free (strings);
  }
#endif // HAVE_BACKTRACE
#endif // __GNUC__

  write_core(sig);

  exit(1);
}

} /* extern "C" */
