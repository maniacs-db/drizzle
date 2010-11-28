/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2009 Sun Microsystems
 *
 *  Authors:
 *
 *    Jay Pipes <joinfu@sun.com>
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
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <iostream>
#include <string>
#include <algorithm>
#include <vector>
#include <unistd.h>
#include "drizzled/gettext.h"
#include "drizzled/message/transaction.pb.h"
#include "drizzled/message/statement_transform.h"
#include "transaction_manager.h"
#include "transaction_file_reader.h"
#include "transaction_log_connection.h"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

#include <boost/program_options.hpp>

using namespace std;
using namespace google;
using namespace drizzled;

namespace po= boost::program_options;

static const char *replace_with_spaces= "\n\r";

static void printStatement(const message::Statement &statement)
{
  vector<string> sql_strings;

  message::transformStatementToSql(statement,
                                   sql_strings,
                                   message::DRIZZLE,
                                   true /* already in transaction */);

  for (vector<string>::iterator sql_string_iter= sql_strings.begin();
       sql_string_iter != sql_strings.end();
       ++sql_string_iter)
  {
    string &sql= *sql_string_iter;

    /*
     * Replace \n and \r with spaces so that SQL statements
     * are always on a single line
     */
    {
      string::size_type found= sql.find_first_of(replace_with_spaces);
      while (found != string::npos)
      {
        sql[found]= ' ';
        found= sql.find_first_of(replace_with_spaces, found);
      }
    }

    /*
     * Embedded NUL characters are a pain in the ass.
     */
    {
      string::size_type found= sql.find_first_of('\0');
      while (found != string::npos)
      {
        sql[found]= '\\';
        sql.insert(found + 1, 1, '0');
        found= sql.find_first_of('\0', found);
      }
    }

    cout << sql << ';' << endl;
  }
}

static bool isEndStatement(const message::Statement &statement)
{
  switch (statement.type())
  {
    case (message::Statement::INSERT):
    {
      const message::InsertData &data= statement.insert_data();
      if (not data.end_segment())
        return false;
      break;
    }
    case (message::Statement::UPDATE):
    {
      const message::UpdateData &data= statement.update_data();
      if (not data.end_segment())
        return false;
      break;
    }
    case (message::Statement::DELETE):
    {
      const message::DeleteData &data= statement.delete_data();
      if (not data.end_segment())
        return false;
      break;
    }
    default:
      return true;
  }
  return true;
}

static bool isEndTransaction(const message::Transaction &transaction)
{
  const message::TransactionContext trx= transaction.transaction_context();

  size_t num_statements= transaction.statement_size();

  /*
   * If any Statement is partial, then we can expect another Transaction
   * message.
   */
  for (size_t x= 0; x < num_statements; ++x)
  {
    const message::Statement &statement= transaction.statement(x);

    if (not isEndStatement(statement))
      return false;
  }

  return true;
}

static void printEvent(const message::Event &event)
{
  switch (event.type())
  {
    case message::Event::STARTUP:
    {
      cout << "-- EVENT: Server startup\n";
      break;
    }
    case message::Event::SHUTDOWN:
    {
      cout << "-- EVENT: Server shutdown\n";
      break;
    }
    default:
    {
      cout << "-- EVENT: Unknown event\n";
      break;
    }
  }
}

