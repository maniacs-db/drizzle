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

#ifndef DRIZZLED_ITEM_COPY_STRING_H
#define DRIZZLED_ITEM_COPY_STRING_H

#include <drizzled/item/ident.h>
#include <drizzled/item/field.h>

class Item_copy_string :public Item
{
  enum enum_field_types cached_field_type;
public:
  Item *item;
  Item_copy_string(Item *i) :item(i)
  {
    null_value= maybe_null= item->maybe_null;
    decimals=item->decimals;
    max_length=item->max_length;
    name=item->name;
    cached_field_type= item->field_type();
  }
  enum Type type() const { return COPY_STR_ITEM; }
  enum Item_result result_type () const { return STRING_RESULT; }
  enum_field_types field_type() const { return cached_field_type; }
  double val_real()
  {
    int err_not_used;
    char *end_not_used;
    return (null_value ? 0.0 :
            my_strntod(str_value.charset(), (char*) str_value.ptr(),
                       str_value.length(), &end_not_used, &err_not_used));
  }
  int64_t val_int()
  {
    int err;
    return null_value ? 0 : my_strntoll(str_value.charset(),str_value.ptr(),
                                        str_value.length(),10, (char**) 0,
                                        &err);
  }
  String *val_str(String*);
  my_decimal *val_decimal(my_decimal *);
  void make_field(Send_field *field) { item->make_field(field); }
  void copy();
  int save_in_field(Field *field, bool)
  {
    return save_str_value_in_field(field, &str_value);
  }
  table_map used_tables() const { return (table_map) 1L; }
  bool const_item() const { return 0; }
  bool is_null() { return null_value; }
};


class Item_default_value : public Item_field
{
public:
  Item *arg;
  Item_default_value(Name_resolution_context *context_arg)
    :Item_field(context_arg, (const char *)NULL, (const char *)NULL,
               (const char *)NULL),
     arg(NULL) {}
  Item_default_value(Name_resolution_context *context_arg, Item *a)
    :Item_field(context_arg, (const char *)NULL, (const char *)NULL,
                (const char *)NULL),
     arg(a) {}
  enum Type type() const { return DEFAULT_VALUE_ITEM; }
  bool eq(const Item *item, bool binary_cmp) const;
  bool fix_fields(Session *, Item **);
  virtual void print(String *str, enum_query_type query_type);
  int save_in_field(Field *field_arg, bool no_conversions);
  table_map used_tables() const { return (table_map)0L; }

  bool walk(Item_processor processor, bool walk_subquery, unsigned char *args)
  {
    return arg->walk(processor, walk_subquery, args) ||
      (this->*processor)(args);
  }

  Item *transform(Item_transformer transformer, unsigned char *args);
};

#endif /* DRIZZLED_ITEM_COPY_STRING_H */