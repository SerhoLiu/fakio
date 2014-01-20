#include "fcontext.h"
#include <stdlib.h>
#include "base/fevent.h"

#define MIN_MAXSIZE 16

struct context_node {
    int mask;
    context_t *c;
    struct context_node *next, *prev;
};

struct context_pool {
    int max_size;
    int current_size;
    int used_size;

    struct context_node *head;
};


context_pool_t *context_pool_create(int maxsize)
{
    if (maxsize < MIN_MAXSIZE) {
        maxsize = MIN_MAXSIZE;
    }
    context_pool_t *pool = (context_pool_t *)malloc(sizeof(*pool));
    if (pool == NULL) return NULL;

    pool->max_size = maxsize;
    pool->used_size = pool->current_size = 0;
    
    pool->head = NULL;

    return pool;
}

void context_pool_destroy(context_pool_t *pool)
{
    if (pool == NULL) return;
    struct context_node *current, *next;
    
    int len = pool->current_size;
    current = pool->head;
    while (len--) {
        next = current->next;
        free(current);
        current = next;
    }

    free(pool);
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
        free(c->req);
        free(c);
        return NULL;
    }
    
    c->crypto = malloc(sizeof(struct fcrypt_ctx));
    if (c->crypto == NULL) {
        free(c->req);
        free(c->res);
        free(c);
        return NULL;
    }
    c->user = NULL;
    c->client_fd = c->remote_fd = 0;

    return c;
}

void context_set_mask(context_t *c, int mask)
{
    c->node->mask = mask;
}

int context_get_mask(context_t *c)
{
    return c->node->mask;
}

/* 在 context list 头部添加一个节点 */
static struct context_node *context_pool_add_node(context_pool_t *pool)
{
    struct context_node *node;
    node = (struct context_node *)malloc(sizeof(*node));
    if (node == NULL) return NULL;

    node->c = context_create();
    if (node->c == NULL) {
        free(node);
        return NULL;
    }
    node->c->node = node;
    node->c->pool = pool;

    // 将 node 插入 used list 头部
    if (pool->current_size == 0) {
        pool->head = node;
        node->prev = node->next = NULL;
    } else {
        node->prev = NULL;
        node->next = pool->head;
        pool->head->prev = node;
        pool->head = node;
    }

    pool->current_size++;
    return node;
}

context_t *context_pool_get(context_pool_t *pool, int mask)
{
    if (pool == NULL) return NULL;

    LOG_FOR_DEBUG("Context size=%d current=%d used=%d",
        pool->max_size, pool->current_size, pool->used_size);

    struct context_node *node;
    
    if (pool->used_size < pool->current_size) {
        node = pool->head;
        int len = pool->current_size;
        while (len--) {
            if (node->mask == MASK_NONE) {
                node->mask = mask;
                pool->used_size++;
                return node->c;
            }
            node = node->next;
        }
    }

    if (pool->used_size == pool->current_size) {
        if (pool->current_size < pool->max_size) {
            node = context_pool_add_node(pool);
            if (node == NULL) return NULL;
            node->mask = mask;
            pool->used_size++;
            return node->c;
        }
    }

    return NULL;
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
    struct context_node *node = c->node;

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

        if (node != pool->head) {
            node->prev->next = node->next;
            if (node->next != NULL) {
                node->next->prev = node->prev;
            }
            node->prev = NULL;
            node->next = pool->head;
            pool->head->prev = node;
            pool->head = node;
        }
        pool->used_size--;
    }
}
