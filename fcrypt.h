#ifndef _FAKIO_CRYPT_H_
#define _FAKIO_CRYPT_H_

#include <string.h>

typedef struct {
    unsigned char en_table[256];
    unsigned char de_table[256];
} fcrypt_ctx;

void fcrypt_init_ctx(fcrypt_ctx *fctx, const unsigned char *key, unsigned int keylen);
void fcrypt_encrypt(fcrypt_ctx *fctx, unsigned char *buf, int len);
void fcrypt_decrypt(fcrypt_ctx *fctx, unsigned char *buf, int len);

#define FAKIO_INIT_CRYPT(a, b, c) fcrypt_init_ctx((a), (b), (c))

#ifdef NCRYPT
    #define FAKIO_ENCRYPT(a, b, c) (void)0
    #define FAKIO_DECRYPT(a, b, c) (void)0
#else    
    #define FAKIO_ENCRYPT(a, b, c) fcrypt_encrypt((a), (b), (c))
    #define FAKIO_DECRYPT(a, b, c) fcrypt_decrypt((a), (b), (c))
#endif

#endif