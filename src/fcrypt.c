#include "fcrypt.h"
#include <fcntl.h>


void random_bytes(uint8_t *bytes, size_t len)
{
    int fd, rc, rlen;

    rlen = 0;
    fd = open("/dev/urandom", O_RDONLY);
    if (fd > 0) {
        while (rlen < len) {
            rc = read(fd, bytes+rlen, len-rlen);
            if (rc < 0) {
                break;
            }
            rlen += rc;
        }
        close(fd);    
    }
    
    //if (rlen < len) {
    //    RAND_bytes(buffer, blen);
    //}
}


int fcrypt_set_key(fcrypt_ctx_t *ctx, uint8_t *key, size_t keysize)
{
    if (ctx == NULL || key == NULL) return 0;
    if (keysize != 128 && keysize != 192 && keysize != 256) return 0;
    
    aes_setkey_enc(&ctx->aes, key, keysize);
    //aes_setkey_dec(&ctx->d_ctx, key, keysize);
    return 1;
}


int fcrypt_encrypt_all(fcrypt_ctx_t *ctx,
                       size_t length,
                       uint8_t iv[16],
                       const uint8_t *input,
                       uint8_t *output ) 
{   

    size_t off = 0;
    
    return aes_crypt_cfb128(&ctx->aes, AES_ENCRYPT, length, &off, iv, input, output);

}


int fcrypt_decrypt_all(fcrypt_ctx_t *ctx,
                       size_t length,
                       uint8_t iv[16],
                       const uint8_t *input,
                       uint8_t *output ) 
{
    size_t off = 0;
    
    return aes_crypt_cfb128(&ctx->aes, AES_DECRYPT, length, &off, iv, input, output);
}


int fcrypt_ctx_init(fcrypt_ctx_t *ctx, uint8_t bytes[48], int c)
{   
    if (c) {
        memcpy(ctx->e_iv, bytes, 16);
        memcpy(ctx->d_iv, bytes+16, 16);
    } else {
        memcpy(ctx->d_iv, bytes, 16);
        memcpy(ctx->e_iv, bytes+16, 16);
    }
    
    
    memcpy(ctx->key, bytes+32, 16);

    ctx->e_pos = ctx->d_pos = 0;
    return fcrypt_set_key(ctx, ctx->key, 128);
}


int fcrypt_encrypt(fcrypt_ctx_t *ctx, fbuffer_t *buffer)
{
    printf("enc datalen %d\n", FBUF_DATA_LEN(buffer));

    int i;
    printf("key:\n");
    for (i = 0; i < 16; i++)
        printf("%d ", ctx->key[i]);
    printf("\n");

    printf("iv:\n");
    for (i = 0; i < 16; i++)
        printf("%d ", ctx->e_iv[i]);
    printf("\n");

    uint8_t *data = FBUF_DATA_AT(buffer);
    printf("data:\n");
    for (i = 0; i < 16; i++)
        printf("%d ", data[i]);
    printf("\n");

    aes_crypt_cfb128(&ctx->aes, AES_ENCRYPT, FBUF_DATA_LEN(buffer),
                     &ctx->e_pos, ctx->e_iv,
                     FBUF_DATA_AT(buffer), FBUF_DATA_AT(buffer));

    printf("palin:\n");
    for (i = 0; i < 16; i++)
        printf("%d ", data[i]);
    printf("\n");

    return 1;
}


int fcrypt_decrypt(fcrypt_ctx_t *ctx, fbuffer_t *buffer)
{   
    printf("dec datalen %d\n", FBUF_DATA_LEN(buffer));

    int i;
    printf("key:\n");
    for (i = 0; i < 16; i++)
        printf("%d ", ctx->key[i]);
    printf("\n");

    printf("iv:\n");
    for (i = 0; i < 16; i++)
        printf("%d ", ctx->d_iv[i]);
    printf("\n");

    uint8_t *data = FBUF_DATA_AT(buffer);
    printf("plain:\n");
    for (i = 0; i < 16; i++)
        printf("%d ", data[i]);
    printf("\n");

    aes_crypt_cfb128(&ctx->aes, AES_DECRYPT, FBUF_DATA_LEN(buffer),
                     &ctx->d_pos, ctx->d_iv,
                     FBUF_DATA_AT(buffer), FBUF_DATA_AT(buffer));

    printf("data:\n");
    for (i = 0; i < 16; i++)
        printf("%d ", data[i]);
    printf("\n");
    return 1;
}
