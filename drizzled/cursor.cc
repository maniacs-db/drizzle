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

/**
  @file Cursor.cc

  Handler-calling-functions
*/

#include "drizzled/server_includes.h"
#include "mysys/hash.h"
#include "drizzled/error.h"
#include "drizzled/gettext.h"
#include "drizzled/probes.h"
#include "drizzled/sql_parse.h"
#include "drizzled/cost_vect.h"
#include "drizzled/session.h"
#include "drizzled/sql_base.h"
#include "drizzled/replication_services.h"
#include "drizzled/lock.h"
#include "drizzled/item/int.h"
#include "drizzled/item/empty_string.h"
#include "drizzled/unireg.h" // for mysql_frm_type
#include "drizzled/field/timestamp.h"
#include "drizzled/message/table.pb.h"
#include "drizzled/plugin/client.h"

using namespace std;
using namespace drizzled;

/****************************************************************************
** General Cursor functions
****************************************************************************/
Cursor::~Cursor(void)
{
  assert(locked == false);
  /* TODO: assert(inited == NONE); */
}


Cursor *Cursor::clone(MEM_ROOT *mem_root)
{
  Cursor *new_handler= table->s->db_type()->getCursor(*table->s, mem_root);

  /*
    Allocate Cursor->ref here because otherwise ha_open will allocate it
    on this->table->mem_root and we will not be able to reclaim that memory
    when the clone Cursor object is destroyed.
  */
  if (!(new_handler->ref= (unsigned char*) alloc_root(mem_root, ALIGN_SIZE(ref_length)*2)))
    return NULL;
  if (new_handler && !new_handler->ha_open(table,
                                           table->s->normalized_path.str,
                                           table->getDBStat(),
                                           HA_OPEN_IGNORE_IF_LOCKED))
    return new_handler;
  return NULL;
}

int Cursor::ha_index_init(uint32_t idx, bool sorted)
{
  int result;
  assert(inited == NONE);
  if (!(result= index_init(idx, sorted)))
    inited=INDEX;
  end_range= NULL;
  return result;
}

int Cursor::ha_index_end()
{
  assert(inited==INDEX);
  inited=NONE;
  end_range= NULL;
  return(index_end());
}

int Cursor::ha_rnd_init(bool scan)
{
  int result;
  assert(inited==NONE || (inited==RND && scan));
  inited= (result= rnd_init(scan)) ? NONE: RND;

  return result;
}

int Cursor::ha_rnd_end()
{
  assert(inited==RND);
  inited=NONE;
  return(rnd_end());
}

int Cursor::ha_index_or_rnd_end()
{
  return inited == INDEX ? ha_index_end() : inited == RND ? ha_rnd_end() : 0;
}

void Cursor::ha_start_bulk_insert(ha_rows rows)
{
  estimation_rows_to_insert= rows;
  start_bulk_insert(rows);
}

int Cursor::ha_end_bulk_insert()
{
  estimation_rows_to_insert= 0;
  return end_bulk_insert();
}

void Cursor::change_table_ptr(Table *table_arg, TableShare *share)
{
  table= table_arg;
  table_share= share;
}

const key_map *Cursor::keys_to_use_for_scanning()
{
  return &key_map_empty;
}

bool Cursor::has_transactions()
{
  return (table->s->db_type()->check_flag(HTON_BIT_DOES_TRANSACTIONS));
}

void Cursor::ha_statistic_increment(ulong SSV::*offset) const
{
  status_var_increment(table->in_use->status_var.*offset);
}

void **Cursor::ha_data(Session *session) const
{
  return session_ha_data(session, engine);
}

Session *Cursor::ha_session(void) const
{
  assert(!table || !table->in_use || table->in_use == current_session);
  return (table && table->in_use) ? table->in_use : current_session;
}


bool Cursor::is_fatal_error(int error, uint32_t flags)
{
  if (!error ||
      ((flags & HA_CHECK_DUP_KEY) &&
       (error == HA_ERR_FOUND_DUPP_KEY ||
        error == HA_ERR_FOUND_DUPP_UNIQUE)))
    return false;
  return true;
}


ha_rows Cursor::records() { return stats.records; }

/**
  Open database-Cursor.

  Try O_RDONLY if cannot open as O_RDWR
  Don't wait for locks if not HA_OPEN_WAIT_IF_LOCKED is set
*/
int Cursor::ha_open(Table *table_arg, const char *name, int mode,
                     int test_if_locked)
{
  int error;

  table= table_arg;
  assert(table->s == table_share);
  assert(alloc_root_inited(&table->mem_root));

  if ((error=open(name,mode,test_if_locked)))
  {
    if ((error == EACCES || error == EROFS) && mode == O_RDWR &&
	(table->db_stat & HA_TRY_READ_ONLY))
    {
      table->db_stat|=HA_READ_ONLY;
      error=open(name,O_RDONLY,test_if_locked);
    }
  }
  if (error)
  {
    my_errno= error;                            /* Safeguard */
  }
  else
  {
    if (table->s->db_options_in_use & HA_OPTION_READ_ONLY_DATA)
      table->db_stat|=HA_READ_ONLY;
    (void) extra(HA_EXTRA_NO_READCHECK);	// Not needed in SQL

    /* ref is already allocated for us if we're called from Cursor::clone() */
    if (!ref && !(ref= (unsigned char*) alloc_root(&table->mem_root,
                                          ALIGN_SIZE(ref_length)*2)))
    {
      close();
      error=HA_ERR_OUT_OF_MEM;
    }
    else
      dup_ref=ref+ALIGN_SIZE(ref_length);
  }
  return error;
}

