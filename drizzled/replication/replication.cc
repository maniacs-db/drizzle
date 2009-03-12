/* Copyright (C) 2000-2006 MySQL AB & Sasha

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

#include <drizzled/server_includes.h>

#include <drizzled/replication/mi.h>
#include <drizzled/replication/replication.h>
#include <drizzled/log_event.h>
#include <libdrizzle/libdrizzle.h>
#include <mysys/hash.h>
#include <drizzled/error.h>
#include <drizzled/gettext.h>
#include <drizzled/data_home.h>

int max_binlog_dump_events = 0; // unlimited

/*
    fake_rotate_event() builds a fake (=which does not exist physically in any
    binlog) Rotate event, which contains the name of the binlog we are going to
    send to the slave (because the slave may not know it if it just asked for
    MASTER_LOG_FILE='', MASTER_LOG_POS=4).
    < 4.0.14, fake_rotate_event() was called only if the requested pos was 4.
    After this version we always call it, so that a 3.23.58 slave can rely on
    it to detect if the master is 4.0 (and stop) (the _fake_ Rotate event has
    zeros in the good positions which, by chance, make it possible for the 3.23
    slave to detect that this event is unexpected) (this is luck which happens
    because the master and slave disagree on the size of the header of
    Log_event).

    Relying on the event length of the Rotate event instead of these
    well-placed zeros was not possible as Rotate events have a variable-length
    part.
*/
static int fake_rotate_event(NET* net, String* packet, char* log_file_name,
                             uint64_t position, const char** errmsg)
{
  char header[LOG_EVENT_HEADER_LEN], buf[ROTATE_HEADER_LEN+100];
  /*
    'when' (the timestamp) is set to 0 so that slave could distinguish between
    real and fake Rotate events (if necessary)
  */
  memset(header, 0, 4);
  header[EVENT_TYPE_OFFSET] = ROTATE_EVENT;

  char* p = log_file_name+dirname_length(log_file_name);
  uint32_t ident_len = (uint32_t) strlen(p);
  uint32_t event_len = ident_len + LOG_EVENT_HEADER_LEN + ROTATE_HEADER_LEN;
  int4store(header + SERVER_ID_OFFSET, server_id);
  int4store(header + EVENT_LEN_OFFSET, event_len);
  int2store(header + FLAGS_OFFSET, 0);

  // TODO: check what problems this may cause and fix them
  int4store(header + LOG_POS_OFFSET, 0);

  packet->append(header, sizeof(header));
  int8store(buf+R_POS_OFFSET,position);
  packet->append(buf, ROTATE_HEADER_LEN);
  packet->append(p,ident_len);
  if (my_net_write(net, (unsigned char*) packet->ptr(), packet->length()))
  {
    *errmsg = "failed on my_net_write()";
    return(-1);
  }
  return(0);
}

static int send_file(Session *session)
{
  NET* net = &session->net;
  int fd = -1, error = 1;
  size_t bytes;
  char fname[FN_REFLEN+1];
  const char *errmsg = 0;
  int old_timeout;
  unsigned long packet_len;
  unsigned char buf[IO_SIZE];				// It's safe to alloc this

  /*
    The client might be slow loading the data, give him wait_timeout to do
    the job
  */
  old_timeout= net->read_timeout;
  my_net_set_read_timeout(net, session->variables.net_wait_timeout);

  /*
    We need net_flush here because the client will not know it needs to send
    us the file name until it has processed the load event entry
  */
  if (net_flush(net) || (packet_len = my_net_read(net)) == packet_error)
  {
    errmsg = _("Failed in send_file() while reading file name");
    goto err;
  }

  // terminate with \0 for fn_format
  *((char*)net->read_pos +  packet_len) = 0;
  fn_format(fname, (char*) net->read_pos + 1, "", "", 4);
  // this is needed to make replicate-ignore-db
  if (!strcmp(fname,"/dev/null"))
    goto end;

  if ((fd = my_open(fname, O_RDONLY, MYF(0))) < 0)
  {
    errmsg = _("Failed in send_file() on open of file");
    goto err;
  }

  while ((long) (bytes= my_read(fd, buf, IO_SIZE, MYF(0))) > 0)
  {
    if (my_net_write(net, buf, bytes))
    {
      errmsg = _("Failed in send_file() while writing data to client");
      goto err;
    }
  }

 end:
  if (my_net_write(net, (unsigned char*) "", 0) || net_flush(net) ||
      (my_net_read(net) == packet_error))
  {
    errmsg = _("Failed in send_file() while negotiating file transfer close");
    goto err;
  }
  error = 0;

 err:
  my_net_set_read_timeout(net, old_timeout);
  if (fd >= 0)
    (void) my_close(fd, MYF(0));
  if (errmsg)
  {
    sql_print_error("%s",errmsg);
  }
  return(error);
}


/*
  Adjust the position pointer in the binary log file for all running slaves

  SYNOPSIS
    adjust_linfo_offsets()
    purge_offset	Number of bytes removed from start of log index file

  NOTES
    - This is called when doing a PURGE when we delete lines from the
      index log file

  REQUIREMENTS
    - Before calling this function, we have to ensure that no threads are
      using any binary log file before purge_offset.a

  TODO
    - Inform the slave threads that they should sync the position
      in the binary log file with flush_relay_log_info.
      Now they sync is done for next read.
*/

void adjust_linfo_offsets(my_off_t purge_offset)
{
  Session *tmp;

  pthread_mutex_lock(&LOCK_thread_count);
  I_List_iterator<Session> it(threads);

  while ((tmp=it++))
  {
    LOG_INFO* linfo;
    if ((linfo = tmp->current_linfo))
    {
      pthread_mutex_lock(&linfo->lock);
      /*
	Index file offset can be less that purge offset only if
	we just started reading the index file. In that case
	we have nothing to adjust
      */
      if (linfo->index_file_offset < purge_offset)
	linfo->fatal = (linfo->index_file_offset != 0);
      else
	linfo->index_file_offset -= purge_offset;
      pthread_mutex_unlock(&linfo->lock);
    }
  }
  pthread_mutex_unlock(&LOCK_thread_count);
}


bool log_in_use(const char* log_name)
{
  int log_name_len = strlen(log_name) + 1;
  Session *tmp;
  bool result = 0;

  pthread_mutex_lock(&LOCK_thread_count);
  I_List_iterator<Session> it(threads);

  while ((tmp=it++))
  {
    LOG_INFO* linfo;
    if ((linfo = tmp->current_linfo))
    {
      pthread_mutex_lock(&linfo->lock);
      result = !memcmp(log_name, linfo->log_file_name, log_name_len);
      pthread_mutex_unlock(&linfo->lock);
      if (result)
	break;
    }
  }

  pthread_mutex_unlock(&LOCK_thread_count);
  return result;
}

