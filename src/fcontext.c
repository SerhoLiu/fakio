#include "fcontext.h"
#include "fevent.h"
#include "flog.h"
#include "fbuffer.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MIN_MAXSIZE 16

struct context_node {
    int mask;
    context *c;

    struct context_node *next, *prev;
};

typedef struct context_node context_node_t;

struct context_list {
    int max_size;
    int empty_size;
    int used_size;

    context_node_t *empty_head, *empty_tail;
    context_node_t *used_head;
};


context_list_t *context_list_create(int maxsize)
{
    if (maxsize < MIN_MAXSIZE) {
        maxsize = MIN_MAXSIZE;
    }
    context_list_t *list = (context_list_t *)malloc(sizeof(*list));
    if (list == NULL) return NULL;

    list->max_size = maxsize;
    list->empty_size = 0;
    list->used_size = 0;
    
    list->empty_head = list->used_head = list->empty_tail = NULL;

    return list;
}

void context_list_free(context_list_t *list)
{
    if (list == NULL) return;
    context_node_t *n, *p;
    
    p = list->used_head;
    while (p != NULL) {
        n = p->next;
        free(p);
        p = n;
    }

    p = list->empty_head;
    while (p != NULL) {
        n = p->next;
        free(p);
        p = n;
    }

    free(list);
}

static context *context_create()
{
    context *c = (context *)malloc(sizeof(*c));
    if (c == NULL) return NULL;
    
    FBUF_CREATE(c->request);
    if (c->request == NULL) {
        free(c);
        return NULL;
    }

    FBUF_CREATE(c->response);
    if (c->response == NULL) {
        free(c->request);
        free(c);
        return NULL;
    }

    return c;
}

/* 在 context list 头部添加一个节点 */
static context_node_t *context_list_adduse(context_list_t *list)
{
    context_node_t *node;
    node = (context_node_t *)malloc(sizeof(*node));
    if (node == NULL) return NULL;

    node->c = context_create();
    if (node->c == NULL) {
        free(node);
        return NULL;
    }
    node->c->node = node;

    // 将 node 插入 used list 头部
    if (list->used_head == NULL) {
        node->next = node->prev = NULL;
    } else {
        node->next = list->used_head;
        node->prev = NULL;
        list->used_head->prev = node;
    }

    list->used_head = node;
    list->used_size++;
    return node;
}

context *context_list_get_empty(context_list_t *list)
{
    if (list == NULL) return NULL;

    LOG_DEBUG("Context size=%d empty=%d used=%d",
        list->max_size, list->empty_size, list->used_size);

    context_node_t *node;
    if (list->empty_size > 0) {
        // 从空闲 list 中取
        node = list->empty_head;
        list->empty_head = node->next;
        list->empty_size--;

        // 加入已使用 list 头部
        list->used_head->prev = node;
        node->next = list->used_head;
        list->used_head = node;
        node->prev = NULL;
        list->used_size++;

        return node->c;
    }

    if (list->empty_size == 0) {
        if (list->used_size < list->max_size) {
            context_node_t *node = context_list_adduse(list);
            if (node == NULL) return NULL;
            node->mask = (MASK_CLIENT | MASK_REMOTE);
            list->used_size++;
            return node->c;
        }
    }

    return NULL;
}


static void delete_and_close_fd(context *c, int fd)
{   
    LOG_DEBUG("delete event context %p fd %d", c, fd);
    if (fd != 0) {
        delete_event(c->loop, fd, EV_WRABLE);
        delete_event(c->loop, fd, EV_RDABLE);
        close(fd);
    }
}

/* 考虑将已经完全清空的节点放到头节点，可加快分配速度 */
void context_list_remove(context_list_t *list, context *c, int mask)
{
    if (list == NULL || c == NULL || mask == MASK_NONE) return;
    context_node_t *node = c->node;

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
        // 将其从已使用 list 中移出
        // 如果 node 是已使用 list 的头节点
        if (node->prev == NULL) {
            list->used_head = node->next;
            node->next->prev = NULL;
        } else {
            node->prev->next = node->next;
            node->next->prev = node->prev;
        }
        list->used_size--;

        // 加入到空闲 list 头
        node->next = list->empty_head;
        list->empty_head = node;
        if (list->empty_tail == NULL) {
            list->empty_tail = node;
        }
        list->empty_size++;
    }
}
