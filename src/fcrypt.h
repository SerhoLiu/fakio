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


struct fcrypt_rand {
    aes_context aes;

    int budget;
    uint8_t key[16];
    uint8_t time[16];
    uint8_t dst[16];
    uint8_t seed[16];
};


static inline void fcrypt_rand_init(fcrypt_rand_t *rand)
{
    rand->budget = 0;
}


void random_bytes(fcrypt_rand_t *rand, uint8_t *bytes, size_t len);


static inline int fcrypt_set_key(fcrypt_ctx_t *ctx, uint8_t *key, size_t keysize)
{
    if (ctx == NULL || key == NULL) return 0;
    if (keysize != 128 && keysize != 192 && keysize != 256) return 0;
    
    aes_setkey_enc(&ctx->aes, key, keysize);
    return 1;
}


static inline void fcrypt_encrypt_all(fcrypt_ctx_t *ctx, uint8_t iv[16],
                    size_t length, const uint8_t *input, uint8_t *output) 
{   
    size_t off = 0;
    aes_crypt_cfb128(&ctx->aes, AES_ENCRYPT, length, &off, iv, input, output);
}


static inline void fcrypt_decrypt_all(fcrypt_ctx_t *ctx, uint8_t iv[16],
                    size_t length, const uint8_t *input, uint8_t *output)
{
    size_t off = 0;
    aes_crypt_cfb128(&ctx->aes, AES_DECRYPT, length, &off, iv, input, output);
}


static inline int fcrypt_ctx_init(fcrypt_ctx_t *ctx, uint8_t bytes[48])
{   
    memcpy(ctx->d_iv, bytes, 16);
    memcpy(ctx->e_iv, bytes+16, 16);
    memcpy(ctx->key, bytes+32, 16);

    ctx->e_pos = ctx->d_pos = 0;
    return fcrypt_set_key(ctx, ctx->key, 128);
}


static inline int fcrypt_encrypt(fcrypt_ctx_t *ctx, fbuffer_t *buffer)
{

    return aes_crypt_cfb128(&ctx->aes, AES_ENCRYPT, FBUF_DATA_LEN(buffer),
                     &ctx->e_pos, ctx->e_iv,
                     FBUF_DATA_AT(buffer), FBUF_DATA_AT(buffer));
}


static inline int fcrypt_decrypt(fcrypt_ctx_t *ctx, fbuffer_t *buffer)
{   

    return aes_crypt_cfb128(&ctx->aes, AES_DECRYPT, FBUF_DATA_LEN(buffer),
                     &ctx->d_pos, ctx->d_iv,
                     FBUF_DATA_AT(buffer), FBUF_DATA_AT(buffer));
}

#endif
