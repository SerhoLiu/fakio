/*
 * Copyright (c) 2006 Maxim Yegorushkin <maxim.yegorushkin@gmail.com>
 * All rights reserved.
 */
#ifndef _MIN_HEAP_H_
#define _MIN_HEAP_H_

#include <stdlib.h>
#include "fevent.h"

typedef struct min_heap {
    time_event** p;
    long n, a;
} min_heap_t;


static inline int _reserve(min_heap_t *s, long n);
static inline void _shift_up(min_heap_t *s, long hole_index, time_event *e);
static inline void _shift_down(min_heap_t *s, long hole_index, time_event *e);

static inline int min_heap_elem_greater(time_event *a, time_event *b)
{
    if (a->when_sec != b->when_sec) return (a->when_sec - b->when_sec);
    return (a->when_usec - b->when_usec);
}

static inline void min_heap_ctor(min_heap_t *s)
{ 
    s->p = NULL; s->n = 0; s->a = 0; 
}

static inline void min_heap_dtor(min_heap_t *s) 
{
    free(s->p);
}

static inline void min_heap_elem_init(time_event *e)
{ 
    e->min_heap_idx = -1; 
}

static inline unsigned min_heap_size(min_heap_t *s)
{ 
    return s->n; 
}

static inline time_event *min_heap_top(min_heap_t *s)
{ 
    return s->n ? *s->p : NULL; 
}

static inline int min_heap_push(min_heap_t *s, time_event *e)
{
    if (_reserve(s, s->n + 1))
        return -1;
    _shift_up(s, s->n++, e);
    return 0;
}

static inline time_event *min_heap_pop(min_heap_t *s)
{
    if (s->n) {
        time_event* e = *s->p;
        _shift_down(s, 0, s->p[--s->n]);
        e->min_heap_idx = -1;
        return e;
    }
    return NULL;
}

static inline int min_heap_delete(min_heap_t *s, time_event *e)
{
    if (e->min_heap_idx != -1) {
        time_event *last = s->p[--s->n];
        long parent = (e->min_heap_idx - 1) / 2;
        /* 
         * we replace e with the last element in the heap.  We might need to
         * shift it upward if it is less than its parent, or downward if it is
         * greater than one or both its children. Since the children are known
         * to be less than the parent, it can't need to shift both up and
         * down. 
         */
        if (e->min_heap_idx > 0 && min_heap_elem_greater(s->p[parent], last))
             _shift_up(s, e->min_heap_idx, last);
        else
             _shift_down(s, e->min_heap_idx, last);
        e->min_heap_idx = -1;
        return 0;
    }
    return -1;
}

static inline int _reserve(min_heap_t *s, long n)
{
    if (s->a < n) {
        time_event** p;
        long a = s->a ? s->a * 2 : 8;
        if(a < n) a = n;
        if(!(p = (time_event**)realloc(s->p, a * sizeof *p))) {
            return -1;
        }
        s->p = p;
        s->a = a;
    }
    return 0;
}


static inline void _shift_up(min_heap_t *s, long hole_index, time_event *e)
{
    long parent = (hole_index - 1) / 2;
    while (hole_index && min_heap_elem_greater(s->p[parent], e)) {
        (s->p[hole_index] = s->p[parent])->min_heap_idx = hole_index;
        hole_index = parent;
        parent = (hole_index - 1) / 2;
    }
    (s->p[hole_index] = e)->min_heap_idx = hole_index;
}

static inline void _shift_down(min_heap_t *s, long hole_index, time_event *e)
{
    long min_child = 2 * (hole_index + 1);
    while (min_child <= s->n) {

        /* 最小孩子结点索引 */
        min_child -= (min_child == s->n || 
            min_heap_elem_greater(s->p[min_child], s->p[min_child - 1]));

        if (!(min_heap_elem_greater(e, s->p[min_child]))) break;
        (s->p[hole_index] = s->p[min_child])->min_heap_idx = hole_index;
        hole_index = min_child;
        min_child = 2 * (hole_index + 1);
    }
    _shift_up(s, hole_index,  e);
}

#endif /* _MIN_HEAP_H_ */