static void printTransactionSummary(const message::Transaction &transaction,
                                    bool ignore_events)
{
  static uint64_t last_trx_id= 0;
  const message::TransactionContext trx= transaction.transaction_context();

  if (last_trx_id != trx.transaction_id())
    cout << "\ntransaction_id = " << trx.transaction_id() << endl;

  last_trx_id= trx.transaction_id();

  if (transaction.has_event() && (not ignore_events))
  {
    cout << "\t";
    printEvent(transaction.event());
  }

  size_t num_statements= transaction.statement_size();
  size_t x;

  for (x= 0; x < num_statements; ++x)
  {
    const message::Statement &statement= transaction.statement(x);

    switch (statement.type())
    {
      case (message::Statement::ROLLBACK):
      {
        cout << "\tROLLBACK\n";
        break;
      }
      case (message::Statement::INSERT):
      {
        const message::InsertHeader &header= statement.insert_header();
        const message::TableMetadata &meta= header.table_metadata();
        cout << "\tINSERT INTO `" << meta.table_name() << "`\n";
        break;
      }
      case (message::Statement::DELETE):
      {
        const message::DeleteHeader &header= statement.delete_header();
        const message::TableMetadata &meta= header.table_metadata();
        cout << "\tDELETE FROM `" << meta.table_name() << "`\n";
        break;
      }
      case (message::Statement::UPDATE):
      {
        const message::UpdateHeader &header= statement.update_header();
        const message::TableMetadata &meta= header.table_metadata();
        cout << "\tUPDATE `" << meta.table_name() << "`\n";
        break;
      }
      case (message::Statement::TRUNCATE_TABLE):
      {
        const message::TableMetadata &meta= statement.truncate_table_statement().table_metadata();
        cout << "\tTRUNCATE TABLE `" << meta.table_name() << "`\n";
        break;
      }
      case (message::Statement::CREATE_SCHEMA):
      {
        const message::Schema &schema= statement.create_schema_statement().schema();
        cout << "\tCREATE SCHEMA `" << schema.name() << "`\n";
        break;
      }
      case (message::Statement::ALTER_SCHEMA):
      {
        const message::Schema &schema= statement.alter_schema_statement().before();
        cout << "\tALTER SCHEMA `" << schema.name() << "`\n";
        break;
      }
      case (message::Statement::DROP_SCHEMA):
      {
        cout << "\tDROP SCHEMA `" << statement.drop_schema_statement().schema_name() << "`\n";
        break;
      }
      case (message::Statement::CREATE_TABLE):
      {
        const message::Table &table= statement.create_table_statement().table();
        cout << "\tCREATE TABLE `" << table.name() << "`\n";
        break;
      }
      case (message::Statement::ALTER_TABLE):
      {
        const message::Table &table= statement.alter_table_statement().before();
        cout << "\tALTER TABLE `" << table.name() << "`\n";
        break;
      }
      case (message::Statement::DROP_TABLE):
      {
        const message::TableMetadata &meta= statement.drop_table_statement().table_metadata();
        cout << "\tDROP TABLE `" << meta.table_name() << "`\n";
        break;
      }
      case (message::Statement::SET_VARIABLE):
      {
        const message::FieldMetadata &meta= statement.set_variable_statement().variable_metadata();
        cout << "\tSET VARIABLE " << meta.name() << "\n";
        break;
      }
      case (message::Statement::RAW_SQL):
      {
        cout << "\tRAW SQL\n";
        break;
      }
      default:
        cout << "\tUnhandled Statement Type\n";
    }
  }
}

static void printTransaction(const message::Transaction &transaction,
                             bool ignore_events,
                             bool print_as_raw)
{
  static uint64_t last_trx_id= 0;
  bool should_commit= true;
  const message::TransactionContext trx= transaction.transaction_context();

  /*
   * First check to see if this is an event message.
   */
  if (transaction.has_event())
  {
    last_trx_id= trx.transaction_id();
    if (not ignore_events)
    {
      if (print_as_raw)
        transaction.PrintDebugString();
      else
        printEvent(transaction.event());
    }
    return;
  }

  if (print_as_raw)
  {
    transaction.PrintDebugString();
    return;
  }

  size_t num_statements= transaction.statement_size();
  size_t x;

  /*
   * One way to determine when a new transaction begins is when the
   * transaction id changes (if all transactions have their GPB messages
   * grouped together, which this program will). We check that here.
   */
  if (trx.transaction_id() != last_trx_id)
    cout << "START TRANSACTION;" << endl;

  last_trx_id= trx.transaction_id();

  for (x= 0; x < num_statements; ++x)
  {
    const message::Statement &statement= transaction.statement(x);

    if (should_commit)
      should_commit= isEndStatement(statement);

    /* A ROLLBACK would be the only Statement within the Transaction
     * since all other Statements will have been deleted from the
     * Transaction message, so we should fall out of this loop immediately.
     * We don't want to issue an unnecessary COMMIT, so we change
     * should_commit to false here.
     */
    if (statement.type() == message::Statement::ROLLBACK)
      should_commit= false;

    printStatement(statement);
  }

  /*
   * If ALL Statements are end segments, we can commit this Transaction.
   * We can also check to see if the transaction_id changed, but this
   * wouldn't work for the last Transaction in the transaction log since
   * we don't have another Transaction to compare to. Checking for all
   * end segments (like we do above) covers this case.
   */
  if (should_commit)
    cout << "COMMIT;" << endl;
}

