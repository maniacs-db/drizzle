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

/* Defines to make different thread packages compatible */

#ifndef _my_pthread_h
#define _my_pthread_h

#include <stdint.h>
#include <unistd.h>
#include <signal.h>

#if !defined(__cplusplus)
# include <stdbool.h>
#endif

#ifndef ETIME
#define ETIME ETIMEDOUT				/* For FreeBSD */
#endif

#ifdef  __cplusplus
#define EXTERNC extern "C"
extern "C" {
#else
#define EXTERNC
#endif /* __cplusplus */ 

#include <pthread.h>
#ifndef _REENTRANT
#define _REENTRANT
#endif
#ifdef HAVE_SCHED_H
#include <sched.h>
#endif
#ifdef HAVE_SYNCH_H
#include <synch.h>
#endif

#define pthread_key(T,V) pthread_key_t V
#define pthread_detach_this_thread()
#define pthread_handler_t void *
typedef void *(* pthread_handler)(void *);

/* Test first for RTS or FSU threads */

#if defined(PTHREAD_SCOPE_GLOBAL) && !defined(PTHREAD_SCOPE_SYSTEM)
#define HAVE_rts_threads
extern int my_pthread_create_detached;
#define pthread_sigmask(A,B,C) sigprocmask((A),(B),(C))
#define PTHREAD_CREATE_DETACHED &my_pthread_create_detached
#define PTHREAD_SCOPE_SYSTEM  PTHREAD_SCOPE_GLOBAL
#define PTHREAD_SCOPE_PROCESS PTHREAD_SCOPE_LOCAL
#define USE_ALARM_THREAD
#endif /* defined(PTHREAD_SCOPE_GLOBAL) && !defined(PTHREAD_SCOPE_SYSTEM) */

#ifndef HAVE_NONPOSIX_SIGWAIT
#define my_sigwait(A,B) sigwait((A),(B))
#else
int my_sigwait(const sigset_t *set,int *sig);
#endif

#ifdef HAVE_NONPOSIX_PTHREAD_MUTEX_INIT
#define pthread_mutex_init(a,b) my_pthread_mutex_init((a),(b))
extern int my_pthread_mutex_init(pthread_mutex_t *mp,
				 const pthread_mutexattr_t *attr);
#define pthread_cond_init(a,b) my_pthread_cond_init((a),(b))
extern int my_pthread_cond_init(pthread_cond_t *mp,
				const pthread_condattr_t *attr);
#endif /* HAVE_NONPOSIX_PTHREAD_MUTEX_INIT */

#if defined(HAVE_SIGTHREADMASK) && !defined(HAVE_PTHREAD_SIGMASK)
#define pthread_sigmask(A,B,C) sigthreadmask((A),(B),(C))
#endif

#if !defined(HAVE_SIGWAIT) && !defined(HAVE_rts_threads) && !defined(sigwait) && !defined(alpha_linux_port) && !defined(HAVE_NONPOSIX_SIGWAIT)
int sigwait(sigset_t *setp, int *sigp);		/* Use our implemention */
#endif


/*
  We define my_sigset() and use that instead of the system sigset() so that
  we can favor an implementation based on sigaction(). On some systems, such
  as Mac OS X, sigset() results in flags such as SA_RESTART being set, and
  we want to make sure that no such flags are set.
*/
#if defined(HAVE_SIGACTION) && !defined(my_sigset)
#define my_sigset(A,B) do { struct sigaction l_s; sigset_t l_set; int l_rc; \
                            assert((A) != 0);                          \
                            sigemptyset(&l_set);                            \
                            l_s.sa_handler = (B);                           \
                            l_s.sa_mask   = l_set;                          \
                            l_s.sa_flags   = 0;                             \
                            l_rc= sigaction((A), &l_s, (struct sigaction *) NULL);\
                            assert(l_rc == 0);                         \
                          } while (0)
#elif defined(HAVE_SIGSET) && !defined(my_sigset)
#define my_sigset(A,B) sigset((A),(B))
#elif !defined(my_sigset)
#define my_sigset(A,B) signal((A),(B))
#endif

