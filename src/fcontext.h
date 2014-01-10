#ifndef _FAKIO_CONTEXT_H_
#define _FAKIO_CONTEXT_H_

#include <openssl/evp.h>
#include "fbuffer.h"
#include "fuser.h"

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

    fuser_t *user;
    EVP_CIPHER_CTX *e_ctx;
    EVP_CIPHER_CTX *d_ctx;
} context;

typedef struct context_list context_list_t;

context_list_t *context_list_create(int maxsize);

void context_list_free(context_list_t *list);

context *context_list_get_empty(context_list_t *list);

void context_list_remove(context_list_t *list, context *c, int mask);

#endif