/**
  one has to use this method when to find
  random position by record as the plain
  position() call doesn't work for some
  handlers for random position
*/

int Cursor::rnd_pos_by_record(unsigned char *record)
{
  register int error;

  position(record);
  if (inited && (error= ha_index_end()))
    return error;
  if ((error= ha_rnd_init(false)))
    return error;

  return rnd_pos(record, ref);
}

/**
  Read first row (only) from a table.

  This is never called for InnoDB tables, as these table types
  has the HA_STATS_RECORDS_IS_EXACT set.
*/
int Cursor::read_first_row(unsigned char * buf, uint32_t primary_key)
{
  register int error;

  ha_statistic_increment(&SSV::ha_read_first_count);

  /*
    If there is very few deleted rows in the table, find the first row by
    scanning the table.
    TODO remove the test for HA_READ_ORDER
  */
  if (stats.deleted < 10 || primary_key >= MAX_KEY ||
      !(index_flags(primary_key) & HA_READ_ORDER))
  {
    (void) ha_rnd_init(1);
    while ((error= rnd_next(buf)) == HA_ERR_RECORD_DELETED) ;
    (void) ha_rnd_end();
  }
  else
  {
    /* Find the first row through the primary key */
    (void) ha_index_init(primary_key, 0);
    error=index_first(buf);
    (void) ha_index_end();
  }
  return error;
}

/**
  Generate the next auto-increment number based on increment and offset.
  computes the lowest number
  - strictly greater than "nr"
  - of the form: auto_increment_offset + N * auto_increment_increment

  In most cases increment= offset= 1, in which case we get:
  @verbatim 1,2,3,4,5,... @endverbatim
    If increment=10 and offset=5 and previous number is 1, we get:
  @verbatim 1,5,15,25,35,... @endverbatim
*/
inline uint64_t
compute_next_insert_id(uint64_t nr,struct system_variables *variables)
{
  if (variables->auto_increment_increment == 1)
    return (nr+1); // optimization of the formula below
  nr= (((nr+ variables->auto_increment_increment -
         variables->auto_increment_offset)) /
       (uint64_t) variables->auto_increment_increment);
  return (nr* (uint64_t) variables->auto_increment_increment +
          variables->auto_increment_offset);
}


void Cursor::adjust_next_insert_id_after_explicit_value(uint64_t nr)
{
  /*
    If we have set Session::next_insert_id previously and plan to insert an
    explicitely-specified value larger than this, we need to increase
    Session::next_insert_id to be greater than the explicit value.
  */
  if ((next_insert_id > 0) && (nr >= next_insert_id))
    set_next_insert_id(compute_next_insert_id(nr, &table->in_use->variables));
}


/**
  Compute a previous insert id

  Computes the largest number X:
  - smaller than or equal to "nr"
  - of the form: auto_increment_offset + N * auto_increment_increment
    where N>=0.

  @param nr            Number to "round down"
  @param variables     variables struct containing auto_increment_increment and
                       auto_increment_offset

  @return
    The number X if it exists, "nr" otherwise.
*/
inline uint64_t
prev_insert_id(uint64_t nr, struct system_variables *variables)
{
  if (unlikely(nr < variables->auto_increment_offset))
  {
    /*
      There's nothing good we can do here. That is a pathological case, where
      the offset is larger than the column's max possible value, i.e. not even
      the first sequence value may be inserted. User will receive warning.
    */
    return nr;
  }
  if (variables->auto_increment_increment == 1)
    return nr; // optimization of the formula below
  nr= (((nr - variables->auto_increment_offset)) /
       (uint64_t) variables->auto_increment_increment);
  return (nr * (uint64_t) variables->auto_increment_increment +
          variables->auto_increment_offset);
}


/**
  Update the auto_increment field if necessary.

  Updates columns with type NEXT_NUMBER if:

  - If column value is set to NULL (in which case auto_increment_field_not_null is false)
  - If column is set to 0 and (sql_mode & MODE_NO_AUTO_VALUE_ON_ZERO) is not
    set. In the future we will only set NEXT_NUMBER fields if one sets them
    to NULL (or they are not included in the insert list).

    In those cases, we check if the currently reserved interval still has
    values we have not used. If yes, we pick the smallest one and use it.
    Otherwise:

  - If a list of intervals has been provided to the statement via SET
    INSERT_ID or via an Intvar_log_event (in a replication slave), we pick the
    first unused interval from this list, consider it as reserved.

  - Otherwise we set the column for the first row to the value
    next_insert_id(get_auto_increment(column))) which is usually
    max-used-column-value+1.
    We call get_auto_increment() for the first row in a multi-row
    statement. get_auto_increment() will tell us the interval of values it
    reserved for us.

  - In both cases, for the following rows we use those reserved values without
    calling the Cursor again (we just progress in the interval, computing
    each new value from the previous one). Until we have exhausted them, then
    we either take the next provided interval or call get_auto_increment()
    again to reserve a new interval.

  - In both cases, the reserved intervals are remembered in
    session->auto_inc_intervals_in_cur_stmt_for_binlog if statement-based
    binlogging; the last reserved interval is remembered in
    auto_inc_interval_for_cur_row.

    The idea is that generated auto_increment values are predictable and
    independent of the column values in the table.  This is needed to be
    able to replicate into a table that already has rows with a higher
    auto-increment value than the one that is inserted.

    After we have already generated an auto-increment number and the user
    inserts a column with a higher value than the last used one, we will
    start counting from the inserted value.

    This function's "outputs" are: the table's auto_increment field is filled
    with a value, session->next_insert_id is filled with the value to use for the
    next row, if a value was autogenerated for the current row it is stored in
    session->insert_id_for_cur_row, if get_auto_increment() was called
    session->auto_inc_interval_for_cur_row is modified, if that interval is not
    present in session->auto_inc_intervals_in_cur_stmt_for_binlog it is added to
    this list.

  @todo
    Replace all references to "next number" or NEXT_NUMBER to
    "auto_increment", everywhere (see below: there is
    table->auto_increment_field_not_null, and there also exists
    table->next_number_field, it's not consistent).

  @retval
    0	ok
  @retval
    HA_ERR_AUTOINC_READ_FAILED  get_auto_increment() was called and
    returned ~(uint64_t) 0
  @retval
    HA_ERR_AUTOINC_ERANGE storing value in field caused strict mode
    failure.
*/

