# Copyright (C) 2008-2009 Sun Microsystems, Inc. All rights reserved.
# Use is subject to license terms.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
# USA

package GenTest::Reporter::Shutdown;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Reporter;
use Data::Dumper;
use IPC::Open2;
use IPC::Open3;

sub report {
	my $reporter = shift;

	my $dbh = DBI->connect($reporter->dsn(), undef, undef, {PrintError => 0});
	my $pid = $reporter->serverInfo('pid');

	if (defined $dbh) {
		say("Shutting down the server...");
		$dbh->func('shutdown', 'admin');
	}

	if (!windows()) {
		say("Waiting for mysqld with pid $pid to terminate...");
		foreach my $i (1..60) {
			if (! -e "/proc/$pid") {
				print "\n";
				last;
			}
			sleep(1);
			print "+";
		}
		say("... waiting complete.");
	}
	
	return STATUS_OK;
}

sub type {
	return REPORTER_TYPE_ALWAYS;
}

1;
