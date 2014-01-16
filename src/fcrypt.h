#ifndef _FAKIO_CRYPT_H_
#define _FAKIO_CRYPT_H_

#include "fakio.h"
#include "base/aes.h"

struct fcrypt_ctx {
    aes_context aes;

    uint8_t e_iv[16];
    uint8_t d_iv[16];
    uint8_t key[16];

    size_t e_pos, d_pos;
};


void random_bytes(uint8_t *bytes, size_t len);

/*int aes_init(uint8_t *key, uint8_t *iv, fcrypt_ctx *e_ctx, fcrypt_ctx *d_ctx);
int aes_encrypt(fcrypt_ctx *e, uint8_t *plain, int len, uint8_t *cipher);
int aes_decrypt(fcrypt_ctx *e, uint8_t *cipher, int len, uint8_t *plain);
int aes_cleanup(fcrypt_ctx *e_ctx, fcrypt_ctx *d_ctx);*/

int fakio_decrypt(context_t *c, fbuffer_t *buf);
int fakio_encrypt(context_t *c, fbuffer_t *buf);

int fcrypt_set_key(fcrypt_ctx_t *ctx, uint8_t *key, size_t keysize);

int fcrypt_encrypt_all(fcrypt_ctx_t *ctx,
                       size_t length,
                       uint8_t iv[16],
                       const uint8_t *input,
                       uint8_t *output );


int fcrypt_decrypt_all(fcrypt_ctx_t *ctx,
                       size_t length,
                       uint8_t iv[16],
                       const uint8_t *input,
                       uint8_t *output );

int fcrypt_ctx_init(fcrypt_ctx_t *ctx, uint8_t bytes[48], int c);

int fcrypt_encrypt(fcrypt_ctx_t *ctx, fbuffer_t *buffer);

int fcrypt_decrypt(fcrypt_ctx_t *ctx, fbuffer_t *buffer);

#endif
