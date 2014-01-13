#ifndef _FAKIO_CRYPT_H_
#define _FAKIO_CRYPT_H_

#include "fakio.h"

void random_bytes(uint8_t *key, size_t keylen);

int aes_init(uint8_t *key, uint8_t *iv, fcrypt_ctx *e_ctx, fcrypt_ctx *d_ctx);
int aes_encrypt(fcrypt_ctx *e, uint8_t *plain, int len, uint8_t *cipher);
int aes_decrypt(fcrypt_ctx *e, uint8_t *cipher, int len, uint8_t *plain);
int aes_cleanup(fcrypt_ctx *e_ctx, fcrypt_ctx *d_ctx);

int fakio_decrypt(context_t *c, fbuffer_t *buf);
int fakio_encrypt(context_t *c, fbuffer_t *buf);

#endif
