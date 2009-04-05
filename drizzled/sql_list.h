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

#ifndef DRIZZLED_SQL_LIST_H
#define DRIZZLED_SQL_LIST_H


#include <utility>
#include <algorithm>
#include <stdlib.h>
#include <drizzled/sql_alloc.h>

/** Struct to handle simple linked lists. */
typedef struct st_sql_list {
  uint32_t elements;
  unsigned char *first;
  unsigned char **next;

  st_sql_list() {}                              /* Remove gcc warning */
  inline void empty()
  {
    elements=0;
    first=0;
    next= &first;
  }
  inline void link_in_list(unsigned char *element,unsigned char **next_ptr)
  {
    elements++;
    (*next)=element;
    next= next_ptr;
    *next=0;
  }
  inline void save_and_clear(struct st_sql_list *save)
  {
    *save= *this;
    empty();
  }
  inline void push_front(struct st_sql_list *save)
  {
    *save->next= first;				/* link current list last */
    first= save->first;
    elements+= save->elements;
  }
  inline void push_back(struct st_sql_list *save)
  {
    if (save->first)
    {
      *next= save->first;
      next= save->next;
      elements+= save->elements;
    }
  }
} SQL_LIST;

/*
  Basic single linked list
  Used for item and item_buffs.
  All list ends with a pointer to the 'end_of_list' element, which
  data pointer is a null pointer and the next pointer points to itself.
  This makes it very fast to traverse lists as we don't have to
  test for a specialend condition for list that can't contain a null
  pointer.
*/


/**
  list_node - a node of a single-linked list.
  @note We never call a destructor for instances of this class.
*/

struct list_node :public Sql_alloc
{
  list_node *next;
  void *info;
  list_node(void *info_par,list_node *next_par)
    :next(next_par),info(info_par)
  {}
  list_node()					/* For end_of_list */
  {
    info= 0;
    next= this;
  }
};


extern list_node end_of_list;

class base_list :public Sql_alloc
{
protected:
  list_node *first,**last;

public:
  uint32_t elements;

  inline void empty() { elements=0; first= &end_of_list; last=&first;}
  inline base_list() { empty(); }
  /**
    This is a shallow copy constructor that implicitly passes the ownership
    from the source list to the new instance. The old instance is not
    updated, so both objects end up sharing the same nodes. If one of
    the instances then adds or removes a node, the other becomes out of
    sync ('last' pointer), while still operational. Some old code uses and
    relies on this behaviour. This logic is quite tricky: please do not use
    it in any new code.
  */
  inline base_list(const base_list &tmp) :Sql_alloc()
  {
    elements= tmp.elements;
    first= tmp.first;
    last= elements ? tmp.last : &first;
  }
  /**
    Construct a deep copy of the argument in memory root mem_root.
    The elements themselves are copied by pointer. If you also
    need to copy elements by value, you should employ
    list_copy_and_replace_each_value after creating a copy.
  */
  base_list(const base_list &rhs, MEM_ROOT *mem_root);
  inline base_list(bool) { }
  inline bool push_back(void *info)
  {
    if (((*last)=new list_node(info, &end_of_list)))
    {
      last= &(*last)->next;
      elements++;
      return 0;
    }
    return 1;
  }
  inline bool push_back(void *info, MEM_ROOT *mem_root)
  {
    if (((*last)=new (mem_root) list_node(info, &end_of_list)))
    {
      last= &(*last)->next;
      elements++;
      return 0;
    }
    return 1;
  }
  inline bool push_front(void *info)
  {
    list_node *node=new list_node(info,first);
    if (node)
    {
      if (last == &first)
	last= &node->next;
      first=node;
      elements++;
      return 0;
    }
    return 1;
  }
  void remove(list_node **prev)
  {
    list_node *node=(*prev)->next;
    if (!--elements)
      last= &first;
    else if (last == &(*prev)->next)
      last= prev;
    delete *prev;
    *prev=node;
  }
  inline void concat(base_list *list)
  {
    if (!list->is_empty())
    {
      *last= list->first;
      last= list->last;
      elements+= list->elements;
    }
  }
  inline void *pop(void)
  {
    if (first == &end_of_list) return 0;
    list_node *tmp=first;
    first=first->next;
    if (!--elements)
      last= &first;
    return tmp->info;
  }
  inline void disjoin(base_list *list)
  {
    list_node **prev= &first;
    list_node *node= first;
    list_node *list_first= list->first;
    elements=0;
    while (node && node != list_first)
    {
      prev= &node->next;
      node= node->next;
      elements++;
    }
    *prev= *last;
    last= prev;
  }
  inline void prepand(base_list *list)
  {
    if (!list->is_empty())
    {
      *list->last= first;
      first= list->first;
      elements+= list->elements;
    }
  }
  /**
    Swap two lists.
  */
  inline void swap(base_list &rhs)
  {
    std::swap(first, rhs.first);
    std::swap(last, rhs.last);
    std::swap(elements, rhs.elements);
  }
  inline list_node* last_node() { return *last; }
  inline list_node* first_node() { return first;}
  inline void *head() { return first->info; }
  inline void **head_ref() { return first != &end_of_list ? &first->info : 0; }
  inline bool is_empty() { return first == &end_of_list ; }
  inline list_node *last_ref() { return &end_of_list; }
  friend class base_list_iterator;
  friend class error_list;
  friend class error_list_iterator;

#ifdef LIST_EXTRA_DEBUG
  /*
    Check list invariants and print results into trace. Invariants are:
      - (*last) points to end_of_list
      - There are no NULLs in the list.
      - base_list::elements is the number of elements in the list.

    SYNOPSIS
      check_list()
        name  Name to print to trace file

    RETURN
      1  The list is Ok.
      0  List invariants are not met.
  */

