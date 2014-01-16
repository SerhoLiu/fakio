#ifndef _FAKIO_CONTEXT_H_
#define _FAKIO_CONTEXT_H_

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
    struct context_node *node;
    struct context_pool *pool;

    fuser_t *user;
    fcrypt_ctx_t *crypto;
};


void context_set_mask(context_t *c, int mask);

context_pool_t *context_pool_create(int maxsize);
void context_pool_destroy(context_pool_t *pool);

context_t *context_pool_get(context_pool_t *pool, int mask);
void context_pool_release(context_pool_t *pool, context_t *c, int mask);



#endif
