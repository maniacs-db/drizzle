/* Copyright (C) 2003 MySQL AB

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

/*
  This files defines some MySQL C API functions that are server specific
*/

#include "mysql_priv.h"

/*
  Function called by my_net_init() to set some check variables
*/

extern "C" {
void my_net_local_init(NET *net)
{
  net->max_packet=   (uint) global_system_variables.net_buffer_length;

  my_net_set_read_timeout(net, (uint)global_system_variables.net_read_timeout);
  my_net_set_write_timeout(net,
                           (uint)global_system_variables.net_write_timeout);

  net->retry_count=  (uint) global_system_variables.net_retry_count;
  net->max_packet_size= max(global_system_variables.net_buffer_length,
			    global_system_variables.max_allowed_packet);
}
}