#define AUTO_INC_DEFAULT_NB_ROWS 1 // Some prefer 1024 here
#define AUTO_INC_DEFAULT_NB_MAX_BITS 16
#define AUTO_INC_DEFAULT_NB_MAX ((1 << AUTO_INC_DEFAULT_NB_MAX_BITS) - 1)

int Cursor::update_auto_increment()
{
  uint64_t nr, nb_reserved_values;
  bool append= false;
  Session *session= table->in_use;
  struct system_variables *variables= &session->variables;

  /*
    next_insert_id is a "cursor" into the reserved interval, it may go greater
    than the interval, but not smaller.
  */
  assert(next_insert_id >= auto_inc_interval_for_cur_row.minimum());

  /* We check if auto_increment_field_not_null is false
     for an auto increment column, not a magic value like NULL is.
     same as sql_mode=NO_AUTO_VALUE_ON_ZERO */

  if ((nr= table->next_number_field->val_int()) != 0
      || table->auto_increment_field_not_null)
  {
    /*
      Update next_insert_id if we had already generated a value in this
      statement (case of INSERT VALUES(null),(3763),(null):
      the last NULL needs to insert 3764, not the value of the first NULL plus
      1).
    */
    adjust_next_insert_id_after_explicit_value(nr);
    insert_id_for_cur_row= 0; // didn't generate anything

    return 0;
  }

  if ((nr= next_insert_id) >= auto_inc_interval_for_cur_row.maximum())
  {
    /* next_insert_id is beyond what is reserved, so we reserve more. */
    const Discrete_interval *forced=
      session->auto_inc_intervals_forced.get_next();
    if (forced != NULL)
    {
      nr= forced->minimum();
      nb_reserved_values= forced->values();
    }
    else
    {
      /*
        Cursor::estimation_rows_to_insert was set by
        Cursor::ha_start_bulk_insert(); if 0 it means "unknown".
      */
      uint32_t nb_already_reserved_intervals=
        session->auto_inc_intervals_in_cur_stmt_for_binlog.nb_elements();
      uint64_t nb_desired_values;
      /*
        If an estimation was given to the engine:
        - use it.
        - if we already reserved numbers, it means the estimation was
        not accurate, then we'll reserve 2*AUTO_INC_DEFAULT_NB_ROWS the 2nd
        time, twice that the 3rd time etc.
        If no estimation was given, use those increasing defaults from the
        start, starting from AUTO_INC_DEFAULT_NB_ROWS.
        Don't go beyond a max to not reserve "way too much" (because
        reservation means potentially losing unused values).
      */
      if (nb_already_reserved_intervals == 0 &&
          (estimation_rows_to_insert > 0))
        nb_desired_values= estimation_rows_to_insert;
      else /* go with the increasing defaults */
      {
        /* avoid overflow in formula, with this if() */
        if (nb_already_reserved_intervals <= AUTO_INC_DEFAULT_NB_MAX_BITS)
        {
          nb_desired_values= AUTO_INC_DEFAULT_NB_ROWS *
            (1 << nb_already_reserved_intervals);
          set_if_smaller(nb_desired_values, (uint64_t)AUTO_INC_DEFAULT_NB_MAX);
        }
        else
          nb_desired_values= AUTO_INC_DEFAULT_NB_MAX;
      }
      /* This call ignores all its parameters but nr, currently */
      get_auto_increment(variables->auto_increment_offset,
                         variables->auto_increment_increment,
                         nb_desired_values, &nr,
                         &nb_reserved_values);
      if (nr == ~(uint64_t) 0)
        return HA_ERR_AUTOINC_READ_FAILED;  // Mark failure

      /*
        That rounding below should not be needed when all engines actually
        respect offset and increment in get_auto_increment(). But they don't
        so we still do it. Wonder if for the not-first-in-index we should do
        it. Hope that this rounding didn't push us out of the interval; even
        if it did we cannot do anything about it (calling the engine again
        will not help as we inserted no row).
      */
      nr= compute_next_insert_id(nr-1, variables);
    }

    if (table->s->next_number_keypart == 0)
    {
      /* We must defer the appending until "nr" has been possibly truncated */
      append= true;
    }
  }

  if (unlikely(table->next_number_field->store((int64_t) nr, true)))
  {
    /*
      first test if the query was aborted due to strict mode constraints
    */
    if (session->killed == Session::KILL_BAD_DATA)
      return HA_ERR_AUTOINC_ERANGE;

    /*
      field refused this value (overflow) and truncated it, use the result of
      the truncation (which is going to be inserted); however we try to
      decrease it to honour auto_increment_* variables.
      That will shift the left bound of the reserved interval, we don't
      bother shifting the right bound (anyway any other value from this
      interval will cause a duplicate key).
    */
    nr= prev_insert_id(table->next_number_field->val_int(), variables);
    if (unlikely(table->next_number_field->store((int64_t) nr, true)))
      nr= table->next_number_field->val_int();
  }
  if (append)
  {
    auto_inc_interval_for_cur_row.replace(nr, nb_reserved_values,
                                          variables->auto_increment_increment);
  }

  /*
    Record this autogenerated value. If the caller then
    succeeds to insert this value, it will call
    record_first_successful_insert_id_in_cur_stmt()
    which will set first_successful_insert_id_in_cur_stmt if it's not
    already set.
  */
  insert_id_for_cur_row= nr;
  /*
    Set next insert id to point to next auto-increment value to be able to
    handle multi-row statements.
  */
  set_next_insert_id(compute_next_insert_id(nr, variables));

  return 0;
}


