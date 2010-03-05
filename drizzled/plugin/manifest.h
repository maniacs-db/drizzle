/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2009 Sun Microsystems
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

#ifndef DRIZZLED_PLUGIN_MANIFEST_H
#define DRIZZLED_PLUGIN_MANIFEST_H

#include "drizzled/plugin/context.h"

namespace drizzled
{

struct drizzle_show_var;
struct drizzle_sys_var;

/* We use the following strings to define licenses for plugins */
enum plugin_license_type {
  PLUGIN_LICENSE_GPL,
  PLUGIN_LICENSE_BSD,
  PLUGIN_LICENSE_LGPL,
  PLUGIN_LICENSE_PROPRIETARY,
  PLUGIN_LICENSE_MAX=PLUGIN_LICENSE_LGPL
};


namespace plugin
{

typedef int (*initialize_func_t)(Context &);

/**
 * Plugin Manfiest
 *
 * One Manifest is required per plugin library which is to be dlopened
 *
 * This is a struct and not a class because it is staticly defined in the
 * plugin objects and needs to be a POD as it can, or else it won't compile.
 */
struct Manifest
{
  uint64_t drizzle_version;  /* Drizzle version the plugin was compiled for  */
  const char *name;          /* plugin name (for SHOW PLUGINS)               */
  const char *version;       /* plugin version (for SHOW PLUGINS)            */
  const char *author;        /* plugin author (for SHOW PLUGINS)             */
  const char *descr;         /* general descriptive text (for SHOW PLUGINS ) */
  plugin_license_type license; /* plugin license (PLUGIN_LICENSE_XXX)          */
  initialize_func_t init;     /* function to invoke when plugin is loaded     */
  initialize_func_t deinit;   /* function to invoke when plugin is unloaded   */
  drizzle_sys_var **system_vars;
  void *reserved1;           /* reserved for dependency checking             */
};

} /* namespace plugin */
} /* namespace drizzled */

#endif /* DRIZZLED_PLUGIN_MANIFEST_H */