bool purge_error_message(Session* session, int res)
{
  uint32_t errmsg= 0;

  switch (res)  {
  case 0: break;
  case LOG_INFO_EOF:	errmsg= ER_UNKNOWN_TARGET_BINLOG; break;
  case LOG_INFO_IO:	errmsg= ER_IO_ERR_LOG_INDEX_READ; break;
  case LOG_INFO_INVALID:errmsg= ER_BINLOG_PURGE_PROHIBITED; break;
  case LOG_INFO_SEEK:	errmsg= ER_FSEEK_FAIL; break;
  case LOG_INFO_MEM:	errmsg= ER_OUT_OF_RESOURCES; break;
  case LOG_INFO_FATAL:	errmsg= ER_BINLOG_PURGE_FATAL_ERR; break;
  case LOG_INFO_IN_USE: errmsg= ER_LOG_IN_USE; break;
  case LOG_INFO_EMFILE: errmsg= ER_BINLOG_PURGE_EMFILE; break;
  default:		errmsg= ER_LOG_PURGE_UNKNOWN_ERR; break;
  }

  if (errmsg)
  {
    my_message(errmsg, ER(errmsg), MYF(0));
    return true;
  }
  my_ok(session);
  return false;
}


bool purge_master_logs(Session* session, const char* to_log)
{
  char search_file_name[FN_REFLEN];
  if (!drizzle_bin_log.is_open())
  {
    my_ok(session);
    return false;
  }

  drizzle_bin_log.make_log_name(search_file_name, to_log);
  return purge_error_message(session,
			     drizzle_bin_log.purge_logs(search_file_name, 0, 1,
						      1, NULL));
}


bool purge_master_logs_before_date(Session* session, time_t purge_time)
{
  if (!drizzle_bin_log.is_open())
  {
    my_ok(session);
    return 0;
  }
  return purge_error_message(session,
                             drizzle_bin_log.purge_logs_before_date(purge_time));
}

int test_for_non_eof_log_read_errors(int error, const char **errmsg)
{
  if (error == LOG_READ_EOF)
    return 0;
  my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
  switch (error) {
  case LOG_READ_BOGUS:
    *errmsg = "bogus data in log event";
    break;
  case LOG_READ_TOO_LARGE:
    *errmsg = "log event entry exceeded max_allowed_packet; \
Increase max_allowed_packet on master";
    break;
  case LOG_READ_IO:
    *errmsg = "I/O error reading log event";
    break;
  case LOG_READ_MEM:
    *errmsg = "memory allocation failed reading log event";
    break;
  case LOG_READ_TRUNC:
    *errmsg = "binlog truncated in the middle of event";
    break;
  default:
    *errmsg = "unknown error reading log event on the master";
    break;
  }
  return error;
}


/**
  An auxiliary function for calling in mysql_binlog_send
  to initialize the heartbeat timeout in waiting for a binlogged event.

  @param[in]    session  Session to access a user variable

  @return        heartbeat period an uint64_t of nanoseconds
                 or zero if heartbeat was not demanded by slave
*/ 
static uint64_t get_heartbeat_period(Session * session)
{
  bool null_value;
  LEX_STRING name=  { C_STRING_WITH_LEN("master_heartbeat_period")};
  user_var_entry *entry= 
    (user_var_entry*) hash_search(&session->user_vars, (unsigned char*) name.str,
                                  name.length);
  return entry? entry->val_int(&null_value) : 0;
}

/*
  Function prepares and sends repliation heartbeat event.

  @param net                net object of Session
  @param packet             buffer to store the heartbeat instance
  @param event_coordinates  binlog file name and position of the last
                            real event master sent from binlog

  @note 
    Among three essential pieces of heartbeat data Log_event::when
    is computed locally.
    The  error to send is serious and should force terminating
    the dump thread.
*/
static int send_heartbeat_event(NET* net, String* packet,
                                const struct event_coordinates *coord)
{
  char header[LOG_EVENT_HEADER_LEN];
  /*
    'when' (the timestamp) is set to 0 so that slave could distinguish between
    real and fake Rotate events (if necessary)
  */
  memset(header, 0, 4);  // when

  header[EVENT_TYPE_OFFSET] = HEARTBEAT_LOG_EVENT;

  char* p= coord->file_name + dirname_length(coord->file_name);

  uint32_t ident_len = strlen(p);
  uint32_t event_len = ident_len + LOG_EVENT_HEADER_LEN;
  int4store(header + SERVER_ID_OFFSET, server_id);
  int4store(header + EVENT_LEN_OFFSET, event_len);
  int2store(header + FLAGS_OFFSET, 0);

  int4store(header + LOG_POS_OFFSET, coord->pos);  // log_pos

  packet->append(header, sizeof(header));
  packet->append(p, ident_len);             // log_file_name

  if (my_net_write(net, (unsigned char*) packet->ptr(), packet->length()) ||
      net_flush(net))
  {
    return(-1);
  }
  packet->set("\0", 1, &my_charset_bin);
  return(0);
}

/*
  TODO: Clean up loop to only have one call to send_file()
*/