/**
  Reserves an interval of auto_increment values from the Cursor.

  offset and increment means that we want values to be of the form
  offset + N * increment, where N>=0 is integer.
  If the function sets *first_value to ~(uint64_t)0 it means an error.
  If the function sets *nb_reserved_values to UINT64_MAX it means it has
  reserved to "positive infinite".

  @param offset
  @param increment
  @param nb_desired_values   how many values we want
  @param first_value         (OUT) the first value reserved by the Cursor
  @param nb_reserved_values  (OUT) how many values the Cursor reserved
*/
void Cursor::get_auto_increment(uint64_t ,
                                 uint64_t ,
                                 uint64_t ,
                                 uint64_t *first_value,
                                 uint64_t *nb_reserved_values)
{
  uint64_t nr;
  int error;

  (void) extra(HA_EXTRA_KEYREAD);
  table->mark_columns_used_by_index_no_reset(table->s->next_number_index);
  index_init(table->s->next_number_index, 1);
  if (table->s->next_number_keypart == 0)
  {						// Autoincrement at key-start
    error=index_last(table->record[1]);
    /*
      MySQL implicitely assumes such method does locking (as MySQL decides to
      use nr+increment without checking again with the Cursor, in
      Cursor::update_auto_increment()), so reserves to infinite.
    */
    *nb_reserved_values= UINT64_MAX;
  }
  else
  {
    unsigned char key[MAX_KEY_LENGTH];
    key_copy(key, table->record[0],
             table->key_info + table->s->next_number_index,
             table->s->next_number_key_offset);
    error= index_read_map(table->record[1], key,
                          make_prev_keypart_map(table->s->next_number_keypart),
                          HA_READ_PREFIX_LAST);
    /*
      MySQL needs to call us for next row: assume we are inserting ("a",null)
      here, we return 3, and next this statement will want to insert
      ("b",null): there is no reason why ("b",3+1) would be the good row to
      insert: maybe it already exists, maybe 3+1 is too large...
    */
    *nb_reserved_values= 1;
  }

  if (error)
    nr=1;
  else
    nr= ((uint64_t) table->next_number_field->
         val_int_offset(table->s->rec_buff_length)+1);
  index_end();
  (void) extra(HA_EXTRA_NO_KEYREAD);
  *first_value= nr;
}


void Cursor::ha_release_auto_increment()
{
  release_auto_increment();
  insert_id_for_cur_row= 0;
  auto_inc_interval_for_cur_row.replace(0, 0, 0);
  if (next_insert_id > 0)
  {
    next_insert_id= 0;
    /*
      this statement used forced auto_increment values if there were some,
      wipe them away for other statements.
    */
    table->in_use->auto_inc_intervals_forced.empty();
  }
}

void Cursor::drop_table(const char *)
{
  close();
}


/**
  Performs checks upon the table.

  @param session                thread doing CHECK Table operation
  @param check_opt          options from the parser

  @retval
    HA_ADMIN_OK               Successful upgrade
  @retval
    HA_ADMIN_NEEDS_UPGRADE    Table has structures requiring upgrade
  @retval
    HA_ADMIN_NEEDS_ALTER      Table has structures requiring ALTER Table
  @retval
    HA_ADMIN_NOT_IMPLEMENTED
*/
int Cursor::ha_check(Session *, HA_CHECK_OPT *)
{
  return HA_ADMIN_OK;
}

/**
  A helper function to mark a transaction read-write,
  if it is started.
*/

inline
void
Cursor::mark_trx_read_write()
{
  Ha_trx_info *ha_info= &ha_session()->ha_data[engine->getSlot()].ha_info[0];
  /*
    When a storage engine method is called, the transaction must
    have been started, unless it's a DDL call, for which the
    storage engine starts the transaction internally, and commits
    it internally, without registering in the ha_list.
    Unfortunately here we can't know know for sure if the engine
    has registered the transaction or not, so we must check.
  */
  if (ha_info->is_started())
  {
      ha_info->set_trx_read_write();
  }
}


/**
  Delete all rows: public interface.

  @sa Cursor::delete_all_rows()
*/

int
Cursor::ha_delete_all_rows()
{
  mark_trx_read_write();

  return delete_all_rows();
}


/**
  Reset auto increment: public interface.

  @sa Cursor::reset_auto_increment()
*/

int
Cursor::ha_reset_auto_increment(uint64_t value)
{
  mark_trx_read_write();

  return reset_auto_increment(value);
}


/**
  Analyze table: public interface.

  @sa Cursor::analyze()
*/

int
Cursor::ha_analyze(Session* session, HA_CHECK_OPT*)
{
  mark_trx_read_write();

  return analyze(session);
}

