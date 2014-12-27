// TODO: 1. 连接池 2. 多线程

#include "fcontexts.h"
#include <stdlib.h>
#include "base/fevent.h"

#define MIN_MAXSIZE 64

context_pool_t *context_pool_create(int maxsize)
{
    if (maxsize < MIN_MAXSIZE) {
        maxsize = MIN_MAXSIZE;
    }
    context_pool_t *pool = (context_pool_t *)malloc(sizeof(*pool));
    if (pool == NULL) return NULL;

    pool->max_size = pool->free_size = maxsize;
    pool->inited_size = 0;
    
    pool->contexts = malloc(sizeof(struct context_pool_node) * maxsize);
    if (pool->contexts == NULL) {
        free(pool);
        return NULL;
    }

    int i;
    struct context_pool_node *nodes, *next;
    nodes = pool->contexts;
    next = NULL;
    for (i = maxsize - 1; i >= 0; i--) {
        nodes[i].c = NULL;
        nodes[i].mask = MASK_NONE;
        nodes[i].next = next;

        next = &nodes[i];
    }
    pool->free_context = next;

    return pool;
}

static context_t *context_create()
{
    context_t *c = (context_t *)malloc(sizeof(*c));
    if (c == NULL) return NULL;

    c->req = c->res = NULL;
    FBUF_CREATE(c->req);
    if (c->req == NULL) {
        free(c);
        return NULL;
    }
    FBUF_CREATE(c->res);
    if (c->res == NULL) {
        FBUF_FREE(c->req);
        free(c);
        return NULL;
    }
    
    c->crypto = malloc(sizeof(struct fcrypt_ctx));
    if (c->crypto == NULL) {
        FBUF_FREE(c->req);
        FBUF_FREE(c->res);
        free(c);
        return NULL;
    }
    c->user = NULL;
    c->client_fd = c->remote_fd = 0;

    return c;
}


context_t *context_pool_get(context_pool_t *pool, int mask)
{
    if (pool == NULL) return NULL;

    LOG_FOR_DEBUG("Context size=%d inited=%d free=%d",
        pool->max_size, pool->inited_size, pool->free_size);

    struct context_pool_node *node = pool->free_context;
    if (node == NULL) {
        return NULL;
    }
    pool->free_context = node->next;
    pool->free_size--;

    if (node->c != NULL) {
        node->mask = mask;
    } else {
        node->c = context_create();
        if (node->c == NULL) {
            return NULL;
        }
        node->c->node = node;
        node->c->pool = pool;
        node->mask = mask;
        pool->inited_size++;
    }

    return node->c;
}

static inline void delete_and_close_fd(context_t *c, int fd)
{   
    LOG_FOR_DEBUG("delete event context %p fd %d", (void *)c, fd);
    if (fd != 0) {
        delete_event(c->loop, fd, EV_WRABLE);
        delete_event(c->loop, fd, EV_RDABLE);
        close(fd);
    }
}

void context_pool_release(context_pool_t *pool, context_t *c, int mask)
{
    if (pool == NULL || c == NULL || mask == MASK_NONE) return;
    struct context_pool_node *node = c->node;

    int rmask = node->mask & mask;
    
    if (rmask == MASK_CLIENT) {
        delete_and_close_fd(c, c->client_fd);
        c->client_fd = 0;    
    } else if (rmask == MASK_REMOTE) {
        delete_and_close_fd(c, c->remote_fd);
        c->remote_fd = 0;  
    } else {
        delete_and_close_fd(c, c->remote_fd);
        delete_and_close_fd(c, c->client_fd);
        c->client_fd = c->remote_fd = 0;
    }

    node->mask &= (~mask);
    if (node->mask == MASK_NONE) {
        FBUF_REST(node->c->req);
        FBUF_REST(node->c->res);
        node->next = pool->free_context;
        pool->free_context = node;
        pool->free_size++;
    }
}

void context_pool_destroy(context_pool_t *pool)
{
    if (pool == NULL) return;
    free(pool->contexts);
    free(pool);
}
