# xb_stream.sh - test for xtrabackup
# Copyright (C) 2009-2011 Percona Inc.

# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
. inc/common.sh

init
run_mysqld
load_dbase_schema sakila
load_dbase_data sakila

# Take backup
mkdir -p $topdir/backup
run_cmd ${IB_BIN} --socket=$mysql_socket --stream=tar $topdir/backup > $topdir/backup/out.tar 2> $OUTFILE 

stop_mysqld
# Remove datadir
rm -r $mysql_datadir
# Restore sakila
vlog "Applying log"
backup_dir=$topdir/backup
cd $backup_dir
tar -ixvf out.tar
cd - >/dev/null 2>&1 
echo "###########" >> $OUTFILE
echo "# PREPARE #" >> $OUTFILE
echo "###########" >> $OUTFILE
run_cmd ${IB_BIN} --apply-log $backup_dir >> $OUTFILE 2>&1
vlog "Restoring MySQL datadir"
mkdir -p $mysql_datadir
echo "###########" >> $OUTFILE
echo "# RESTORE #" >> $OUTFILE
echo "###########" >> $OUTFILE
run_cmd ${IB_BIN} --copy-back $backup_dir >> $OTFILE 2>&1

run_mysqld
# Check sakila
run_cmd ${MYSQL} ${MYSQL_ARGS} -e "SELECT count(*) from actor" sakila
stop_mysqld
clean