#ifndef my_pthread_attr_setprio
#ifdef HAVE_PTHREAD_ATTR_SETPRIO
#define my_pthread_attr_setprio(A,B) pthread_attr_setprio((A),(B))
#else
extern void my_pthread_attr_setprio(pthread_attr_t *attr, int priority);
#endif
#endif

#if !defined(HAVE_PTHREAD_ATTR_SETSCOPE)
#define pthread_attr_setscope(A,B)
#endif

#if defined(HAVE_BROKEN_PTHREAD_COND_TIMEDWAIT)
extern int my_pthread_cond_timedwait(pthread_cond_t *cond,
				     pthread_mutex_t *mutex,
				     struct timespec *abstime);
#define pthread_cond_timedwait(A,B,C) my_pthread_cond_timedwait((A),(B),(C))
#endif

/* FSU THREADS */
#if !defined(HAVE_PTHREAD_KEY_DELETE) && !defined(pthread_key_delete)
#define pthread_key_delete(A) pthread_dummy(0)
#endif

#ifdef HAVE_CTHREADS_WRAPPER			/* For MacOSX */
#define pthread_cond_destroy(A) pthread_dummy(0)
#define pthread_mutex_destroy(A) pthread_dummy(0)
#define pthread_attr_delete(A) pthread_dummy(0)
#define pthread_condattr_delete(A) pthread_dummy(0)
#define pthread_attr_setstacksize(A,B) pthread_dummy(0)
#define pthread_equal(A,B) ((A) == (B))
#define pthread_cond_timedwait(a,b,c) pthread_cond_wait((a),(b))
#define pthread_attr_init(A) pthread_attr_create(A)
#define pthread_attr_destroy(A) pthread_attr_delete(A)
#define pthread_attr_setdetachstate(A,B) pthread_dummy(0)
#define pthread_create(A,B,C,D) pthread_create((A),*(B),(C),(D))
#define pthread_sigmask(A,B,C) sigprocmask((A),(B),(C))
#define pthread_kill(A,B) pthread_dummy((A) ? 0 : ESRCH)
#undef	pthread_detach_this_thread
#define pthread_detach_this_thread() { pthread_t tmp=pthread_self() ; pthread_detach(&tmp); }
#endif

#ifdef HAVE_DARWIN5_THREADS
#define pthread_sigmask(A,B,C) sigprocmask((A),(B),(C))
#define pthread_kill(A,B) pthread_dummy((A) ? 0 : ESRCH)
#define pthread_condattr_init(A) pthread_dummy(0)
#define pthread_condattr_destroy(A) pthread_dummy(0)
#undef	pthread_detach_this_thread
#define pthread_detach_this_thread() { pthread_t tmp=pthread_self() ; pthread_detach(tmp); }
#endif

#if (defined(HAVE_PTHREAD_ATTR_CREATE) && !defined(HAVE_SIGWAIT))
/* This is set on AIX_3_2 and Siemens unix (and DEC OSF/1 3.2 too) */
#define pthread_key_create(A,B) \
		pthread_keycreate(A,(B) ?\
				  (pthread_destructor_t) (B) :\
				  (pthread_destructor_t) pthread_dummy)
#define pthread_attr_init(A) pthread_attr_create(A)
#define pthread_attr_destroy(A) pthread_attr_delete(A)
#define pthread_attr_setdetachstate(A,B) pthread_dummy(0)
#define pthread_create(A,B,C,D) pthread_create((A),*(B),(C),(D))
#ifndef pthread_sigmask
#define pthread_sigmask(A,B,C) sigprocmask((A),(B),(C))
#endif
#define pthread_kill(A,B) pthread_dummy((A) ? 0 : ESRCH)
#undef	pthread_detach_this_thread
#define pthread_detach_this_thread() { pthread_t tmp=pthread_self() ; pthread_detach(&tmp); }
#else
#define HAVE_PTHREAD_KILL
#endif

#if defined(HAVE_POSIX1003_4a_MUTEX) && !defined(DONT_REMAP_PTHREAD_FUNCTIONS)
#undef pthread_mutex_trylock
#define pthread_mutex_trylock(a) my_pthread_mutex_trylock((a))
int my_pthread_mutex_trylock(pthread_mutex_t *mutex);
#endif