void mysql_binlog_send(Session* session, char* log_ident, my_off_t pos,
		       uint16_t flags)
{
  LOG_INFO linfo;
  char *log_file_name = linfo.log_file_name;
  char search_file_name[FN_REFLEN], *name;
  IO_CACHE log;
  File file = -1;
  String* packet = &session->packet;
  int error;
  const char *errmsg = "Unknown error";
  NET* net = &session->net;
  pthread_mutex_t *log_lock;
  bool binlog_can_be_corrupted= false;

  memset(&log, 0, sizeof(log));
  /* 
     heartbeat_period from @master_heartbeat_period user variable
  */
  uint64_t heartbeat_period= get_heartbeat_period(session);
  struct timespec heartbeat_buf;
  struct event_coordinates coord_buf;
  struct timespec *heartbeat_ts= NULL;
  struct event_coordinates *coord= NULL;
  if (heartbeat_period != 0L)
  {
    heartbeat_ts= &heartbeat_buf;
    set_timespec_nsec(*heartbeat_ts, 0);
    coord= &coord_buf;
    coord->file_name= log_file_name; // initialization basing on what slave remembers
    coord->pos= pos;
  }

  if (!drizzle_bin_log.is_open())
  {
    errmsg = "Binary log is not open";
    my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
    goto err;
  }
  if (!server_id_supplied)
  {
    errmsg = "Misconfigured master - server id was not set";
    my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
    goto err;
  }

  name=search_file_name;
  if (log_ident[0])
    drizzle_bin_log.make_log_name(search_file_name, log_ident);
  else
    name=0;					// Find first log

  linfo.index_file_offset = 0;

  if (drizzle_bin_log.find_log_pos(&linfo, name, 1))
  {
    errmsg = "Could not find first log file name in binary log index file";
    my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
    goto err;
  }

  pthread_mutex_lock(&LOCK_thread_count);
  session->current_linfo = &linfo;
  pthread_mutex_unlock(&LOCK_thread_count);

  if ((file=open_binlog(&log, log_file_name, &errmsg)) < 0)
  {
    my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
    goto err;
  }
  if (pos < BIN_LOG_HEADER_SIZE || pos > my_b_filelength(&log))
  {
    errmsg= "Client requested master to start replication from \
impossible position";
    my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
    goto err;
  }

  /*
    We need to start a packet with something other than 255
    to distinguish it from error
  */
  packet->set("\0", 1, &my_charset_bin); /* This is the start of a new packet */

  /*
    Tell the client about the log name with a fake Rotate event;
    this is needed even if we also send a Format_description_log_event
    just after, because that event does not contain the binlog's name.
    Note that as this Rotate event is sent before
    Format_description_log_event, the slave cannot have any info to
    understand this event's format, so the header len of
    Rotate_log_event is FROZEN (so in 5.0 it will have a header shorter
    than other events except FORMAT_DESCRIPTION_EVENT).
    Before 4.0.14 we called fake_rotate_event below only if (pos ==
    BIN_LOG_HEADER_SIZE), because if this is false then the slave
    already knows the binlog's name.
    Since, we always call fake_rotate_event; if the slave already knew
    the log's name (ex: CHANGE MASTER TO MASTER_LOG_FILE=...) this is
    useless but does not harm much. It is nice for 3.23 (>=.58) slaves
    which test Rotate events to see if the master is 4.0 (then they
    choose to stop because they can't replicate 4.0); by always calling
    fake_rotate_event we are sure that 3.23.58 and newer will detect the
    problem as soon as replication starts (BUG#198).
    Always calling fake_rotate_event makes sending of normal
    (=from-binlog) Rotate events a priori unneeded, but it is not so
    simple: the 2 Rotate events are not equivalent, the normal one is
    before the Stop event, the fake one is after. If we don't send the
    normal one, then the Stop event will be interpreted (by existing 4.0
    slaves) as "the master stopped", which is wrong. So for safety,
    given that we want minimum modification of 4.0, we send the normal
    and fake Rotates.
  */
  if (fake_rotate_event(net, packet, log_file_name, pos, &errmsg))
  {
    /*
       This error code is not perfect, as fake_rotate_event() does not
       read anything from the binlog; if it fails it's because of an
       error in my_net_write(), fortunately it will say so in errmsg.
    */
    my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
    goto err;
  }
  packet->set("\0", 1, &my_charset_bin);
  /*
    Adding MAX_LOG_EVENT_HEADER_LEN, since a binlog event can become
    this larger than the corresponding packet (query) sent 
    from client to master.
  */
  session->variables.max_allowed_packet+= MAX_LOG_EVENT_HEADER;

  /*
    We can set log_lock now, it does not move (it's a member of
    drizzle_bin_log, and it's already inited, and it will be destroyed
    only at shutdown).
  */
  log_lock = drizzle_bin_log.get_log_lock();
  if (pos > BIN_LOG_HEADER_SIZE)
  {
     /*
       Try to find a Format_description_log_event at the beginning of
       the binlog
     */
     if (!(error = Log_event::read_log_event(&log, packet, log_lock)))
     {
       /*
         The packet has offsets equal to the normal offsets in a binlog
         event +1 (the first character is \0).
       */
       if ((*packet)[EVENT_TYPE_OFFSET+1] == FORMAT_DESCRIPTION_EVENT)
       {
         binlog_can_be_corrupted= test((*packet)[FLAGS_OFFSET+1] &
                                       LOG_EVENT_BINLOG_IN_USE_F);
         (*packet)[FLAGS_OFFSET+1] &= ~LOG_EVENT_BINLOG_IN_USE_F;
         /*
           mark that this event with "log_pos=0", so the slave
           should not increment master's binlog position
           (rli->group_master_log_pos)
         */
         int4store((char*) packet->ptr()+LOG_POS_OFFSET+1, 0);
         /*
           if reconnect master sends FD event with `created' as 0
           to avoid destroying temp tables.
          */
         int4store((char*) packet->ptr()+LOG_EVENT_MINIMAL_HEADER_LEN+
                   ST_CREATED_OFFSET+1, (uint32_t) 0);
         /* send it */
         if (my_net_write(net, (unsigned char*) packet->ptr(), packet->length()))
         {
           errmsg = "Failed on my_net_write()";
           my_errno= ER_UNKNOWN_ERROR;
           goto err;
         }

         /*
           No need to save this event. We are only doing simple reads
           (no real parsing of the events) so we don't need it. And so
           we don't need the artificial Format_description_log_event of
           3.23&4.x.
         */
       }
     }
     else
     {
       if (test_for_non_eof_log_read_errors(error, &errmsg))
         goto err;
       /*
         It's EOF, nothing to do, go on reading next events, the
         Format_description_log_event will be found naturally if it is written.
       */
     }
     /* reset the packet as we wrote to it in any case */
     packet->set("\0", 1, &my_charset_bin);
  } /* end of if (pos > BIN_LOG_HEADER_SIZE); */
  else
  {
    /* The Format_description_log_event event will be found naturally. */
  }

  /* seek to the requested position, to start the requested dump */
  my_b_seek(&log, pos);			// Seek will done on next read

  while (!net->error && net->vio != 0 && !session->killed)
  {
    while (!(error = Log_event::read_log_event(&log, packet, log_lock)))
    {
      /*
        log's filename does not change while it's active
      */
      if (coord)
        coord->pos= uint4korr(packet->ptr() + 1 + LOG_POS_OFFSET);

      if ((*packet)[EVENT_TYPE_OFFSET+1] == FORMAT_DESCRIPTION_EVENT)
      {
        binlog_can_be_corrupted= test((*packet)[FLAGS_OFFSET+1] &
                                      LOG_EVENT_BINLOG_IN_USE_F);
        (*packet)[FLAGS_OFFSET+1] &= ~LOG_EVENT_BINLOG_IN_USE_F;
      }
      else if ((*packet)[EVENT_TYPE_OFFSET+1] == STOP_EVENT)
        binlog_can_be_corrupted= false;

      if (my_net_write(net, (unsigned char*) packet->ptr(), packet->length()))
      {
	errmsg = "Failed on my_net_write()";
	my_errno= ER_UNKNOWN_ERROR;
	goto err;
      }

      if ((*packet)[LOG_EVENT_OFFSET+1] == LOAD_EVENT)
      {
	if (send_file(session))
	{
	  errmsg = "failed in send_file()";
	  my_errno= ER_UNKNOWN_ERROR;
	  goto err;
	}
      }
      packet->set("\0", 1, &my_charset_bin);
    }

    /*
      here we were reading binlog that was not closed properly (as a result
      of a crash ?). treat any corruption as EOF
    */
    if (binlog_can_be_corrupted && error != LOG_READ_MEM)
      error=LOG_READ_EOF;
    /*
      TODO: now that we are logging the offset, check to make sure
      the recorded offset and the actual match.
      Guilhem 2003-06: this is not true if this master is a slave
      <4.0.15 running with --log-slave-updates, because then log_pos may
      be the offset in the-master-of-this-master's binlog.
    */
    if (test_for_non_eof_log_read_errors(error, &errmsg))
      goto err;

    if (!(flags & BINLOG_DUMP_NON_BLOCK) &&
        drizzle_bin_log.is_active(log_file_name))
    {
      /*
	Block until there is more data in the log
      */
      if (net_flush(net))
      {
	errmsg = "failed on net_flush()";
	my_errno= ER_UNKNOWN_ERROR;
	goto err;
      }

      /*
	We may have missed the update broadcast from the log
	that has just happened, let's try to catch it if it did.
	If we did not miss anything, we just wait for other threads
	to signal us.
      */
      {
	log.error=0;
	bool read_packet = 0, fatal_error = 0;

	/*
	  No one will update the log while we are reading
	  now, but we'll be quick and just read one record

	  TODO:
          Add an counter that is incremented for each time we update the
          binary log.  We can avoid the following read if the counter
          has not been updated since last read.
	*/

	pthread_mutex_lock(log_lock);
	switch (Log_event::read_log_event(&log, packet, (pthread_mutex_t*)0)) {
	case 0:
	  /* we read successfully, so we'll need to send it to the slave */
	  pthread_mutex_unlock(log_lock);
	  read_packet = 1;
          if (coord)
            coord->pos= uint4korr(packet->ptr() + 1 + LOG_POS_OFFSET);
	  break;

	case LOG_READ_EOF:
        {
          int ret;
	  if (session->server_id==0) // for mysqlbinlog (mysqlbinlog.server_id==0)
	  {
	    pthread_mutex_unlock(log_lock);
	    goto end;
	  }

          do 
          {
            if (coord)
            {
              assert(heartbeat_ts && heartbeat_period != 0L);
              set_timespec_nsec(*heartbeat_ts, heartbeat_period);
            }
            ret= drizzle_bin_log.wait_for_update_bin_log(session, heartbeat_ts);
            assert(ret == 0 || (heartbeat_period != 0L && coord != NULL));
            if (ret == ETIMEDOUT || ret == ETIME)
            {
              if (send_heartbeat_event(net, packet, coord))
              {
                errmsg = "Failed on my_net_write()";
                my_errno= ER_UNKNOWN_ERROR;
                pthread_mutex_unlock(log_lock);
                goto err;
              }
            }
            else
            {
              assert(ret == 0);
            }
          } while (ret != 0 && coord != NULL && !session->killed);
          pthread_mutex_unlock(log_lock);
        }    
        break;
            
        default:
	  pthread_mutex_unlock(log_lock);
	  fatal_error = 1;
	  break;
	}

	if (read_packet)
	{
	  session->set_proc_info("Sending binlog event to slave");
	  if (my_net_write(net, (unsigned char*) packet->ptr(), packet->length()) )
	  {
	    errmsg = "Failed on my_net_write()";
	    my_errno= ER_UNKNOWN_ERROR;
	    goto err;
	  }

	  if ((*packet)[LOG_EVENT_OFFSET+1] == LOAD_EVENT)
	  {
	    if (send_file(session))
	    {
	      errmsg = "failed in send_file()";
	      my_errno= ER_UNKNOWN_ERROR;
	      goto err;
	    }
	  }
	  packet->set("\0", 1, &my_charset_bin);
	  /*
	    No need to net_flush because we will get to flush later when
	    we hit EOF pretty quick
	  */
	}

	if (fatal_error)
	{
	  errmsg = "error reading log entry";
          my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
	  goto err;
	}
	log.error=0;
      }
    }
    else
    {
      bool loop_breaker = 0;
      /* need this to break out of the for loop from switch */

      session->set_proc_info("Finished reading one binlog; switching to next binlog");
      switch (drizzle_bin_log.find_next_log(&linfo, 1)) {
      case LOG_INFO_EOF:
	loop_breaker = (flags & BINLOG_DUMP_NON_BLOCK);
	break;
      case 0:
	break;
      default:
	errmsg = "could not find next log";
	my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
	goto err;
      }

      if (loop_breaker)
        break;

      end_io_cache(&log);
      (void) my_close(file, MYF(MY_WME));

      /*
        Call fake_rotate_event() in case the previous log (the one which
        we have just finished reading) did not contain a Rotate event
        (for example (I don't know any other example) the previous log
        was the last one before the master was shutdown & restarted).
        This way we tell the slave about the new log's name and
        position.  If the binlog is 5.0, the next event we are going to
        read and send is Format_description_log_event.
      */
      if ((file=open_binlog(&log, log_file_name, &errmsg)) < 0 ||
	  fake_rotate_event(net, packet, log_file_name, BIN_LOG_HEADER_SIZE,
                            &errmsg))
      {
	my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
	goto err;
      }

      packet->length(0);
      packet->append('\0');
      if (coord)
        coord->file_name= log_file_name; // reset to the next
    }
  }

end:
  end_io_cache(&log);
  (void)my_close(file, MYF(MY_WME));

  my_eof(session);
  session->set_proc_info("Waiting to finalize termination");
  pthread_mutex_lock(&LOCK_thread_count);
  session->current_linfo = 0;
  pthread_mutex_unlock(&LOCK_thread_count);
  return;

err:
  session->set_proc_info("Waiting to finalize termination");
  end_io_cache(&log);
  /*
    Exclude  iteration through thread list
    this is needed for purge_logs() - it will iterate through
    thread list and update session->current_linfo->index_file_offset
    this mutex will make sure that it never tried to update our linfo
    after we return from this stack frame
  */
  pthread_mutex_lock(&LOCK_thread_count);
  session->current_linfo = 0;
  pthread_mutex_unlock(&LOCK_thread_count);
  if (file >= 0)
    (void) my_close(file, MYF(MY_WME));

  my_message(my_errno, errmsg, MYF(0));
  return;
}