static void processTransactionMessage(TransactionManager &trx_mgr, 
                                      const message::Transaction &transaction, 
                                      bool summarize,
                                      bool ignore_events,
                                      bool print_as_raw)
{
  if (not isEndTransaction(transaction))
  {
    trx_mgr.store(transaction);
  }
  else
  {
    const message::TransactionContext trx= transaction.transaction_context();
    uint64_t transaction_id= trx.transaction_id();

    /*
     * If there are any previous Transaction messages for this transaction,
     * store this one, then output all of them together.
     */
    if (trx_mgr.contains(transaction_id))
    {
      trx_mgr.store(transaction);

      uint32_t size= trx_mgr.getTransactionBufferSize(transaction_id);
      uint32_t idx= 0;

      while (idx != size)
      {
        message::Transaction new_trx;
        trx_mgr.getTransactionMessage(new_trx, transaction_id, idx);
        if (summarize)
        {
          printTransactionSummary(new_trx, ignore_events);
        }
        else
        {
          printTransaction(new_trx, ignore_events, print_as_raw);
        }
        idx++;
      }

      /* No longer need this transaction */
      trx_mgr.remove(transaction_id);
    }
    else
    {
      if (summarize)
      {
        printTransactionSummary(transaction, ignore_events);
      }
      else
      {
        printTransaction(transaction, ignore_events, print_as_raw);
      }
    }
  }
}