#if !defined(HAVE_PTHREAD_YIELD_ONE_ARG) && !defined(HAVE_PTHREAD_YIELD_ZERO_ARG)
/* no pthread_yield() available */
#ifdef HAVE_SCHED_YIELD
#define pthread_yield() sched_yield()
#elif defined(HAVE_PTHREAD_YIELD_NP) /* can be Mac OS X */
#define pthread_yield() pthread_yield_np()
#endif
#endif

/*
  The defines set_timespec and set_timespec_nsec should be used
  for calculating an absolute time at which
  pthread_cond_timedwait should timeout
*/
#ifdef HAVE_TIMESPEC_TS_SEC
#ifndef set_timespec
#define set_timespec(ABSTIME,SEC) \
{ \
  (ABSTIME).ts_sec=time(0) + (time_t) (SEC); \
  (ABSTIME).ts_nsec=0; \
}
#endif /* !set_timespec */
#ifndef set_timespec_nsec
#define set_timespec_nsec(ABSTIME,NSEC) \
{ \
  uint64_t now= my_getsystime() + (NSEC/100); \
  (ABSTIME).ts_sec=  (now / 10000000UL); \
  (ABSTIME).ts_nsec= (now % 10000000UL * 100 + ((NSEC) % 100)); \
}
#endif /* !set_timespec_nsec */
#else
#ifndef set_timespec
#define set_timespec(ABSTIME,SEC) \
{\
  struct timeval tv;\
  gettimeofday(&tv,0);\
  (ABSTIME).tv_sec=tv.tv_sec+(time_t) (SEC);\
  (ABSTIME).tv_nsec=tv.tv_usec*1000;\
}
#endif /* !set_timespec */
#ifndef set_timespec_nsec
#define set_timespec_nsec(ABSTIME,NSEC) \
{\
  uint64_t now= my_getsystime() + (NSEC/100); \
  (ABSTIME).tv_sec=  (time_t) (now / 10000000UL);                  \
  (ABSTIME).tv_nsec= (long) (now % 10000000UL * 100 + ((NSEC) % 100)); \
}
#endif /* !set_timespec_nsec */
#endif /* HAVE_TIMESPEC_TS_SEC */

	/* safe_mutex adds checking to mutex for easier debugging */

typedef struct st_safe_mutex_t
{
  pthread_mutex_t global,mutex;
  const char *file;
  uint32_t line,count;
  pthread_t thread;
} safe_mutex_t;

int safe_mutex_init(safe_mutex_t *mp, const pthread_mutexattr_t *attr,
                    const char *file, uint32_t line);
int safe_mutex_lock(safe_mutex_t *mp, bool try_lock, const char *file, uint32_t line);
int safe_mutex_unlock(safe_mutex_t *mp,const char *file, uint32_t line);
int safe_mutex_destroy(safe_mutex_t *mp,const char *file, uint32_t line);
int safe_cond_wait(pthread_cond_t *cond, safe_mutex_t *mp,const char *file,
		   uint32_t line);
int safe_cond_timedwait(pthread_cond_t *cond, safe_mutex_t *mp,
			struct timespec *abstime, const char *file, uint32_t line);
void safe_mutex_global_init(void);
void safe_mutex_end(void);

	/* Wrappers if safe mutex is actually used */
#define safe_mutex_assert_owner(mp)
#define safe_mutex_assert_not_owner(mp)

#if defined(MY_PTHREAD_FASTMUTEX)
typedef struct st_my_pthread_fastmutex_t
{
  pthread_mutex_t mutex;
  uint32_t spins;
} my_pthread_fastmutex_t;
void fastmutex_global_init(void);

int my_pthread_fastmutex_init(my_pthread_fastmutex_t *mp, 
                              const pthread_mutexattr_t *attr);
int my_pthread_fastmutex_lock(my_pthread_fastmutex_t *mp);