/**
  Disable indexes: public interface.

  @sa Cursor::disable_indexes()
*/

int
Cursor::ha_disable_indexes(uint32_t mode)
{
  mark_trx_read_write();

  return disable_indexes(mode);
}


/**
  Enable indexes: public interface.

  @sa Cursor::enable_indexes()
*/

int
Cursor::ha_enable_indexes(uint32_t mode)
{
  mark_trx_read_write();

  return enable_indexes(mode);
}


/**
  Discard or import tablespace: public interface.

  @sa Cursor::discard_or_import_tablespace()
*/

int
Cursor::ha_discard_or_import_tablespace(bool discard)
{
  mark_trx_read_write();

  return discard_or_import_tablespace(discard);
}

/**
  Drop table in the engine: public interface.

  @sa Cursor::drop_table()
*/

void
Cursor::closeMarkForDelete(const char *name)
{
  mark_trx_read_write();

  return drop_table(name);
}

/**
  Tell the storage engine that it is allowed to "disable transaction" in the
  Cursor. It is a hint that ACID is not required - it is used in NDB for
  ALTER Table, for example, when data are copied to temporary table.
  A storage engine may treat this hint any way it likes. NDB for example
  starts to commit every now and then automatically.
  This hint can be safely ignored.
*/
int ha_enable_transaction(Session *session, bool on)
{
  int error= 0;

  if ((session->transaction.on= on))
  {
    /*
      Now all storage engines should have transaction handling enabled.
      But some may have it enabled all the time - "disabling" transactions
      is an optimization hint that storage engine is free to ignore.
      So, let's commit an open transaction (if any) now.
    */
    if (!(error= ha_commit_trans(session, 0)))
      if (! session->endTransaction(COMMIT))
        error= 1;

  }
  return error;
}

int Cursor::index_next_same(unsigned char *buf, const unsigned char *key, uint32_t keylen)
{
  int error;
  if (!(error=index_next(buf)))
  {
    ptrdiff_t ptrdiff= buf - table->record[0];
    unsigned char *save_record_0= NULL;
    KEY *key_info= NULL;
    KEY_PART_INFO *key_part;
    KEY_PART_INFO *key_part_end= NULL;

    /*
      key_cmp_if_same() compares table->record[0] against 'key'.
      In parts it uses table->record[0] directly, in parts it uses
      field objects with their local pointers into table->record[0].
      If 'buf' is distinct from table->record[0], we need to move
      all record references. This is table->record[0] itself and
      the field pointers of the fields used in this key.
    */
    if (ptrdiff)
    {
      save_record_0= table->record[0];
      table->record[0]= buf;
      key_info= table->key_info + active_index;
      key_part= key_info->key_part;
      key_part_end= key_part + key_info->key_parts;
      for (; key_part < key_part_end; key_part++)
      {
        assert(key_part->field);
        key_part->field->move_field_offset(ptrdiff);
      }
    }

    if (key_cmp_if_same(table, key, active_index, keylen))
    {
      table->status=STATUS_NOT_FOUND;
      error=HA_ERR_END_OF_FILE;
    }

    /* Move back if necessary. */
    if (ptrdiff)
    {
      table->record[0]= save_record_0;
      for (key_part= key_info->key_part; key_part < key_part_end; key_part++)
        key_part->field->move_field_offset(-ptrdiff);
    }
  }
  return error;
}


/****************************************************************************
** Some general functions that isn't in the Cursor class
****************************************************************************/

/**
  Calculate cost of 'index only' scan for given index and number of records

  @param keynr    Index number
  @param records  Estimated number of records to be retrieved

  @note
    It is assumed that we will read trough the whole key range and that all
    key blocks are half full (normally things are much better). It is also
    assumed that each time we read the next key from the index, the Cursor
    performs a random seek, thus the cost is proportional to the number of
    blocks read.

  @todo
    Consider joining this function and Cursor::read_time() into one
    Cursor::read_time(keynr, records, ranges, bool index_only) function.

  @return
    Estimated cost of 'index only' scan
*/

double Cursor::index_only_read_time(uint32_t keynr, double key_records)
{
  uint32_t keys_per_block= (stats.block_size/2/
			(table->key_info[keynr].key_length + ref_length) + 1);
  return ((double) (key_records + keys_per_block-1) /
          (double) keys_per_block);
}


/****************************************************************************
 * Default MRR implementation (MRR to non-MRR converter)
 ***************************************************************************/

/**
  Get cost and other information about MRR scan over a known list of ranges

  Calculate estimated cost and other information about an MRR scan for given
  sequence of ranges.

  @param keyno           Index number
  @param seq             Range sequence to be traversed
  @param seq_init_param  First parameter for seq->init()
  @param n_ranges_arg    Number of ranges in the sequence, or 0 if the caller
                         can't efficiently determine it
  @param bufsz    INOUT  IN:  Size of the buffer available for use
                         OUT: Size of the buffer that is expected to be actually
                              used, or 0 if buffer is not needed.
  @param flags    INOUT  A combination of HA_MRR_* flags
  @param cost     OUT    Estimated cost of MRR access

  @note
    This method (or an overriding one in a derived class) must check for
    session->killed and return HA_POS_ERROR if it is not zero. This is required
    for a user to be able to interrupt the calculation by killing the
    connection/query.

  @retval
    HA_POS_ERROR  Error or the engine is unable to perform the requested
                  scan. Values of OUT parameters are undefined.
  @retval
    other         OK, *cost contains cost of the scan, *bufsz and *flags
                  contain scan parameters.
*/

