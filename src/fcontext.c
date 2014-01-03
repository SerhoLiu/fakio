#include "fcontext.h"
#include "fevent.h"
#include "flog.h"
//#include "fbuffer.h"
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
    int current_size;
    int used_size;

    context_node_t *head;
};


context_list_t *context_list_create(int maxsize)
{
    if (maxsize < MIN_MAXSIZE) {
        maxsize = MIN_MAXSIZE;
    }
    context_list_t *list = (context_list_t *)malloc(sizeof(*list));
    if (list == NULL) return NULL;

    list->max_size = maxsize;
    list->used_size = list->current_size = 0;
    
    list->head = NULL;
    //list->head.next = list->head.prev = &(list->head);

    return list;
}

void context_list_free(context_list_t *list)
{
    if (list == NULL) return;
    context_node_t *current, *next;
    
    int len = list->current_size;
    current = list->head;
    while (len--) {
        next = current->next;
        free(current);
        current = next;
    }

    free(list);
}

static context *context_create()
{
    context *c = (context *)malloc(sizeof(*c));
    if (c == NULL) return NULL;

    return c;
}

/* 在 context list 头部添加一个节点 */
static context_node_t *context_list_add_node(context_list_t *list)
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
    if (list->current_size == 0) {
        list->head = node;
        node->prev = node->next = NULL;
    } else {
        node->prev = NULL;
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }

    list->current_size++;
    return node;
}

context *context_list_get_empty(context_list_t *list)
{
    if (list == NULL) return NULL;

    LOG_DEBUG("Context size=%d current=%d used=%d",
        list->max_size, list->current_size, list->used_size);

    context_node_t *node;
    
    if (list->used_size < list->current_size) {
        node = list->head;
        int len = list->current_size;
        while (len--) {
            if (node->mask == MASK_NONE) {
                node->mask = (MASK_CLIENT | MASK_REMOTE);
                list->used_size++;
                return node->c;
            }
            node = node->next;
        }
    }

    if (list->used_size == list->current_size) {
        if (list->current_size < list->max_size) {
            node = context_list_add_node(list);
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
        
        if (node != list->head) {
            node->prev->next = node->next;
            if (node->next != NULL) {
                node->next->prev = node->prev;
            }
            node->prev = NULL;
            node->next = list->head;
            list->head->prev = node;
            list->head = node;
        }
        list->used_size--;
    }
}