  bool check_list(const char *name)
  {
    base_list *list= this;
    list_node *node= first;
    uint32_t cnt= 0;

    while (node->next != &end_of_list)
    {
      if (!node->info)
      {
        return false;
      }
      node= node->next;
      cnt++;
    }
    if (last != &(node->next))
    {
      return false;
    }
    if (cnt+1 != elements)
    {
      return false;
    }
    return true;
  }
#endif // LIST_EXTRA_DEBUG

protected:
  void after(void *info,list_node *node)
  {
    list_node *new_node=new list_node(info,node->next);
    node->next=new_node;
    elements++;
    if (last == &(node->next))
      last= &new_node->next;
  }
};


class base_list_iterator
{
protected:
  base_list *list;
  list_node **el,**prev,*current;
  void sublist(base_list &ls, uint32_t elm)
  {
    ls.first= *el;
    ls.last= list->last;
    ls.elements= elm;
  }
public:
  base_list_iterator()
    :list(0), el(0), prev(0), current(0)
  {}

  base_list_iterator(base_list &list_par)
  { init(list_par); }

  inline void init(base_list &list_par)
  {
    list= &list_par;
    el= &list_par.first;
    prev= 0;
    current= 0;
  }

  inline void *next(void)
  {
    prev=el;
    current= *el;
    el= &current->next;
    return current->info;
  }
  inline void *next_fast(void)
  {
    list_node *tmp;
    tmp= *el;
    el= &tmp->next;
    return tmp->info;
  }
  inline void rewind(void)
  {
    el= &list->first;
  }
  inline void *replace(void *element)
  {						// Return old element
    void *tmp=current->info;
    assert(current->info != 0);
    current->info=element;
    return tmp;
  }
  void *replace(base_list &new_list)
  {
    void *ret_value=current->info;
    if (!new_list.is_empty())
    {
      *new_list.last=current->next;
      current->info=new_list.first->info;
      current->next=new_list.first->next;
      if ((list->last == &current->next) && (new_list.elements > 1))
	list->last= new_list.last;
      list->elements+=new_list.elements-1;
    }
    return ret_value;				// return old element
  }
  inline void remove(void)			// Remove current
  {
    list->remove(prev);
    el=prev;
    current=0;					// Safeguard
  }
  void after(void *element)			// Insert element after current
  {
    list->after(element,current);
    current=current->next;
    el= &current->next;
  }
  inline void **ref(void)			// Get reference pointer
  {
    return &current->info;
  }
  inline bool is_last(void)
  {
    return el == &list->last_ref()->next;
  }
  friend class error_list_iterator;
};

template <class T> class List :public base_list
{
public:
  inline List() :base_list() {}
  inline List(const List<T> &tmp) :base_list(tmp) {}
  inline List(const List<T> &tmp, MEM_ROOT *mem_root) :
    base_list(tmp, mem_root) {}
  inline bool push_back(T *a) { return base_list::push_back(a); }
  inline bool push_back(T *a, MEM_ROOT *mem_root)
  { return base_list::push_back(a, mem_root); }
  inline bool push_front(T *a) { return base_list::push_front(a); }
  inline T* head() {return (T*) base_list::head(); }
  inline T** head_ref() {return (T**) base_list::head_ref(); }
  inline T* pop()  {return (T*) base_list::pop(); }
  inline void concat(List<T> *list) { base_list::concat(list); }
  inline void disjoin(List<T> *list) { base_list::disjoin(list); }
  inline void prepand(List<T> *list) { base_list::prepand(list); }
  void delete_elements(void)
  {
    list_node *element,*next;
    for (element=first; element != &end_of_list; element=next)
    {
      next=element->next;
      delete (T*) element->info;
    }
    empty();
  }
};


template <class T> class List_iterator :public base_list_iterator
{
public:
  List_iterator(List<T> &a) : base_list_iterator(a) {}
  List_iterator() : base_list_iterator() {}
  inline void init(List<T> &a) { base_list_iterator::init(a); }
  inline T* operator++(int) { return (T*) base_list_iterator::next(); }
  inline T *replace(T *a)   { return (T*) base_list_iterator::replace(a); }
  inline T *replace(List<T> &a) { return (T*) base_list_iterator::replace(a); }
  inline void rewind(void)  { base_list_iterator::rewind(); }
  inline void remove()      { base_list_iterator::remove(); }
  inline void after(T *a)   { base_list_iterator::after(a); }
  inline T** ref(void)	    { return (T**) base_list_iterator::ref(); }
};