ha_rows
Cursor::multi_range_read_info_const(uint32_t keyno, RANGE_SEQ_IF *seq,
                                     void *seq_init_param,
                                     uint32_t ,
                                     uint32_t *bufsz, uint32_t *flags, COST_VECT *cost)
{
  KEY_MULTI_RANGE range;
  range_seq_t seq_it;
  ha_rows rows, total_rows= 0;
  uint32_t n_ranges=0;
  Session *session= current_session;

  /* Default MRR implementation doesn't need buffer */
  *bufsz= 0;

  seq_it= seq->init(seq_init_param, n_ranges, *flags);
  while (!seq->next(seq_it, &range))
  {
    if (unlikely(session->killed != 0))
      return HA_POS_ERROR;

    n_ranges++;
    key_range *min_endp, *max_endp;
    {
      min_endp= range.start_key.length? &range.start_key : NULL;
      max_endp= range.end_key.length? &range.end_key : NULL;
    }
    if ((range.range_flag & UNIQUE_RANGE) && !(range.range_flag & NULL_RANGE))
      rows= 1; /* there can be at most one row */
    else
    {
      if (HA_POS_ERROR == (rows= this->records_in_range(keyno, min_endp,
                                                        max_endp)))
      {
        /* Can't scan one range => can't do MRR scan at all */
        total_rows= HA_POS_ERROR;
        break;
      }
    }
    total_rows += rows;
  }

  if (total_rows != HA_POS_ERROR)
  {
    /* The following calculation is the same as in multi_range_read_info(): */
    *flags |= HA_MRR_USE_DEFAULT_IMPL;
    cost->zero();
    cost->avg_io_cost= 1; /* assume random seeks */
    if ((*flags & HA_MRR_INDEX_ONLY) && total_rows > 2)
      cost->io_count= index_only_read_time(keyno, (uint32_t)total_rows);
    else
      cost->io_count= read_time(keyno, n_ranges, total_rows);
    cost->cpu_cost= (double) total_rows / TIME_FOR_COMPARE + 0.01;
  }
  return total_rows;
}


/**
  Get cost and other information about MRR scan over some sequence of ranges

  Calculate estimated cost and other information about an MRR scan for some
  sequence of ranges.

  The ranges themselves will be known only at execution phase. When this
  function is called we only know number of ranges and a (rough) E(#records)
  within those ranges.

  Currently this function is only called for "n-keypart singlepoint" ranges,
  i.e. each range is "keypart1=someconst1 AND ... AND keypartN=someconstN"

  The flags parameter is a combination of those flags: HA_MRR_SORTED,
  HA_MRR_INDEX_ONLY, HA_MRR_NO_ASSOCIATION, HA_MRR_LIMITS.

  @param keyno           Index number
  @param n_ranges        Estimated number of ranges (i.e. intervals) in the
                         range sequence.
  @param n_rows          Estimated total number of records contained within all
                         of the ranges
  @param bufsz    INOUT  IN:  Size of the buffer available for use
                         OUT: Size of the buffer that will be actually used, or
                              0 if buffer is not needed.
  @param flags    INOUT  A combination of HA_MRR_* flags
  @param cost     OUT    Estimated cost of MRR access

  @retval
    0     OK, *cost contains cost of the scan, *bufsz and *flags contain scan
          parameters.
  @retval
    other Error or can't perform the requested scan
*/

int Cursor::multi_range_read_info(uint32_t keyno, uint32_t n_ranges, uint32_t n_rows,
                                   uint32_t *bufsz, uint32_t *flags, COST_VECT *cost)
{
  *bufsz= 0; /* Default implementation doesn't need a buffer */

  *flags |= HA_MRR_USE_DEFAULT_IMPL;

  cost->zero();
  cost->avg_io_cost= 1; /* assume random seeks */

  /* Produce the same cost as non-MRR code does */
  if (*flags & HA_MRR_INDEX_ONLY)
    cost->io_count= index_only_read_time(keyno, n_rows);
  else
    cost->io_count= read_time(keyno, n_ranges, n_rows);
  return 0;
}


/**
  Initialize the MRR scan

  Initialize the MRR scan. This function may do heavyweight scan
  initialization like row prefetching/sorting/etc (NOTE: but better not do
  it here as we may not need it, e.g. if we never satisfy WHERE clause on
  previous tables. For many implementations it would be natural to do such
  initializations in the first multi_read_range_next() call)

  mode is a combination of the following flags: HA_MRR_SORTED,
  HA_MRR_INDEX_ONLY, HA_MRR_NO_ASSOCIATION

  @param seq             Range sequence to be traversed
  @param seq_init_param  First parameter for seq->init()
  @param n_ranges        Number of ranges in the sequence
  @param mode            Flags, see the description section for the details
  @param buf             INOUT: memory buffer to be used

  @note
    One must have called index_init() before calling this function. Several
    multi_range_read_init() calls may be made in course of one query.

    Until WL#2623 is done (see its text, section 3.2), the following will
    also hold:
    The caller will guarantee that if "seq->init == mrr_ranges_array_init"
    then seq_init_param is an array of n_ranges KEY_MULTI_RANGE structures.
    This property will only be used by NDB Cursor until WL#2623 is done.

    Buffer memory management is done according to the following scenario:
    The caller allocates the buffer and provides it to the callee by filling
    the members of HANDLER_BUFFER structure.
    The callee consumes all or some fraction of the provided buffer space, and
    sets the HANDLER_BUFFER members accordingly.
    The callee may use the buffer memory until the next multi_range_read_init()
    call is made, all records have been read, or until index_end() call is
    made, whichever comes first.

  @retval 0  OK
  @retval 1  Error
*/

