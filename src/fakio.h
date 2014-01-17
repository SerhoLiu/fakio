#ifndef _FAKIO_H_
#define _FAKIO_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "base/hashmap.h"

typedef struct fcrypt_ctx fcrypt_ctx_t;
typedef struct fserver fserver_t;
typedef struct fbuffer fbuffer_t;
typedef struct frequest frequest_t;
typedef struct context_pool context_pool_t;
typedef struct context context_t;
typedef struct fuser fuser_t;

#define BUFSIZE (4096+16)
#define MAX_USERNAME 256

#define MAX_HOST_LEN 253
#define MAX_PORT_LEN 6

#include "futils.h"
#include "fbuffer.h"
#include "fuser.h"
#include "fconfig.h"
#include "fcontext.h"
#include "fcrypt.h"
#include "fnet.h"

struct fserver {
    char host[MAX_HOST_LEN];
    char port[MAX_PORT_LEN];
    context_pool_t *pool;
    hashmap *users;
};

#endif
