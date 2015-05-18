/*
 * Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the Artistic License 2.0 as published by
 * The Perl Foundation.
 *
 * This source is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Artistic License 2.0 for more details.
 *
 * You should have received a copy of the Artistic License 2.0
 * along the source as a COPYING file. If not, obtain it from
 * http://www.perlfoundation.org/artistic_license_2_0.
 */

#ifndef _INLIST_H
#define _INLIST_H

#include <assert.h>

#define INLIST_JUMP_SIZE 256

typedef struct _Inlist Inlist;
typedef struct _Inlist_Sorted_State Inlist_Sorted_State;

typedef int (*Inlist_Compare_Cb)(const void *data1, const void *data2);

struct _Inlist
{
   Inlist *next;
   Inlist *prev;
   Inlist *last;
};

struct _Inlist_Sorted_State
{
   Inlist *jump_table[INLIST_JUMP_SIZE];

   unsigned short jump_limit;
   int jump_div;

   int inserted;
};

#define INLIST Inlist __in_list
#define INLIST_GET(Inlist)         (& ((Inlist)->__in_list))
#define INLIST_CONTAINER_GET(ptr,                          \
                                  type) ((type *)((char *)ptr - \
                                                  offsetof(type, __in_list)))

#define SAFETY_ON_NULL_RETURN_VAL(in, out) \
	if(in == NULL) \
		return out;


static inline Inlist *
inlist_append(Inlist *list, Inlist *new_l)
{
	Inlist *l;
	
	SAFETY_ON_NULL_RETURN_VAL(new_l, list);
	
	new_l->next = NULL;
	if (!list)
	{
		new_l->prev = NULL;
		new_l->last = new_l;
		return new_l;
	}
	
	if (list->last)
		l = list->last;
	else
		for (l = list; (l) && (l->next); l = l->next)
			;
	
	l->next = new_l;
	new_l->prev = l;
	list->last = new_l;

	return list;
}

static inline Inlist *
inlist_append_relative(Inlist *list, Inlist *new_l, Inlist *relative)
{
	SAFETY_ON_NULL_RETURN_VAL(new_l, list);
	
	if ((relative) && (list))
	{
		if (relative->next)
		{
			new_l->next = relative->next;
			relative->next->prev = new_l;
		}
		else
			new_l->next = NULL;
			
		relative->next = new_l;
		new_l->prev = relative;
		if (!new_l->next)
			list->last = new_l;
			
		return list;
	}
	
	return inlist_append(list, new_l);
}

static inline Inlist *
inlist_prepend(Inlist *list, Inlist *new_l)
{
	SAFETY_ON_NULL_RETURN_VAL(new_l, list);
	
	new_l->prev = NULL;
	if (!list)
	{
		new_l->next = NULL;
		new_l->last = new_l;
		return new_l;
	}
	
	new_l->next = list;
	list->prev = new_l;
	new_l->last = list->last;
	list->last = NULL;

	return new_l;
}

static inline Inlist *
inlist_prepend_relative(Inlist *list, Inlist *new_l, Inlist *relative)
{
	SAFETY_ON_NULL_RETURN_VAL(new_l, list);
	
	if ((relative) && (list))
	{
		new_l->prev = relative->prev;
		new_l->next = relative;
		relative->prev = new_l;
		if (new_l->prev)
		{
			new_l->prev->next = new_l;
			/* new_l->next could not be NULL, as it was set to 'relative' */
			assert(new_l->next);
			return list;
		}
		else
		{
			/* new_l->next could not be NULL, as it was set to 'relative' */
			assert(new_l->next);
			
			new_l->last = list->last;
			list->last = NULL;
			return new_l;
		}
	}
	
	return inlist_prepend(list, new_l);
}

static inline Inlist *
inlist_remove(Inlist *list, Inlist *item)
{
	Inlist *return_l;
	
	/* checkme */
	SAFETY_ON_NULL_RETURN_VAL(list, NULL);
	SAFETY_ON_NULL_RETURN_VAL(item, list);
	if ((item != list) && (!item->prev) && (!item->next))
	{
		//FIXME LOG_ERR("safety check failed: item %p does not appear to be part of an inlist!", item);
		return list;
	}
	
	if (item->next)
		item->next->prev = item->prev;
		
	if (item->prev)
	{
		item->prev->next = item->next;
		return_l = list;
		}
	else
	{
		return_l = item->next;
		if (return_l)
			return_l->last = list->last;
	}
	
	if (item == list->last)
		list->last = item->prev;
		
	item->next = NULL;
	item->prev = NULL;
	return return_l;
}

static inline unsigned int
inlist_count(const Inlist *list)
{
	const Inlist *l;
	unsigned int i = 0;
	
	for (l = list; l; l = l->next)
		i++;
		
	return i;
}

static inline void
_inlist_sorted_state_compact(Inlist_Sorted_State *state)
{
	unsigned short i, j;
	
	/* compress the jump table */
	state->jump_div *= 2;
	state->jump_limit /= 2;
	
	for (i = 2, j = 1; i < INLIST_JUMP_SIZE; i += 2, j++)
		state->jump_table[j] = state->jump_table[i];
}