int
Cursor::multi_range_read_init(RANGE_SEQ_IF *seq_funcs, void *seq_init_param,
                               uint32_t n_ranges, uint32_t mode,
                               HANDLER_BUFFER *)
{
  mrr_iter= seq_funcs->init(seq_init_param, n_ranges, mode);
  mrr_funcs= *seq_funcs;
  mrr_is_output_sorted= test(mode & HA_MRR_SORTED);
  mrr_have_range= false;
  return 0;
}


/**
  Get next record in MRR scan

  Default MRR implementation: read the next record

  @param range_info  OUT  Undefined if HA_MRR_NO_ASSOCIATION flag is in effect
                          Otherwise, the opaque value associated with the range
                          that contains the returned record.

  @retval 0      OK
  @retval other  Error code
*/

int Cursor::multi_range_read_next(char **range_info)
{
  int result= 0;
  int range_res= 0;

  if (!mrr_have_range)
  {
    mrr_have_range= true;
    goto start;
  }

  do
  {
    /* Save a call if there can be only one row in range. */
    if (mrr_cur_range.range_flag != (UNIQUE_RANGE | EQ_RANGE))
    {
      result= read_range_next();
      /* On success or non-EOF errors jump to the end. */
      if (result != HA_ERR_END_OF_FILE)
        break;
    }
    else
    {
      if (was_semi_consistent_read())
        goto scan_it_again;
      /*
        We need to set this for the last range only, but checking this
        condition is more expensive than just setting the result code.
      */
      result= HA_ERR_END_OF_FILE;
    }

start:
    /* Try the next range(s) until one matches a record. */
    while (!(range_res= mrr_funcs.next(mrr_iter, &mrr_cur_range)))
    {
scan_it_again:
      result= read_range_first(mrr_cur_range.start_key.keypart_map ?
                                 &mrr_cur_range.start_key : 0,
                               mrr_cur_range.end_key.keypart_map ?
                                 &mrr_cur_range.end_key : 0,
                               test(mrr_cur_range.range_flag & EQ_RANGE),
                               mrr_is_output_sorted);
      if (result != HA_ERR_END_OF_FILE)
        break;
    }
  }
  while ((result == HA_ERR_END_OF_FILE) && !range_res);

  *range_info= mrr_cur_range.ptr;
  return result;
}


/* **************************************************************************
 * DS-MRR implementation ends
 ***************************************************************************/

/**
  Read first row between two ranges.

  @param start_key		Start key. Is 0 if no min range
  @param end_key		End key.  Is 0 if no max range
  @param eq_range_arg	        Set to 1 if start_key == end_key
  @param sorted		Set to 1 if result should be sorted per key

  @note
    Record is read into table->record[0]

  @retval
    0			Found row
  @retval
    HA_ERR_END_OF_FILE	No rows in range
  @retval
    \#			Error code
*/
int Cursor::read_range_first(const key_range *start_key,
                             const key_range *end_key,
                             bool eq_range_arg,
                             bool )
{
  int result;

  eq_range= eq_range_arg;
  end_range= 0;
  if (end_key)
  {
    end_range= &save_end_range;
    save_end_range= *end_key;
    key_compare_result_on_equal= ((end_key->flag == HA_READ_BEFORE_KEY) ? 1 :
				  (end_key->flag == HA_READ_AFTER_KEY) ? -1 : 0);
  }
  range_key_part= table->key_info[active_index].key_part;

  if (!start_key)			// Read first record
    result= index_first(table->record[0]);
  else
    result= index_read_map(table->record[0],
                           start_key->key,
                           start_key->keypart_map,
                           start_key->flag);
  if (result)
    return((result == HA_ERR_KEY_NOT_FOUND)
		? HA_ERR_END_OF_FILE
		: result);

  return (compare_key(end_range) <= 0 ? 0 : HA_ERR_END_OF_FILE);
}


/**
  Read next row between two endpoints.

  @note
    Record is read into table->record[0]

  @retval
    0			Found row
  @retval
    HA_ERR_END_OF_FILE	No rows in range
  @retval
    \#			Error code
*/
int Cursor::read_range_next()
{
  int result;

  if (eq_range)
  {
    /* We trust that index_next_same always gives a row in range */
    return(index_next_same(table->record[0],
                                end_range->key,
                                end_range->length));
  }
  result= index_next(table->record[0]);
  if (result)
    return result;
  return(compare_key(end_range) <= 0 ? 0 : HA_ERR_END_OF_FILE);
}


/**
  Compare if found key (in row) is over max-value.

  @param range		range to compare to row. May be 0 for no range

  @seealso
    key.cc::key_cmp()

  @return
    The return value is SIGN(key_in_row - range_key):

    - 0   : Key is equal to range or 'range' == 0 (no range)
    - -1  : Key is less than range
    - 1   : Key is larger than range
*/
int Cursor::compare_key(key_range *range)
{
  int cmp;
  if (!range || in_range_check_pushed_down)
    return 0;					// No max range
  cmp= key_cmp(range_key_part, range->key, range->length);
  if (!cmp)
    cmp= key_compare_result_on_equal;
  return cmp;
}


/*
  Same as compare_key() but doesn't check have in_range_check_pushed_down.
  This is used by index condition pushdown implementation.
*/

int Cursor::compare_key2(key_range *range)
{
  int cmp;
  if (!range)
    return 0;					// no max range
  cmp= key_cmp(range_key_part, range->key, range->length);
  if (!cmp)
    cmp= key_compare_result_on_equal;
  return cmp;
}

