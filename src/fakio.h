#ifndef _FAKIO_H_
#define _FAKIO_H_

#include "fcommon.h"
#include <openssl/evp.h>
#include "fuser.h"
#include "fcontext.h"

typedef struct {
    uint8_t hand[1024];
    int start;
    int length;

    context *c;
    user_t *user;
    
    EVP_CIPHER_CTX *e_ctx;
    EVP_CIPHER_CTX *d_ctx;
} fakio_context_t;

#endif