int start_slave(Session* session , Master_info* mi,  bool net_report)
{
  int slave_errno= 0;
  int thread_mask;

  lock_slave_threads(mi);  // this allows us to cleanly read slave_running
  // Get a mask of _stopped_ threads
  init_thread_mask(&thread_mask,mi,1 /* inverse */);
  /*
    Below we will start all stopped threads.  But if the user wants to
    start only one thread, do as if the other thread was running (as we
    don't wan't to touch the other thread), so set the bit to 0 for the
    other thread
  */
  if (session->lex->slave_session_opt)
    thread_mask&= session->lex->slave_session_opt;
  if (thread_mask) //some threads are stopped, start them
  {
    if (mi->init_master_info(master_info_file, relay_log_info_file, thread_mask))
      slave_errno=ER_MASTER_INFO;
    else if (server_id_supplied && *mi->getHostname())
    {
      /*
        If we will start SQL thread we will care about UNTIL options If
        not and they are specified we will ignore them and warn user
        about this fact.
      */
      if (thread_mask & SLAVE_SQL)
      {
        pthread_mutex_lock(&mi->rli.data_lock);

        if (session->lex->mi.pos)
        {
          mi->rli.until_condition= Relay_log_info::UNTIL_MASTER_POS;
          mi->rli.until_log_pos= session->lex->mi.pos;
          /*
             We don't check session->lex->mi.log_file_name for NULL here
             since it is checked in sql_yacc.yy
          */
          strncpy(mi->rli.until_log_name, session->lex->mi.log_file_name,
                  sizeof(mi->rli.until_log_name)-1);
        }
        else if (session->lex->mi.relay_log_pos)
        {
          mi->rli.until_condition= Relay_log_info::UNTIL_RELAY_POS;
          mi->rli.until_log_pos= session->lex->mi.relay_log_pos;
          strncpy(mi->rli.until_log_name, session->lex->mi.relay_log_name,
                  sizeof(mi->rli.until_log_name)-1);
        }
        else
          mi->rli.clear_until_condition();

        if (mi->rli.until_condition != Relay_log_info::UNTIL_NONE)
        {
          /* Preparing members for effective until condition checking */
          const char *p= fn_ext(mi->rli.until_log_name);
          char *p_end;
          if (*p)
          {
            //p points to '.'
            mi->rli.until_log_name_extension= strtoul(++p,&p_end, 10);
            /*
              p_end points to the first invalid character. If it equals
              to p, no digits were found, error. If it contains '\0' it
              means  conversion went ok.
            */
            if (p_end==p || *p_end)
              slave_errno=ER_BAD_SLAVE_UNTIL_COND;
          }
          else
            slave_errno=ER_BAD_SLAVE_UNTIL_COND;

          /* mark the cached result of the UNTIL comparison as "undefined" */
          mi->rli.until_log_names_cmp_result=
            Relay_log_info::UNTIL_LOG_NAMES_CMP_UNKNOWN;

          /* Issuing warning then started without --skip-slave-start */
          if (!opt_skip_slave_start)
            push_warning(session, DRIZZLE_ERROR::WARN_LEVEL_NOTE,
                         ER_MISSING_SKIP_SLAVE,
                         ER(ER_MISSING_SKIP_SLAVE));
        }

        pthread_mutex_unlock(&mi->rli.data_lock);
      }
      else if (session->lex->mi.pos || session->lex->mi.relay_log_pos)
        push_warning(session, DRIZZLE_ERROR::WARN_LEVEL_NOTE, ER_UNTIL_COND_IGNORED,
                     ER(ER_UNTIL_COND_IGNORED));

      if (!slave_errno)
        slave_errno = start_slave_threads(0 /*no mutex */,
					1 /* wait for start */,
					mi,
					master_info_file,relay_log_info_file,
					thread_mask);
    }
    else
      slave_errno = ER_BAD_SLAVE;
  }
  else
  {
    /* no error if all threads are already started, only a warning */
    push_warning(session, DRIZZLE_ERROR::WARN_LEVEL_NOTE, ER_SLAVE_WAS_RUNNING,
                 ER(ER_SLAVE_WAS_RUNNING));
  }

  unlock_slave_threads(mi);

  if (slave_errno)
  {
    if (net_report)
      my_message(slave_errno, ER(slave_errno), MYF(0));
    return(1);
  }
  else if (net_report)
    my_ok(session);

  return(0);
}


