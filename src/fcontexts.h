#ifndef _FAKIO_CONTEXTS_H_
#define _FAKIO_CONTEXTS_H_

#include "fakio.h"

#define MASK_NONE 0
#define MASK_CLIENT 1
#define MASK_REMOTE 2

struct context {
    int client_fd;
    int remote_fd;
    
    fbuffer_t *req; /* Request buffer */
    fbuffer_t *res; /* Response Buffer */

    struct fserver *server;
    struct event_loop *loop;
    struct context_pool_node *node;
    struct context_pool *pool;

    fuser_t *user;
    fcrypt_ctx_t *crypto;
};

struct context_pool_node {
    int mask;
    context_t *c;
    struct context_pool_node *next;
};

struct context_pool {
    int max_size;
    int inited_size;
    int free_size;

    struct context_pool_node *contexts, *free_context;
};

static inline void context_set_mask(context_t *c, int mask)
{
    c->node->mask = mask;
}

static inline int context_get_mask(context_t *c)
{
    return c->node->mask;
}

context_pool_t *context_pool_create(int maxsize);
void context_pool_destroy(context_pool_t *pool);

context_t *context_pool_get(context_pool_t *pool, int mask);
void context_pool_release(context_pool_t *pool, context_t *c, int mask);

#endif
