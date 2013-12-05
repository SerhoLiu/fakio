#include "fcontext.h"
#include "fevent.h"
#include "flog.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MIN_MAXSIZE 16

struct context_node {
    int mask;
    struct context_node *next;
    context *c;
};

struct context_list {
    int max_size;
    int current_size;
    int used_size;
    context_node *head, *tail;
};

context_list *context_list_create(int maxsize)
{
    if (maxsize < MIN_MAXSIZE) {
        maxsize = MIN_MAXSIZE;
    }
    context_list *list = (context_list *)malloc(sizeof(*list));
    if (list == NULL) return NULL;

    list->max_size = maxsize;
    list->current_size = 0;
    list->used_size = 0;
    list->head = list->tail = NULL;

    return list;
}

void context_list_free(context_list *list)
{
    if (list == NULL) return;
    context_node *n, *p;
    p = list->head;
    while (p != NULL) {
        n = p->next;
        free(p);
        p = n;
    }
    free(list);
}

/* 在 context list 尾部添加一个节点 */
static context_node *context_list_add(context_list *list)
{
    context_node *node;
    node = (context_node *)malloc(sizeof(*node));
    if (node == NULL) return NULL;

    node->c = (context *)malloc(sizeof(context));
    if (node->c == NULL) {
        free(node);
        return NULL;
    }
    node->c->node = node;

    node->next = NULL;
    if (list->head == NULL) {
        list->head = node;
    }
    if (list->tail != NULL) {
        list->tail->next = node;
    }

    list->tail = node;
    list->current_size++;
    return node;
}

context *context_list_get(context_list *list)
{
    if (list == NULL) return NULL;

    LOG_DEBUG("Context size=%d current=%d used=%d",
        list->max_size, list->current_size, list->used_size);

    context_node *node;
    if (list->used_size < list->current_size) {
        for (node = list->head; node != NULL; node = node->next) {
            if (!node->mask) {
                node->mask = (MASK_CLIENT | MASK_REMOTE);
                list->used_size++;
                return node->c;
            }
        }
    }

    if (list->used_size == list->current_size) {
        if (list->current_size < list->max_size) {
            context_node *node = context_list_add(list);
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
void context_list_remove(context_list *list, context *c, int mask)
{
    if (list == NULL || c == NULL || mask == MASK_NONE) return;
    context_node *node = c->node;

    if (node->mask == MASK_NONE) return;
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
        list->used_size--;
    }
}