int stop_slave(Session* session, Master_info* mi, bool net_report )
{
  int slave_errno;
  if (!session)
    session = current_session;

  session->set_proc_info("Killing slave");
  int thread_mask;
  lock_slave_threads(mi);
  // Get a mask of _running_ threads
  init_thread_mask(&thread_mask,mi,0 /* not inverse*/);
  /*
    Below we will stop all running threads.
    But if the user wants to stop only one thread, do as if the other thread
    was stopped (as we don't wan't to touch the other thread), so set the
    bit to 0 for the other thread
  */
  if (session->lex->slave_session_opt)
    thread_mask &= session->lex->slave_session_opt;

  if (thread_mask)
  {
    slave_errno= terminate_slave_threads(mi,thread_mask,
                                         1 /*skip lock */);
  }
  else
  {
    //no error if both threads are already stopped, only a warning
    slave_errno= 0;
    push_warning(session, DRIZZLE_ERROR::WARN_LEVEL_NOTE, ER_SLAVE_WAS_NOT_RUNNING,
                 ER(ER_SLAVE_WAS_NOT_RUNNING));
  }
  unlock_slave_threads(mi);
  session->set_proc_info(0);

  if (slave_errno)
  {
    if (net_report)
      my_message(slave_errno, ER(slave_errno), MYF(0));
    return(1);
  }
  else if (net_report)
    my_ok(session);

  return(0);
}


/*
  Remove all relay logs and start replication from the start

  SYNOPSIS
    reset_slave()
    session			Thread handler
    mi			Master info for the slave

  RETURN
    0	ok
    1	error
*/


int reset_slave(Session *session, Master_info* mi)
{
  struct stat stat_area;
  char fname[FN_REFLEN];
  int thread_mask= 0, error= 0;
  uint32_t sql_errno=0;
  const char* errmsg=0;

  lock_slave_threads(mi);
  init_thread_mask(&thread_mask,mi,0 /* not inverse */);
  if (thread_mask) // We refuse if any slave thread is running
  {
    sql_errno= ER_SLAVE_MUST_STOP;
    error=1;
    goto err;
  }

  // delete relay logs, clear relay log coordinates
  if ((error= purge_relay_logs(&mi->rli, session,
			       1 /* just reset */,
			       &errmsg)))
    goto err;

  /* Clear master's log coordinates */
  mi->reset();
  /*
     Reset errors (the idea is that we forget about the
     old master).
  */
  mi->rli.clear_error();
  mi->rli.clear_until_condition();

  // close master_info_file, relay_log_info_file, set mi->inited=rli->inited=0
  mi->end_master_info();
  // and delete these two files
  fn_format(fname, master_info_file, drizzle_data_home, "", 4+32);
  if (!stat(fname, &stat_area) && my_delete(fname, MYF(MY_WME)))
  {
    error=1;
    goto err;
  }
  // delete relay_log_info_file
  fn_format(fname, relay_log_info_file, drizzle_data_home, "", 4+32);
  if (!stat(fname, &stat_area) && my_delete(fname, MYF(MY_WME)))
  {
    error=1;
    goto err;
  }

err:
  unlock_slave_threads(mi);
  if (error)
    my_error(sql_errno, MYF(0), errmsg);
  return(error);
}

/*

  Kill all Binlog_dump threads which previously talked to the same slave
  ("same" means with the same server id). Indeed, if the slave stops, if the
  Binlog_dump thread is waiting (pthread_cond_wait) for binlog update, then it
  will keep existing until a query is written to the binlog. If the master is
  idle, then this could last long, and if the slave reconnects, we could have 2
  Binlog_dump threads in SHOW PROCESSLIST, until a query is written to the
  binlog. To avoid this, when the slave reconnects and sends COM_BINLOG_DUMP,
  the master kills any existing thread with the slave's server id (if this id is
  not zero; it will be true for real slaves, but false for mysqlbinlog when it
  sends COM_BINLOG_DUMP to get a remote binlog dump).

  SYNOPSIS
    kill_zombie_dump_threads()
    slave_server_id     the slave's server id

*/