int main(int argc, char* argv[])
{
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  int opt_start_pos= 0;
  uint64_t opt_transaction_id= 0;
  uint32_t opt_drizzle_port= 0; 
  string current_user, opt_password, opt_protocol, current_host;
  bool use_drizzle_protocol= false; 

  /*
   * Setup program options
   */
  po::options_description desc("Program options");
  desc.add_options()
    ("help", N_("Display help and exit"))
    ("use-innodb-replication-log", N_("Read from the innodb transaction log"))
    ("user,u", po::value<string>(&current_user)->default_value(""), 
      N_("User for login if not current user."))
    ("port,p", po::value<uint32_t>(&opt_drizzle_port)->default_value(0), 
      N_("Port number to use for connection."))
    ("password,P", po::value<string>(&opt_password)->default_value(""), 
      N_("Password to use when connecting to server"))
    ("protocol",po::value<string>(&opt_protocol)->default_value("mysql"),
      N_("The protocol of connection (mysql or drizzle)."))
    ("checksum", N_("Perform checksum"))
    ("ignore-events", N_("Ignore event messages"))
    ("input-file", po::value< vector<string> >(), N_("Transaction log file"))
    ("raw", N_("Print raw Protobuf messages instead of SQL"))
    ("start-pos",
      po::value<int>(&opt_start_pos),
      N_("Start reading from the given file position"))
    ("transaction-id",
      po::value<uint64_t>(&opt_transaction_id),
      N_("Only output for the given transaction ID"))
    ("summarize", N_("Summarize message contents"));

  /*
   * We allow one positional argument that will be transaction file name
   */
  po::positional_options_description pos;
  pos.add("input-file", 1);

  /*
   * Parse the program options
   */
  po::variables_map vm;
  po::store(po::command_line_parser(argc, argv).
            options(desc).positional(pos).run(), vm);
  po::notify(vm);

  if (vm.count("help"))
  {
    cerr << desc << endl;
    return -1;
  }

  if (not vm.count("input-file") && not vm.count("use-innodb-replication-log"))
  {
    cerr << desc << endl;
    return -1;
  }

  /*
   * Specifying both a transaction ID and a start position
   * is not logical.
   */
  if (vm.count("start-pos") && vm.count("transaction-id"))
  {
    cerr << _("Cannot use --start-pos and --transaction-id together\n");
    return -1;
  }

  if (vm.count("summarize") && (vm.count("raw") || vm.count("transaction-id")))
  {
    cerr << _("Cannot use --summarize with either --raw or --transaction-id\n");
    return -1;
  }

  bool do_checksum= vm.count("checksum") ? true : false;
  bool use_innodb_replication_log= vm.count("use-innodb-replication-log") ? true : false;
  bool ignore_events= vm.count("ignore-events") ? true : false;
  bool print_as_raw= vm.count("raw") ? true : false;
  bool summarize= vm.count("summarize") ? true : false;

  if (use_innodb_replication_log)
  {
    TransactionLogConnection *connection = new TransactionLogConnection(current_host, opt_drizzle_port,
      current_user, opt_password, use_drizzle_protocol);

    string query_string;
    if (vm.count("transaction-id"))
    {
      query_string= "SELECT TRANSACTION_MESSAGE_BINARY, TRANSACTION_LENGTH FROM DATA_DICTIONARY.INNODB_REPLICATION_LOG WHERE TRANSACTION_ID= opt_transaction_id";
    }
    else
    {
      query_string= "SELECT TRANSACTION_MESSAGE_BINARY, TRANSACTION_LENGTH FROM DATA_DICTIONARY.INNODB_REPLICATION_LOG";
    }

    drizzle_result_st *result= connection->query(query_string);

    drizzle_row_t row;
    while ((row= drizzle_row_next(result)))
    {
      char* data= (char*)row[0];
      uint64_t length= (row[1]) ? boost::lexical_cast<uint64_t>(row[1]) : 0;

      message::Transaction transaction;
      TransactionManager trx_mgr;

      transaction.ParseFromArray(data, length);

      processTransactionMessage(trx_mgr, transaction, 
                                summarize, ignore_events, print_as_raw);
    }    
  }
  else // file based transaction log 
  {
    string filename= vm["input-file"].as< vector<string> >()[0];

    TransactionFileReader fileReader;

    if (not fileReader.openFile(filename, opt_start_pos))
    {
      cerr << fileReader.getErrorString() << endl;
      return -1;
    }

    message::Transaction transaction;
    TransactionManager trx_mgr;
    uint32_t checksum= 0;

    while (fileReader.getNextTransaction(transaction, &checksum))
    {
      const message::TransactionContext trx= transaction.transaction_context();
      uint64_t transaction_id= trx.transaction_id();
    
      /*
       * If we are given a transaction ID, we only look for that one and
       * print it out.
       */
      if (vm.count("transaction-id"))
      {
        if (opt_transaction_id == transaction_id)
        {
          processTransactionMessage(trx_mgr, transaction, summarize, 
                                    ignore_events, print_as_raw);
        }
        else
        {
          continue;
        }
      }
      else 
      {
        /*
         * No transaction ID given, so process all messages.
         */
        processTransactionMessage(trx_mgr, transaction, summarize,
                                  ignore_events, print_as_raw);
      }  

      if (do_checksum)
      {
        uint32_t calculated= fileReader.checksumLastReadTransaction();
        if (checksum != calculated)
        {
          cerr << _("Checksum failed. Wanted ")
               << checksum
               << _(" got ")
               << calculated
               << endl;
        }
      }
    } // end while

    string error= fileReader.getErrorString();

    if (error != "EOF")
    {
      cerr << error << endl;
      return 1;
    }
  }

  return 0;
}
