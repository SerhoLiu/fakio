#ifndef _FAKIO_CRYPT_H_
#define _FAKIO_CRYPT_H_

#include <string.h>

typedef struct {
    int x, y;
    unsigned char m[256];
} rc4_ctx;

typedef struct {
    rc4_ctx en_ctx;
    rc4_ctx de_ctx;
} fcrypt_ctx;

int rc4_crypt(rc4_ctx *ctx, size_t length, unsigned char *buffer);
void fcrypt_init_ctx(fcrypt_ctx *fctx, const unsigned char *key, unsigned int keylen);

#define FCRYPT_INIT(a, b, c) fcrypt_init_ctx((a), (b), (c))
#define FCRYPT_ENCRYPT(a, b, c) rc4_crypt(&((a)->en_ctx), (b), (c))
#define FCRYPT_DECRYPT(a, b, c) rc4_crypt(&((a)->de_ctx), (b), (c))

#endif