void kill_zombie_dump_threads(uint32_t slave_server_id)
{
  pthread_mutex_lock(&LOCK_thread_count);
  I_List_iterator<Session> it(threads);
  Session *tmp;

  while ((tmp=it++))
  {
    if (tmp->command == COM_BINLOG_DUMP &&
       tmp->server_id == slave_server_id)
    {
      pthread_mutex_lock(&tmp->LOCK_delete);	// Lock from delete
      break;
    }
  }
  pthread_mutex_unlock(&LOCK_thread_count);
  if (tmp)
  {
    /*
      Here we do not call kill_one_thread() as
      it will be slow because it will iterate through the list
      again. We just to do kill the thread ourselves.
    */
    tmp->awake(Session::KILL_QUERY);
    pthread_mutex_unlock(&tmp->LOCK_delete);
  }
}


bool change_master(Session* session, Master_info* mi)
{
  int thread_mask;
  const char* errmsg= 0;
  bool need_relay_log_purge= 1;

  lock_slave_threads(mi);
  init_thread_mask(&thread_mask,mi,0 /*not inverse*/);
  if (thread_mask) // We refuse if any slave thread is running
  {
    my_message(ER_SLAVE_MUST_STOP, ER(ER_SLAVE_MUST_STOP), MYF(0));
    unlock_slave_threads(mi);
    return(true);
  }

  session->set_proc_info("Changing master");
  LEX_MASTER_INFO* lex_mi= &session->lex->mi;
  // TODO: see if needs re-write
  if (mi->init_master_info(master_info_file, relay_log_info_file, thread_mask))
  {
    my_message(ER_MASTER_INFO, ER(ER_MASTER_INFO), MYF(0));
    unlock_slave_threads(mi);
    return(true);
  }

  /*
    Data lock not needed since we have already stopped the running threads,
    and we have the hold on the run locks which will keep all threads that
    could possibly modify the data structures from running
  */

  /*
    If the user specified host or port without binlog or position,
    reset binlog's name to FIRST and position to 4.
  */

  if ((lex_mi->host || lex_mi->port) && !lex_mi->log_file_name && !lex_mi->pos)
    mi->reset();

  if (lex_mi->log_file_name)
    mi->setLogName(lex_mi->log_file_name);
  if (lex_mi->pos)
  {
    mi->setLogPosition(lex_mi->pos);
  }

  if (lex_mi->host)
    mi->setHost(lex_mi->host, lex_mi->port);
  if (lex_mi->user)
    mi->setUsername(lex_mi->user);
  if (lex_mi->password)
    mi->setPassword(lex_mi->password);
  if (lex_mi->connect_retry)
    mi->connect_retry = lex_mi->connect_retry;
  if (lex_mi->heartbeat_opt != LEX_MASTER_INFO::LEX_MI_UNCHANGED)
    mi->heartbeat_period = lex_mi->heartbeat_period;
  else
    mi->heartbeat_period= (float) cmin((double)SLAVE_MAX_HEARTBEAT_PERIOD,
                                      (slave_net_timeout/2.0));
  mi->received_heartbeats= 0L; // counter lives until master is CHANGEd

  if (lex_mi->relay_log_name)
  {
    need_relay_log_purge= 0;
    mi->rli.event_relay_log_name.assign(lex_mi->relay_log_name);
  }

  if (lex_mi->relay_log_pos)
  {
    need_relay_log_purge= 0;
    mi->rli.group_relay_log_pos= mi->rli.event_relay_log_pos= lex_mi->relay_log_pos;
  }

  /*
    If user did specify neither host nor port nor any log name nor any log
    pos, i.e. he specified only user/password/master_connect_retry, he probably
    wants replication to resume from where it had left, i.e. from the
    coordinates of the **SQL** thread (imagine the case where the I/O is ahead
    of the SQL; restarting from the coordinates of the I/O would lose some
    events which is probably unwanted when you are just doing minor changes
    like changing master_connect_retry).
    A side-effect is that if only the I/O thread was started, this thread may
    restart from ''/4 after the CHANGE MASTER. That's a minor problem (it is a
    much more unlikely situation than the one we are fixing here).
    Note: coordinates of the SQL thread must be read here, before the
    'if (need_relay_log_purge)' block which resets them.
  */
  if (!lex_mi->host && !lex_mi->port &&
      !lex_mi->log_file_name && !lex_mi->pos &&
      need_relay_log_purge)
   {
     /*
       Sometimes mi->rli.master_log_pos == 0 (it happens when the SQL thread is
       not initialized), so we use a cmax().
       What happens to mi->rli.master_log_pos during the initialization stages
       of replication is not 100% clear, so we guard against problems using
       cmax().
      */
     mi->setLogPosition(((BIN_LOG_HEADER_SIZE > mi->rli.group_master_log_pos)
                         ? BIN_LOG_HEADER_SIZE
                         : mi->rli.group_master_log_pos));
     mi->setLogName(mi->rli.group_master_log_name.c_str());
  }
  /*
    Relay log's IO_CACHE may not be inited, if rli->inited==0 (server was never
    a slave before).
  */
  if (mi->flush())
  {
    my_error(ER_RELAY_LOG_INIT, MYF(0), "Failed to flush master info file");
    unlock_slave_threads(mi);
    return(true);
  }
  if (need_relay_log_purge)
  {
    relay_log_purge= 1;
    session->set_proc_info("Purging old relay logs");
    if (purge_relay_logs(&mi->rli, session,
			 0 /* not only reset, but also reinit */,
			 &errmsg))
    {
      my_error(ER_RELAY_LOG_FAIL, MYF(0), errmsg);
      unlock_slave_threads(mi);
      return(true);
    }
  }
  else
  {
    const char* msg;
    relay_log_purge= 0;
    /* Relay log is already initialized */
    if (init_relay_log_pos(&mi->rli,
			   mi->rli.group_relay_log_name.c_str(),
			   mi->rli.group_relay_log_pos,
			   0 /*no data lock*/,
			   &msg, 0))
    {
      my_error(ER_RELAY_LOG_INIT, MYF(0), msg);
      unlock_slave_threads(mi);
      return(true);
    }
  }
  /*
    Coordinates in rli were spoilt by the 'if (need_relay_log_purge)' block,
    so restore them to good values. If we left them to ''/0, that would work;
    but that would fail in the case of 2 successive CHANGE MASTER (without a
    START SLAVE in between): because first one would set the coords in mi to
    the good values of those in rli, the set those in rli to ''/0, then
    second CHANGE MASTER would set the coords in mi to those of rli, i.e. to
    ''/0: we have lost all copies of the original good coordinates.
    That's why we always save good coords in rli.
  */
  mi->rli.group_master_log_pos= mi->getLogPosition();
  mi->rli.group_master_log_name.assign(mi->getLogName());

  if (mi->rli.group_master_log_name.size() == 0) // uninitialized case
    mi->rli.group_master_log_pos= 0;

  pthread_mutex_lock(&mi->rli.data_lock);
  mi->rli.abort_pos_wait++; /* for MASTER_POS_WAIT() to abort */
  /* Clear the errors, for a clean start */
  mi->rli.clear_error();
  mi->rli.clear_until_condition();
  /*
    If we don't write new coordinates to disk now, then old will remain in
    relay-log.info until START SLAVE is issued; but if mysqld is shutdown
    before START SLAVE, then old will remain in relay-log.info, and will be the
    in-memory value at restart (thus causing errors, as the old relay log does
    not exist anymore).
  */
  flush_relay_log_info(&mi->rli);
  pthread_cond_broadcast(&mi->data_cond);
  pthread_mutex_unlock(&mi->rli.data_lock);

  unlock_slave_threads(mi);
  session->set_proc_info(0);
  my_ok(session);
  return(false);
}