int Cursor::index_read_idx_map(unsigned char * buf, uint32_t index,
                                const unsigned char * key,
                                key_part_map keypart_map,
                                enum ha_rkey_function find_flag)
{
  int error, error1;
  error= index_init(index, 0);
  if (!error)
  {
    error= index_read_map(buf, key, keypart_map, find_flag);
    error1= index_end();
  }
  return error ?  error : error1;
}

/**
  Check if the conditions for row-based binlogging is correct for the table.

  A row in the given table should be replicated if:
  - It is not a temporary table
*/

static bool log_row_for_replication(Table* table,
                                    const unsigned char *before_record,
                                    const unsigned char *after_record)
{
  ReplicationServices &replication_services= ReplicationServices::singleton();
  Session *const session= table->in_use;

  if (table->s->tmp_table || ! replication_services.isActive())
    return false;

  switch (session->lex->sql_command)
  {
  case SQLCOM_REPLACE:
  case SQLCOM_REPLACE_SELECT:
    /*
     * This is a total hack because of the code that is
     * in write_record() in sql_insert.cc. During
     * a REPLACE statement, a call to ha_write_row() is
     * called.  If it fails, then a call to ha_delete_row()
     * is called, followed by a repeat of the original
     * call to ha_write_row().  So, log_row_for_replication
     * could be called either once or twice for a REPLACE
     * statement.  The below looks at the values of before_record
     * and after_record to determine which call to this
     * function is for the delete or the insert, since NULL
     * is passed for after_record for the delete and NULL is
     * passed for before_record for the insert...
     *
     * In addition, there is an optimization that allows an
     * engine to convert the above delete + insert into an
     * update, so we must also check for this case below...
     */
    if (after_record == NULL)
    {
      replication_services.deleteRecord(session, table);
      /* 
       * We set the "current" statement message to NULL.  This triggers
       * the replication services component to generate a new statement
       * message for the inserted record which will come next.
       */
      replication_services.finalizeStatement(*session->getStatementMessage(), session);
    }
    else
    {
      if (before_record == NULL)
        replication_services.insertRecord(session, table);
      else
        replication_services.updateRecord(session, table, before_record, after_record);
    }
    break;
  case SQLCOM_INSERT:
  case SQLCOM_INSERT_SELECT:
    /*
     * The else block below represents an 
     * INSERT ... ON DUPLICATE KEY UPDATE that
     * has hit a key conflict and actually done
     * an update.
     */
    if (before_record == NULL)
      replication_services.insertRecord(session, table);
    else
      replication_services.updateRecord(session, table, before_record, after_record);
    break;

  case SQLCOM_UPDATE:
    replication_services.updateRecord(session, table, before_record, after_record);
    break;

  case SQLCOM_DELETE:
    replication_services.deleteRecord(session, table);
    break;
  default:
    break;
  }

  return false;
}

int Cursor::ha_external_lock(Session *session, int lock_type)
{
  /*
    Whether this is lock or unlock, this should be true, and is to verify that
    if get_auto_increment() was called (thus may have reserved intervals or
    taken a table lock), ha_release_auto_increment() was too.
  */
  assert(next_insert_id == 0);

  /*
    We cache the table flags if the locking succeeded. Otherwise, we
    keep them as they were when they were fetched in ha_open().
  */

  int error= external_lock(session, lock_type);

  return error;
}


/**
  Check Cursor usage and reset state of file to after 'open'
*/
int Cursor::ha_reset()
{
  /* Check that we have called all proper deallocation functions */
  assert((unsigned char*) table->def_read_set.getBitmap() +
              table->s->column_bitmap_size ==
              (unsigned char*) table->def_write_set.getBitmap());
  assert(table->s->all_set.isSetAll());
  assert(table->key_read == 0);
  /* ensure that ha_index_end / ha_rnd_end has been called */
  assert(inited == NONE);
  /* Free cache used by filesort */
  table->free_io_cache();
  /* reset the bitmaps to point to defaults */
  table->default_column_bitmaps();
  return(reset());
}


int Cursor::ha_write_row(unsigned char *buf)
{
  int error;

  /*
   * If we have a timestamp column, update it to the current time
   *
   * @TODO Technically, the below two lines can be take even further out of the
   * Cursor interface and into the fill_record() method.
   */
  if (table->timestamp_field_type & TIMESTAMP_AUTO_SET_ON_INSERT)
    table->timestamp_field->set_time();

  mark_trx_read_write();

  if (unlikely(error= write_row(buf)))
  {
    return error;
  }

  if (unlikely(log_row_for_replication(table, NULL, buf)))
    return HA_ERR_RBR_LOGGING_FAILED;

  return 0;
}


int Cursor::ha_update_row(const unsigned char *old_data, unsigned char *new_data)
{
  int error;

  /*
    Some storage engines require that the new record is in record[0]
    (and the old record is in record[1]).
   */
  assert(new_data == table->record[0]);

  mark_trx_read_write();

  if (unlikely(error= update_row(old_data, new_data)))
  {
    return error;
  }

  if (unlikely(log_row_for_replication(table, old_data, new_data)))
    return HA_ERR_RBR_LOGGING_FAILED;

  return 0;
}

int Cursor::ha_delete_row(const unsigned char *buf)
{
  int error;

  mark_trx_read_write();

  if (unlikely(error= delete_row(buf)))
    return error;

  if (unlikely(log_row_for_replication(table, buf, NULL)))
    return HA_ERR_RBR_LOGGING_FAILED;

  return 0;
}
