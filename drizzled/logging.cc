/*
 -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:

 *  Copyright (C) 2008 Mark Atwood
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

#include <drizzled/server_includes.h>
#include <drizzled/logging.h>

int logging_initializer(st_plugin_int *plugin)
{
  logging_t *p;

  fprintf(stderr, "MRA %s plugin:%s dl:%s\n",
	  __func__, plugin->name.str, plugin->plugin_dl->dl.str);

  p= (logging_t *) malloc(sizeof(logging_t));
  if (p == NULL) return 1;
  memset(p, 0, sizeof(logging_t));

  plugin->data= (void *)p;

  if (plugin->plugin->init)
  {
    if (plugin->plugin->init((void *)p))
    {
      sql_print_error("Logging plugin '%s' init function returned error.",
                      plugin->name.str);
      goto err;
    }
  }
  return 0;

err:
  free(p);
  return 1;
}

int logging_finalizer(st_plugin_int *plugin)
{ 
  logging_t *p = (logging_t *) plugin->data;

  fprintf(stderr, "MRA %s plugin:%s dl:%s\n",
	  __func__, plugin->name.str, plugin->plugin_dl->dl.str);

  if (plugin->plugin->deinit)
  {
    if (plugin->plugin->deinit((void *)p))
    {
      sql_print_error("Logging plugin '%s' deinit function returned error.",
                      plugin->name.str);
    }
  }

  if (p) free(p);

  return 0;
}

static bool logging_pre_iterate (THD *thd, plugin_ref plugin,
				 void *stuff __attribute__ ((__unused__)))
{
  logging_t *l= plugin_data(plugin, logging_t *);

  if (l && l->logging_pre)
  {
    if (l->logging_pre(thd))
      return true;
  }
  return false;
}

void logging_pre_do (THD *thd)
{
  if (plugin_foreach(thd, logging_pre_iterate, DRIZZLE_LOGGER_PLUGIN, NULL))
  {
    sql_print_error("Logging plugin pre had an error.");
  }
  return;
}

static bool logging_post_iterate (THD *thd, plugin_ref plugin, 
				  void *stuff __attribute__ ((__unused__)))
{
  logging_t *l= plugin_data(plugin, logging_t *);

  if (l && l->logging_post)
  {
    if (l->logging_post(thd))
      return true;
  }
  return false;
}

void logging_post_do (THD *thd)
{
  if (plugin_foreach(thd, logging_post_iterate, DRIZZLE_LOGGER_PLUGIN, NULL))
  {
    sql_print_error("Logging plugin post had an error.");
  }
  return;
}