int reset_master(Session* session)
{
  if (!drizzle_bin_log.is_open())
  {
    my_message(ER_FLUSH_MASTER_BINLOG_CLOSED,
               ER(ER_FLUSH_MASTER_BINLOG_CLOSED), MYF(ME_BELL+ME_WAITTANG));
    return 1;
  }
  return drizzle_bin_log.reset_logs(session);
}

int cmp_master_pos(const char* log_file_name1, uint64_t log_pos1,
		   const char* log_file_name2, uint64_t log_pos2)
{
  int res;
  uint32_t log_file_name1_len=  strlen(log_file_name1);
  uint32_t log_file_name2_len=  strlen(log_file_name2);

  //  We assume that both log names match up to '.'
  if (log_file_name1_len == log_file_name2_len)
  {
    if ((res= strcmp(log_file_name1, log_file_name2)))
      return res;
    return (log_pos1 < log_pos2) ? -1 : (log_pos1 == log_pos2) ? 0 : 1;
  }
  return ((log_file_name1_len < log_file_name2_len) ? -1 : 1);
}


bool show_binlog_info(Session* session)
{
  Protocol *protocol= session->protocol;
  List<Item> field_list;
  field_list.push_back(new Item_empty_string("File", FN_REFLEN));
  field_list.push_back(new Item_return_int("Position",20,
					   DRIZZLE_TYPE_LONGLONG));

  if (protocol->send_fields(&field_list,
                            Protocol::SEND_NUM_ROWS | Protocol::SEND_EOF))
    return(true);
  protocol->prepare_for_resend();

  if (drizzle_bin_log.is_open())
  {
    LOG_INFO li;
    drizzle_bin_log.get_current_log(&li);
    int dir_len = dirname_length(li.log_file_name);
    protocol->store(li.log_file_name + dir_len, &my_charset_bin);
    protocol->store((uint64_t) li.pos);
    if (protocol->write())
      return(true);
  }
  my_eof(session);
  return(false);
}


/*
  Send a list of all binary logs to client

  SYNOPSIS
    show_binlogs()
    session		Thread specific variable

  RETURN VALUES
    false OK
    true  error
*/

bool show_binlogs(Session* session)
{
  IO_CACHE *index_file;
  LOG_INFO cur;
  File file;
  char fname[FN_REFLEN];
  List<Item> field_list;
  uint32_t length;
  int cur_dir_len;
  Protocol *protocol= session->protocol;

  if (!drizzle_bin_log.is_open())
  {
    my_message(ER_NO_BINARY_LOGGING, ER(ER_NO_BINARY_LOGGING), MYF(0));
    return 1;
  }

  field_list.push_back(new Item_empty_string("Log_name", 255));
  field_list.push_back(new Item_return_int("File_size", 20,
                                           DRIZZLE_TYPE_LONGLONG));
  if (protocol->send_fields(&field_list,
                            Protocol::SEND_NUM_ROWS | Protocol::SEND_EOF))
    return(true);
  
  pthread_mutex_lock(drizzle_bin_log.get_log_lock());
  drizzle_bin_log.lock_index();
  index_file= drizzle_bin_log.get_index_file();
  
  drizzle_bin_log.raw_get_current_log(&cur); // dont take mutex
  pthread_mutex_unlock(drizzle_bin_log.get_log_lock()); // lockdep, OK
  
  cur_dir_len= dirname_length(cur.log_file_name);

  reinit_io_cache(index_file, READ_CACHE, (my_off_t) 0, 0, 0);

  /* The file ends with EOF or empty line */
  while ((length=my_b_gets(index_file, fname, sizeof(fname))) > 1)
  {
    int dir_len;
    uint64_t file_length= 0;                   // Length if open fails
    fname[--length] = '\0';                     // remove the newline

    protocol->prepare_for_resend();
    dir_len= dirname_length(fname);
    length-= dir_len;
    protocol->store(fname + dir_len, length, &my_charset_bin);

    if (!(strncmp(fname+dir_len, cur.log_file_name+cur_dir_len, length)))
      file_length= cur.pos;  /* The active log, use the active position */
    else
    {
      /* this is an old log, open it and find the size */
      if ((file= my_open(fname, O_RDONLY,
                         MYF(0))) >= 0)
      {
        file_length= (uint64_t) my_seek(file, 0L, MY_SEEK_END, MYF(0));
        my_close(file, MYF(0));
      }
    }
    protocol->store(file_length);
    if (protocol->write())
      goto err;
  }
  drizzle_bin_log.unlock_index();
  my_eof(session);
  return(false);

err:
  drizzle_bin_log.unlock_index();
  return(true);
}

/**
   Load data's io cache specific hook to be executed
   before a chunk of data is being read into the cache's buffer
   The fuction instantianates and writes into the binlog
   replication events along LOAD DATA processing.
   
   @param file  pointer to io-cache
   @return 0
*/
int log_loaded_block(IO_CACHE* file)
{
  LOAD_FILE_INFO *lf_info;
  uint32_t block_len;
  /* buffer contains position where we started last read */
  unsigned char* buffer= (unsigned char*) my_b_get_buffer_start(file);
  uint32_t max_event_size= current_session->variables.max_allowed_packet;
  lf_info= (LOAD_FILE_INFO*) file->arg;
  if (true)
    return(0);
  if (lf_info->last_pos_in_file != HA_POS_ERROR &&
      lf_info->last_pos_in_file >= my_b_get_pos_in_file(file))
    return(0);
  
  for (block_len= my_b_get_bytes_in_buffer(file); block_len > 0;
       buffer += cmin(block_len, max_event_size),
       block_len -= cmin(block_len, max_event_size))
  {
    lf_info->last_pos_in_file= my_b_get_pos_in_file(file);
    if (lf_info->wrote_create_file)
    {
      Append_block_log_event a(lf_info->session, lf_info->session->db, buffer,
                               cmin(block_len, max_event_size),
                               lf_info->log_delayed);
      drizzle_bin_log.write(&a);
    }
    else
    {
      Begin_load_query_log_event b(lf_info->session, lf_info->session->db,
                                   buffer,
                                   cmin(block_len, max_event_size),
                                   lf_info->log_delayed);
      drizzle_bin_log.write(&b);
      lf_info->wrote_create_file= 1;
    }
  }
  return(0);
}