template <class T> class List_iterator_fast :public base_list_iterator
{
protected:
  inline T *replace(T *)   { return (T*) 0; }
  inline T *replace(List<T> &) { return (T*) 0; }
  inline void remove(void)  { }
  inline void after(T *a)   { }
  inline T** ref(void)	    { return (T**) 0; }

public:
  inline List_iterator_fast(List<T> &a) : base_list_iterator(a) {}
  inline List_iterator_fast() : base_list_iterator() {}
  inline void init(List<T> &a) { base_list_iterator::init(a); }
  inline T* operator++(int) { return (T*) base_list_iterator::next_fast(); }
  inline void rewind(void)  { base_list_iterator::rewind(); }
  void sublist(List<T> &list_arg, uint32_t el_arg)
  {
    base_list_iterator::sublist(list_arg, el_arg);
  }
};


/*
  A simple intrusive list which automaticly removes element from list
  on delete (for Session element)
*/

struct ilink
{
  struct ilink **prev,*next;
  static void *operator new(size_t size)
  {
    return (void*)malloc((uint32_t)size);
  }
  static void operator delete(void* ptr_arg, size_t)
  {
     free((unsigned char*)ptr_arg);
  }

  inline ilink()
  {
    prev=0; next=0;
  }
  inline void unlink()
  {
    /* Extra tests because element doesn't have to be linked */
    if (prev) *prev= next;
    if (next) next->prev=prev;
    prev=0 ; next=0;
  }
  virtual ~ilink() { unlink(); }		/*lint -e1740 */
};


/* Needed to be able to have an I_List of char* strings in mysqld.cc. */

class i_string: public ilink
{
public:
  const char* ptr;
  i_string():ptr(0) { }
  i_string(const char* s) : ptr(s) {}
};

/* needed for linked list of two strings for replicate-rewrite-db */
class i_string_pair: public ilink
{
public:
  const char* key;
  const char* val;
  i_string_pair():key(0),val(0) { }
  i_string_pair(const char* key_arg, const char* val_arg) :
    key(key_arg),val(val_arg) {}
};


template <class T> class I_List_iterator;

/*
  WARNING: copy constructor of this class does not create a usable
  copy, as its members may point at each other.
*/

class base_ilist
{
public:
  struct ilink *first,last;
  inline void empty() { first= &last; last.prev= &first; }
  base_ilist() { empty(); }
  inline bool is_empty() {  return first == &last; }
  inline void append(ilink *a)
  {
    first->prev= &a->next;
    a->next=first; a->prev= &first; first=a;
  }
  inline void push_back(ilink *a)
  {
    *last.prev= a;
    a->next= &last;
    a->prev= last.prev;
    last.prev= &a->next;
  }
  inline struct ilink *get()
  {
    struct ilink *first_link=first;
    if (first_link == &last)
      return 0;
    first_link->unlink();			// Unlink from list
    return first_link;
  }
  inline struct ilink *head()
  {
    return (first != &last) ? first : 0;
  }
  friend class base_list_iterator;
};


class base_ilist_iterator
{
  base_ilist *list;
  struct ilink **el,*current;
public:
  base_ilist_iterator(base_ilist &list_par) :list(&list_par),
    el(&list_par.first),current(0) {}
  void *next(void)
  {
    /* This is coded to allow push_back() while iterating */
    current= *el;
    if (current == &list->last) return 0;
    el= &current->next;
    return current;
  }
};


template <class T>
class I_List :private base_ilist
{
public:
  I_List() :base_ilist()	{}
  inline void empty()		{ base_ilist::empty(); }
  inline bool is_empty()        { return base_ilist::is_empty(); }
  inline void append(T* a)	{ base_ilist::append(a); }
  inline void push_back(T* a)	{ base_ilist::push_back(a); }
  inline T* get()		{ return (T*) base_ilist::get(); }
  inline T* head()		{ return (T*) base_ilist::head(); }
#ifndef _lint
  friend class I_List_iterator<T>;
#endif
};


template <class T> class I_List_iterator :public base_ilist_iterator
{
public:
  I_List_iterator(I_List<T> &a) : base_ilist_iterator(a) {}
  inline T* operator++(int) { return (T*) base_ilist_iterator::next(); }
};

/**
  Make a deep copy of each list element.

  @note A template function and not a template method of class List
  is employed because of explicit template instantiation:
  in server code there are explicit instantiations of List<T> and
  an explicit instantiation of a template requires that any method
  of the instantiated class used in the template can be resolved.
  Evidently not all template arguments have clone() method with
  the right signature.

  @return You must query the error state in Session for out-of-memory
  situation after calling this function.
*/

template <typename T>
inline
void
list_copy_and_replace_each_value(List<T> &list, MEM_ROOT *mem_root)
{
  /* Make a deep copy of each element */
  List_iterator<T> it(list);
  T *el;
  while ((el= it++))
    it.replace(el->clone(mem_root));
}

/* sql_list.cc */
void free_list(I_List <i_string_pair> *list);
void free_list(I_List <i_string> *list);


#endif // DRIZZLED_SQL_LIST_H