static inline int
inlist_sorted_state_init(Inlist_Sorted_State *state, Inlist *list)
{
	Inlist *ct = NULL;
	int count = 0;
	int jump_count = 1;
	
	/*
	* prepare a jump table to avoid doing unnecessary rewalk
	* of the inlist as much as possible.
	*/
	for (ct = list; ct; ct = ct->next, jump_count++, count++)
	{
		if (jump_count == state->jump_div)
		{
			if (state->jump_limit == INLIST_JUMP_SIZE)
			{
				_inlist_sorted_state_compact(state);
			}
			
			state->jump_table[state->jump_limit] = ct;
			state->jump_limit++;
			jump_count = 0;
		}
	}
	
	state->inserted = count;

	return count;
}

static inline Inlist *
inlist_sorted_insert(Inlist *list, Inlist *item, Inlist_Compare_Cb func)
{
	Inlist *ct = NULL;
	Inlist_Sorted_State state;
	int cmp = 0;
	int inf, sup;
	int cur = 0;
	int count;
	
	SAFETY_ON_NULL_RETURN_VAL(item, list);
	SAFETY_ON_NULL_RETURN_VAL(func, list);
	
	if (!list) return inlist_append(NULL, item);
	
	if (!list->next)
	{
		cmp = func(list, item);
		
		if (cmp < 0)
			return inlist_append(list, item);
		return inlist_prepend(list, item);
	}
	
	state.jump_div = 1;
	state.jump_limit = 0;
	count = inlist_sorted_state_init(&state, list);
	
	/*
	* now do a dychotomic search directly inside the jump_table.
	*/
	inf = 0;
	sup = state.jump_limit - 1;
	cur = 0;
	ct = state.jump_table[cur];
	cmp = func(ct, item);
	
	while (inf <= sup)
	{
		cur = inf + ((sup - inf) >> 1);
		ct = state.jump_table[cur];
		
		cmp = func(ct, item);
		if (cmp == 0)
			break ;
		else if (cmp < 0)
			inf = cur + 1;
		else
		{
			if (cur > 0)
				sup = cur - 1;
			else
				break;
		}
	}
	
	/* If at the beginning of the table and cmp < 0,
	* insert just after the head */
	if (cur == 0 && cmp > 0)
		return inlist_prepend_relative(list, item, ct);
		
	/* If at the end of the table and cmp >= 0,
	* just append the item to the list */
	if (cmp < 0 && ct == list->last)
		return inlist_append(list, item);
	
	/*
	* Now do a dychotomic search between two entries inside the jump_table
	*/
	cur *= state.jump_div;
	inf = cur - state.jump_div - 1;
	sup = cur + state.jump_div + 1;
	
	if (sup > count - 1) sup = count - 1;
	if (inf < 0) inf = 0;
	
	while (inf <= sup)
	{
		int tmp = cur;
		
		cur = inf + ((sup - inf) >> 1);
		if (tmp < cur)
			for (; tmp != cur; tmp++, ct = ct->next);
		else if (tmp > cur)
			for (; tmp != cur; tmp--, ct = ct->prev);
			
		cmp = func(ct, item);
		if (cmp == 0)
			break ;
		else if (cmp < 0)
			inf = cur + 1;
		else
		{
			if (cur > 0)
				sup = cur - 1;
			else
				break;
		}
	}
	
	if (cmp <= 0)
		return inlist_append_relative(list, item, ct);
	return inlist_prepend_relative(list, item, ct);
}

#define _INLIST_OFFSET(ref)         ((char *)&(ref)->__in_list - (char *)(ref))

#if !defined(__cplusplus)
#define _INLIST_CONTAINER(ref, ptr) (void *)((char *)(ptr) - \
                                                  _INLIST_OFFSET(ref))
#else
/*
 * In C++ we can't assign a "type*" pointer to void* so we rely on GCC's typeof
 * operator.
 */
# define _INLIST_CONTAINER(ref, ptr) (__typeof__(ref))((char *)(ptr) - \
							    _INLIST_OFFSET(ref))
#endif

#define INLIST_FOREACH(list, it)                                     \
  for (it = NULL, it = (list ? _INLIST_CONTAINER(it, list) : NULL); it; \
       it = (INLIST_GET(it)->next ? _INLIST_CONTAINER(it, INLIST_GET(it)->next) : NULL))

#define INLIST_FOREACH_SAFE(list, list2, it) \
   for (it = NULL, it = (list ? _INLIST_CONTAINER(it, list) : NULL), list2 = it ? INLIST_GET(it)->next : NULL; \
        it; \
        it = NULL, it = list2 ? _INLIST_CONTAINER(it, list2) : NULL, list2 = list2 ? list2->next : NULL)

#define INLIST_REVERSE_FOREACH(list, it)                                \
  for (it = NULL, it = (list ? _INLIST_CONTAINER(it, list->last) : NULL); \
       it; it = (INLIST_GET(it)->prev ? _INLIST_CONTAINER(it, INLIST_GET(it)->prev) : NULL))

#define INLIST_REVERSE_FOREACH_FROM(list, it)                                \
  for (it = NULL, it = (list ? _INLIST_CONTAINER(it, list) : NULL); \
       it; it = (INLIST_GET(it)->prev ? _INLIST_CONTAINER(it, INLIST_GET(it)->prev) : NULL))

#define INLIST_FREE(list, it)				\
  for (it = (__typeof__(it)) list; list; it = (__typeof__(it)) list)

#endif
