#ifndef _FAKIO_H_
#define _FAKIO_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <openssl/evp.h>

typedef struct fbuffer fbuffer_t;
typedef struct frequest frequest_t;
typedef EVP_CIPHER_CTX fcrypt_ctx;
typedef struct context_pool context_pool_t;
typedef struct context context_t;
typedef struct fuser fuser_t;

#define BUFSIZE (4096+16)
#define MAX_USERNAME 256

#include "flog.h"
#include "fbuffer.h"
#include "fuser.h"
#include "config.h"
#include "fcontext.h"
#include "fcrypt.h"
#include "fnet.h"

struct fserver {
    config *cfg;
    context_pool_t *pool;
};

typedef struct fserver fserver_t;

#endif
