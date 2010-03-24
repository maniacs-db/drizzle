/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2010 Monty Taylor
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

#ifndef DRIZZLED_PLUGIN_CONTEXT_H
#define DRIZZLED_PLUGIN_CONTEXT_H

/**
 * @file Defines a Plugin Context
 *
 * A plugin::Context object is a proxy object containing state information
 * about the plugin being registered that knows how to perform registration
 * actions.
 *
 * The plugin registration system creates a new plugin::Context for each
 * plugin::Module during the initializtion phase and passes a reference to
 * the plugin::Context to the module's init method. This allows the plugin
 * to call registration methods without having access to larger plugin::Registry
 * calls. It also provides a filter layer through which calls are made in order
 * to force things like proper name prefixing and the like.
 */

#include "drizzled/plugin/registry.h"

namespace drizzled
{

class sys_var;

namespace plugin
{
class Module;

class Context
{
private:
  Registry &registry;
  Module *module;

  Context(const Context&);
  Context& operator=(const Context&);
public:

  Context(Registry &registry_arg,
          Module *module_arg) :
     registry(registry_arg),
     module(module_arg)
  { }

  template<class T>
  void add(T *plugin)
  {
    plugin->setModule(module);
    registry.add(plugin);
  }

  template<class T>
  void remove(T *plugin)
  {
    registry.remove(plugin);
  }

  void registerVariable(sys_var *var);
};

inline void Context::registerVariable(sys_var *)
{
/* In here, you can do:
  sys_var->append_name_prefix(module->getName());
  register_variable_whatever();
*/
}

} /* end namespace plugin */
} /* end namespace drizzled */

#endif /* DRIZZLED_PLUGIN_CONTEXT_H */