/*
  Replication System Variables
*/

class sys_var_slave_skip_counter :public sys_var
{
public:
  sys_var_slave_skip_counter(sys_var_chain *chain, const char *name_arg)
    :sys_var(name_arg)
  { chain_sys_var(chain); }
  bool check(Session *session, set_var *var);
  bool update(Session *session, set_var *var);
  bool check_type(enum_var_type type) { return type != OPT_GLOBAL; }
  /*
    We can't retrieve the value of this, so we don't have to define
    type() or value_ptr()
  */
};

class sys_var_sync_binlog_period :public sys_var_long_ptr
{
public:
  sys_var_sync_binlog_period(sys_var_chain *chain, const char *name_arg,
                             uint64_t *value_ptr)
    :sys_var_long_ptr(chain, name_arg, value_ptr) {}
  bool update(Session *session, set_var *var);
};

static void fix_slave_net_timeout(Session *session,
                                  enum_var_type type __attribute__((unused)))
{
  pthread_mutex_lock(&LOCK_active_mi);
  if (active_mi && slave_net_timeout < active_mi->heartbeat_period)
    push_warning_printf(session, DRIZZLE_ERROR::WARN_LEVEL_WARN,
                        ER_SLAVE_HEARTBEAT_VALUE_OUT_OF_RANGE,
                        "The currect value for master_heartbeat_period"
                        " exceeds the new value of `slave_net_timeout' sec."
                        " A sensible value for the period should be"
                        " less than the timeout.");
  pthread_mutex_unlock(&LOCK_active_mi);
  return;
}

static sys_var_chain vars = { NULL, NULL };

static sys_var_bool_ptr	sys_relay_log_purge(&vars, "relay_log_purge",
					    &relay_log_purge);
static sys_var_long_ptr	sys_slave_net_timeout(&vars, "slave_net_timeout",
					      &slave_net_timeout,
                                              fix_slave_net_timeout);
static sys_var_long_ptr	sys_slave_trans_retries(&vars, "slave_transaction_retries", &slave_trans_retries);
static sys_var_sync_binlog_period sys_sync_binlog_period(&vars, "sync_binlog", &sync_binlog_period);
static sys_var_slave_skip_counter sys_slave_skip_counter(&vars, "sql_slave_skip_counter");

static int show_slave_skip_errors(Session *session, SHOW_VAR *var, char *buff);


static int show_slave_skip_errors(Session *session __attribute__((unused)),
                                  SHOW_VAR *var, char *buff)
{
  var->type=SHOW_CHAR;
  var->value= buff;
  if (!use_slave_mask || bitmap_is_clear_all(&slave_error_mask))
  {
    var->value= const_cast<char *>("OFF");
  }
  else if (bitmap_is_set_all(&slave_error_mask))
  {
    var->value= const_cast<char *>("ALL");
  }
  else
  {
    /* 10 is enough assuming errors are max 4 digits */
    int i;
    var->value= buff;
    for (i= 1;
         i < MAX_SLAVE_ERROR &&
         (buff - var->value) < SHOW_VAR_FUNC_BUFF_SIZE;
         i++)
    {
      if (bitmap_is_set(&slave_error_mask, i))
      {
        buff= int10_to_str(i, buff, 10);
        *buff++= ',';
      }
    }
    if (var->value != buff)
      buff--;				// Remove last ','
    if (i < MAX_SLAVE_ERROR)
      buff= my_stpcpy(buff, "...");  // Couldn't show all errors
    *buff=0;
  }
  return 0;
}

static st_show_var_func_container
show_slave_skip_errors_cont = { &show_slave_skip_errors };


static SHOW_VAR fixed_vars[]= {
  {"log_slave_updates",       (char*) &opt_log_slave_updates,       SHOW_MY_BOOL},
  {"relay_log" , (char*) &opt_relay_logname, SHOW_CHAR_PTR},
  {"relay_log_index", (char*) &opt_relaylog_index_name, SHOW_CHAR_PTR},
  {"relay_log_info_file", (char*) &relay_log_info_file, SHOW_CHAR_PTR},
  {"relay_log_space_limit",   (char*) &relay_log_space_limit,       SHOW_LONGLONG},
  {"slave_load_tmpdir",       (char*) &slave_load_tmpdir,           SHOW_CHAR_PTR},
  {"slave_skip_errors",       (char*) &show_slave_skip_errors_cont,      SHOW_FUNC},
};

bool sys_var_slave_skip_counter::check(Session *session __attribute__((unused)),
                                       set_var *var)
{
  int result= 0;
  pthread_mutex_lock(&LOCK_active_mi);
  pthread_mutex_lock(&active_mi->rli.run_lock);
  if (active_mi->rli.slave_running)
  {
    my_message(ER_SLAVE_MUST_STOP, ER(ER_SLAVE_MUST_STOP), MYF(0));
    result=1;
  }
  pthread_mutex_unlock(&active_mi->rli.run_lock);
  pthread_mutex_unlock(&LOCK_active_mi);
  var->save_result.uint32_t_value= (uint32_t) var->value->val_int();
  return result;
}


bool sys_var_slave_skip_counter::update(Session *session __attribute__((unused)),
                                        set_var *var)
{
  pthread_mutex_lock(&LOCK_active_mi);
  pthread_mutex_lock(&active_mi->rli.run_lock);
  /*
    The following test should normally never be true as we test this
    in the check function;  To be safe against multiple
    SQL_SLAVE_SKIP_COUNTER request, we do the check anyway
  */
  if (!active_mi->rli.slave_running)
  {
    pthread_mutex_lock(&active_mi->rli.data_lock);
    active_mi->rli.slave_skip_counter= var->save_result.uint32_t_value;
    pthread_mutex_unlock(&active_mi->rli.data_lock);
  }
  pthread_mutex_unlock(&active_mi->rli.run_lock);
  pthread_mutex_unlock(&LOCK_active_mi);
  return 0;
}


bool sys_var_sync_binlog_period::update(Session *session __attribute__((unused)),
                                        set_var *var)
{
  sync_binlog_period= (uint32_t) var->save_result.uint64_t_value;
  return 0;
}

int init_replication_sys_vars()
{
  mysql_append_static_vars(fixed_vars, sizeof(fixed_vars) / sizeof(SHOW_VAR));

  if (mysql_add_sys_var_chain(vars.first, my_long_options))
  {
    /* should not happen */
    fprintf(stderr, "failed to initialize replication system variables");
    unireg_abort(1);
  }
  return 0;
}