#undef pthread_mutex_init
#undef pthread_mutex_lock
#undef pthread_mutex_unlock
#undef pthread_mutex_destroy
#undef pthread_mutex_wait
#undef pthread_mutex_timedwait
#undef pthread_mutex_t
#undef pthread_cond_wait
#undef pthread_cond_timedwait
#undef pthread_mutex_trylock
#define pthread_mutex_init(A,B) my_pthread_fastmutex_init((A),(B))
#define pthread_mutex_lock(A) my_pthread_fastmutex_lock(A)
#define pthread_mutex_unlock(A) pthread_mutex_unlock(&(A)->mutex)
#define pthread_mutex_destroy(A) pthread_mutex_destroy(&(A)->mutex)
#define pthread_cond_wait(A,B) pthread_cond_wait((A),&(B)->mutex)
#define pthread_cond_timedwait(A,B,C) pthread_cond_timedwait((A),&(B)->mutex,(C))
#define pthread_mutex_trylock(A) pthread_mutex_trylock(&(A)->mutex)
#define pthread_mutex_t my_pthread_fastmutex_t
#endif /* defined(MY_PTHREAD_FASTMUTEX) */

	/* READ-WRITE thread locking */

#ifdef HAVE_BROKEN_RWLOCK			/* For OpenUnix */
#undef HAVE_PTHREAD_RWLOCK_RDLOCK
#undef HAVE_RWLOCK_INIT
#undef HAVE_RWLOCK_T
#endif

#if defined(USE_MUTEX_INSTEAD_OF_RW_LOCKS)
/* use these defs for simple mutex locking */
#define rw_lock_t pthread_mutex_t
#define my_rwlock_init(A,B) pthread_mutex_init((A),(B))
#define rw_rdlock(A) pthread_mutex_lock((A))
#define rw_wrlock(A) pthread_mutex_lock((A))
#define rw_tryrdlock(A) pthread_mutex_trylock((A))
#define rw_trywrlock(A) pthread_mutex_trylock((A))
#define rw_unlock(A) pthread_mutex_unlock((A))
#define rwlock_destroy(A) pthread_mutex_destroy((A))
#elif defined(HAVE_PTHREAD_RWLOCK_RDLOCK)
#define rw_lock_t pthread_rwlock_t
#define my_rwlock_init(A,B) pthread_rwlock_init((A),(B))
#define rw_rdlock(A) pthread_rwlock_rdlock(A)
#define rw_wrlock(A) pthread_rwlock_wrlock(A)
#define rw_tryrdlock(A) pthread_rwlock_tryrdlock((A))
#define rw_trywrlock(A) pthread_rwlock_trywrlock((A))
#define rw_unlock(A) pthread_rwlock_unlock(A)
#define rwlock_destroy(A) pthread_rwlock_destroy(A)
#elif defined(HAVE_RWLOCK_INIT)
#ifdef HAVE_RWLOCK_T				/* For example Solaris 2.6-> */
#define rw_lock_t rwlock_t
#endif
#define my_rwlock_init(A,B) rwlock_init((A),USYNC_THREAD,0)
#else
/* Use our own version of read/write locks */
typedef struct _my_rw_lock_t {
	pthread_mutex_t lock;		/* lock for structure		*/
	pthread_cond_t	readers;	/* waiting readers		*/
	pthread_cond_t	writers;	/* waiting writers		*/
	int		state;		/* -1:writer,0:free,>0:readers	*/
	int		waiters;	/* number of waiting writers	*/
} my_rw_lock_t;

#define rw_lock_t my_rw_lock_t
#define rw_rdlock(A) my_rw_rdlock((A))
#define rw_wrlock(A) my_rw_wrlock((A))
#define rw_tryrdlock(A) my_rw_tryrdlock((A))
#define rw_trywrlock(A) my_rw_trywrlock((A))
#define rw_unlock(A) my_rw_unlock((A))
#define rwlock_destroy(A) my_rwlock_destroy((A))

extern int my_rwlock_init(my_rw_lock_t *, void *);
extern int my_rwlock_destroy(my_rw_lock_t *);
extern int my_rw_rdlock(my_rw_lock_t *);
extern int my_rw_wrlock(my_rw_lock_t *);
extern int my_rw_unlock(my_rw_lock_t *);
extern int my_rw_tryrdlock(my_rw_lock_t *);
extern int my_rw_trywrlock(my_rw_lock_t *);
#endif /* USE_MUTEX_INSTEAD_OF_RW_LOCKS */

