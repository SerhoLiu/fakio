#ifndef _FAKIO_CONTEXT_H_
#define _FAKIO_CONTEXT_H_

#include "fbuffer.h"

#define MASK_NONE 0
#define MASK_CLIENT 1
#define MASK_REMOTE 2


typedef struct {
    int client_fd;
    int remote_fd;
    
    fbuffer *req; /* Request buffer */
    fbuffer *res; /* Response Buffer */

    struct event_loop *loop;
    struct context_node *node;
    struct context_list *list;
} context;

typedef struct context_list context_list_t;

context_list_t *context_list_create(int maxsize);

void context_list_free(context_list_t *list);

context *context_list_get_empty(context_list_t *list);

void context_list_remove(context_list_t *list, context *c, int mask);

#endif
