/* Copyright (C) 2000-2001, 2005 MySQL AB

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

#include "myisamdef.h"

	/*
	** Find current row with read on position or read on key
	** If inx >= 0 find record using key
	** Return values:
	** 0 = Ok.
	** HA_ERR_KEY_NOT_FOUND = Row is deleted
	** HA_ERR_END_OF_FILE   = End of file
	*/


int mi_rsame(MI_INFO *info, uchar *record, int inx)
{
  if (inx != -1 && ! mi_is_key_active(info->s->state.key_map, inx))
  {
    return(my_errno=HA_ERR_WRONG_INDEX);
  }
  if (info->lastpos == HA_OFFSET_ERROR || info->update & HA_STATE_DELETED)
  {
    return(my_errno=HA_ERR_KEY_NOT_FOUND);	/* No current record */
  }
  info->update&= (HA_STATE_CHANGED | HA_STATE_ROW_CHANGED);

  /* Read row from data file */
  if (fast_mi_readinfo(info))
    return(my_errno);

  if (inx >= 0)
  {
    info->lastinx=inx;
    info->lastkey_length=_mi_make_key(info,(uint) inx,info->lastkey,record,
				      info->lastpos);
    if (info->s->concurrent_insert)
      rw_rdlock(&info->s->key_root_lock[inx]);
    _mi_search(info,info->s->keyinfo+inx,info->lastkey, USE_WHOLE_KEY,
               SEARCH_SAME,
               info->s->state.key_root[inx]);
    if (info->s->concurrent_insert)
      rw_unlock(&info->s->key_root_lock[inx]);
  }

  if (!(*info->read_record)(info,info->lastpos,record))
    return(0);
  if (my_errno == HA_ERR_RECORD_DELETED)
    my_errno=HA_ERR_KEY_NOT_FOUND;
  return(my_errno);
} /* mi_rsame */
