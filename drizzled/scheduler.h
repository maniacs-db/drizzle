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

#ifndef DRIZZLE_SERVER_SCHEDULER_H
#define DRIZZLE_SERVER_SCHEDULER_H

/*
  Classes for the thread scheduler
*/

/* Forward declarations */
class THD;
struct event;

/* Functions used when manipulating threads */

class scheduler_functions
{
public:
  uint32_t max_threads;
  bool (*init)(void);
  bool (*init_new_connection_thread)(void);
  void (*add_connection)(THD *thd);
  void (*post_kill_notification)(THD *thd);
  bool (*end_thread)(THD *thd, bool cache_thread);
  void (*end)(void);
  scheduler_functions();
};

enum scheduler_types
{
  SCHEDULER_ONE_THREAD_PER_CONNECTION=0,
  SCHEDULER_NO_THREADS,
  SCHEDULER_POOL_OF_THREADS
};

void one_thread_per_connection_scheduler(scheduler_functions* func);
void one_thread_scheduler(scheduler_functions* func);

#define HAVE_POOL_OF_THREADS 1

class thd_scheduler
{
public:
  bool logged_in;
  struct event* io_event;
  LIST list;
  bool thread_attached;  /* Indicates if THD is attached to the OS thread */

  char dbug_explain_buf[256];
  void swap_dbug_explain();

  thd_scheduler();
  ~thd_scheduler();
  thd_scheduler(const thd_scheduler&);
  void operator=(const thd_scheduler&);
  bool init(THD* parent_thd);
  bool thread_attach();
  void thread_detach();
};

void pool_of_threads_scheduler(scheduler_functions* func);

#endif /* DRIZZLE_SERVER_SCHEDULER_H */