#ifndef HAVE_THR_SETCONCURRENCY
#define thr_setconcurrency(A) pthread_dummy(0)
#endif
#if !defined(HAVE_PTHREAD_ATTR_SETSTACKSIZE) && ! defined(pthread_attr_setstacksize)
#define pthread_attr_setstacksize(A,B) pthread_dummy(0)
#endif

/* Define mutex types, see my_thr_init.c */
#define MY_MUTEX_INIT_SLOW   NULL
#ifdef PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP
extern pthread_mutexattr_t my_fast_mutexattr;
#define MY_MUTEX_INIT_FAST &my_fast_mutexattr
#else
#define MY_MUTEX_INIT_FAST   NULL
#endif
#ifdef PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP
extern pthread_mutexattr_t my_errorcheck_mutexattr;
#define MY_MUTEX_INIT_ERRCHK &my_errorcheck_mutexattr
#else
#define MY_MUTEX_INIT_ERRCHK   NULL
#endif

#ifndef ESRCH
/* Define it to something */
#define ESRCH 1
#endif

typedef uint64_t my_thread_id;

extern bool my_thread_global_init(void);
extern void my_thread_global_end(void);
extern bool my_thread_init(void);
extern void my_thread_end(void);
extern const char *my_thread_name(void);
extern my_thread_id my_thread_dbug_id(void);

/* All thread specific variables are in the following struct */

#define THREAD_NAME_SIZE 10
/*
  Drizzle can survive with 32K, but some glibc libraries require > 128K stack
  to resolve hostnames. Also recursive stored procedures needs stack.
*/
#define DEFAULT_THREAD_STACK	(256*INT32_C(1024))

struct st_my_thread_var
{
  pthread_cond_t suspend;
  pthread_mutex_t mutex;
  pthread_mutex_t * volatile current_mutex;
  pthread_cond_t * volatile current_cond;
  pthread_t pthread_self;
  my_thread_id id;
  int cmp_length;
  int volatile abort;
  bool init;
  struct st_my_thread_var *next,**prev;
  void *opt_info;
};

extern struct st_my_thread_var *_my_thread_var(void);
extern uint32_t my_thread_end_wait_time;
#define my_thread_var (_my_thread_var())
/*
  Keep track of shutdown,signal, and main threads so that my_end() will not
  report errors with them
*/

/* Which kind of thread library is in use */

#define THD_LIB_OTHER 1
#define THD_LIB_NPTL  2
#define THD_LIB_LT    4

extern uint32_t thd_lib_detected;

/*
  thread_safe_xxx functions are for critical statistic or counters.
  The implementation is guaranteed to be thread safe, on all platforms.
  Note that the calling code should *not* assume the counter is protected
  by the mutex given, as the implementation of these helpers may change
  to use my_atomic operations instead.
*/

#ifndef thread_safe_increment
#define thread_safe_increment(V,L) \
        (pthread_mutex_lock((L)), (V)++, pthread_mutex_unlock((L)))
#define thread_safe_decrement(V,L) \
        (pthread_mutex_lock((L)), (V)--, pthread_mutex_unlock((L)))
#endif

#ifndef thread_safe_add
#define thread_safe_add(V,C,L) \
        (pthread_mutex_lock((L)), (V)+=(C), pthread_mutex_unlock((L)))
#define thread_safe_sub(V,C,L) \
        (pthread_mutex_lock((L)), (V)-=(C), pthread_mutex_unlock((L)))
#endif

/*
  statistics_xxx functions are for non critical statistic,
  maintained in global variables.
  - race conditions can occur, making the result slightly inaccurate.
  - the lock given is not honored.
*/
#define statistic_decrement(V,L) (V)--
#define statistic_increment(V,L) (V)++
#define statistic_add(V,C,L)     (V)+=(C)
#define statistic_sub(V,C,L)     (V)-=(C)

/*
  No locking needed, the counter is owned by the thread
*/
#define status_var_increment(V) (V)++
#define status_var_decrement(V) (V)--
#define status_var_add(V,C)     (V)+=(C)
#define status_var_sub(V,C)     (V)-=(C)

#ifdef  __cplusplus
}
#endif
#endif /* _my_ptread_h */