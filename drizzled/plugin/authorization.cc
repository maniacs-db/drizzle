/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2010 Monty Taylor
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

#include <vector>

#include "drizzled/plugin/authorization.h"
#include "drizzled/security_context.h"
#include "drizzled/error.h"
#include "drizzled/session.h"
#include "drizzled/plugin/registry.h"
#include "drizzled/gettext.h"

using namespace std;

namespace drizzled
{

vector<plugin::Authorization *> authorization_plugins;


bool plugin::Authorization::addPlugin(plugin::Authorization *auth)
{
  if (auth != NULL)
    authorization_plugins.push_back(auth);
  return false;
}

void plugin::Authorization::removePlugin(plugin::Authorization *auth)
{
  if (auth != NULL)
  {
    authorization_plugins.erase(find(authorization_plugins.begin(),
                                     authorization_plugins.end(),
                                     auth));
  }
}

namespace
{

class RestrictDbFunctor :
  public unary_function<plugin::Authorization *, bool>
{
  const SecurityContext &user_ctx;
  const string &schema;
public:
  RestrictDbFunctor(const SecurityContext &user_ctx_arg,
                    const string &schema_arg) :
    unary_function<plugin::Authorization *, bool>(),
    user_ctx(user_ctx_arg),
    schema(schema_arg)
  { }

  inline result_type operator()(argument_type auth)
  {
    return auth->restrictSchema(user_ctx, schema);
  }
};

class RestrictTableFunctor :
  public unary_function<plugin::Authorization *, bool>
{
  const SecurityContext &user_ctx;
  const string &schema;
  const string &table;
public:
  RestrictTableFunctor(const SecurityContext &user_ctx_arg,
                       const string &schema_arg,
                       const string &table_arg) :
    unary_function<plugin::Authorization *, bool>(),
    user_ctx(user_ctx_arg),
    schema(schema_arg),
    table(table_arg)
  { }

  inline result_type operator()(argument_type auth)
  {
    return auth->restrictTable(user_ctx, schema, table);
  }
};

class RestrictProcessFunctor :
  public unary_function<plugin::Authorization *, bool>
{
  const SecurityContext &user_ctx;
  const SecurityContext &session_ctx;
public:
  RestrictProcessFunctor(const SecurityContext &user_ctx_arg,
                         const SecurityContext &session_ctx_arg) :
    unary_function<plugin::Authorization *, bool>(),
    user_ctx(user_ctx_arg),
    session_ctx(session_ctx_arg)
  { }

  inline result_type operator()(argument_type auth)
  {
    return auth->restrictProcess(user_ctx, session_ctx);
  }
};

} /* namespace */

bool plugin::Authorization::isAuthorized(const SecurityContext &user_ctx,
                                         const string &schema,
                                         bool send_error)
{
  /* If we never loaded any authorization plugins, just return true */
  if (authorization_plugins.empty())
    return true;

  /* Use find_if instead of foreach so that we can collect return codes */
  vector<plugin::Authorization *>::const_iterator iter=
    find_if(authorization_plugins.begin(),
            authorization_plugins.end(),
            RestrictDbFunctor(user_ctx, schema));

  /*
   * If iter is == end() here, that means that all of the plugins returned
   * false, which means that that each of them believe the user is authorized
   * to view the resource in question.
   */
  if (iter != authorization_plugins.end())
  {
    if (send_error)
    {
      my_error(ER_DBACCESS_DENIED_ERROR, MYF(0),
               user_ctx.getUser().c_str(),
               user_ctx.getIp().c_str(),
               schema.c_str());
    }
    return false;
  }
  return true;
}

bool plugin::Authorization::isAuthorized(const SecurityContext &user_ctx,
                                         const string &schema,
                                         const string &table,
                                         bool send_error)
{
  /* If we never loaded any authorization plugins, just return true */
  if (authorization_plugins.empty())
    return true;

  /* Use find_if instead of foreach so that we can collect return codes */
  vector<plugin::Authorization *>::const_iterator iter=
    find_if(authorization_plugins.begin(),
            authorization_plugins.end(),
            RestrictTableFunctor(user_ctx, schema, table));

  /*
   * If iter is == end() here, that means that all of the plugins returned
   * false, which means that that each of them believe the user is authorized
   * to view the resource in question.
   */
  if (iter != authorization_plugins.end())
  {
    if (send_error)
    {
      my_error(ER_DBACCESS_DENIED_ERROR, MYF(0),
               user_ctx.getUser().c_str(),
               user_ctx.getIp().c_str(),
               schema.c_str());
    }
    return false;
  }
  return true;
}

bool plugin::Authorization::isAuthorized(const SecurityContext &user_ctx,
                                         const Session *session,
                                         bool send_error)
{
  const SecurityContext &session_ctx= session->getSecurityContext();

  /* If we never loaded any authorization plugins, just return true */
  if (authorization_plugins.empty())
    return true;

  /* Use find_if instead of foreach so that we can collect return codes */
  vector<plugin::Authorization *>::const_iterator iter=
    find_if(authorization_plugins.begin(),
            authorization_plugins.end(),
            RestrictProcessFunctor(user_ctx, session_ctx));

  /*
   * If iter is == end() here, that means that all of the plugins returned
   * false, which means that that each of them believe the user is authorized
   * to view the resource in question.
   */

  if (iter != authorization_plugins.end())
  {
    if (send_error)
    {
      my_error(ER_KILL_DENIED_ERROR, MYF(0), session->thread_id);
    }
    return false;
  }
  return true;
}

void plugin::Authorization::pruneSchemaNames(const SecurityContext &user_ctx,
                                             set<string> &set_of_names)
{

  set<string> pruned_set_of_names;

  /* If we never loaded any authorization plugins, just return true */
  if (authorization_plugins.empty())
    return;

  /**
   * @TODO: It would be stellar if we could find a way to do this with a
   * functor and an STL algoritm
   */
  for(set<string>::const_iterator iter= set_of_names.begin();
      iter != set_of_names.end();
      ++iter)
  {
    if (plugin::Authorization::isAuthorized(user_ctx, *iter, false))
    {
      pruned_set_of_names.insert(*iter);
    }
  }
  set_of_names.swap(pruned_set_of_names);
}

} /* namespace drizzled */