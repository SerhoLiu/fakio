#include "fcrypt.h"
#include <unistd.h>
#include <fcntl.h>
#include <openssl/rand.h>


void random_bytes(uint8_t *buffer, size_t blen)
{
    int fd, rc, len;

    len = 0;
    fd = open("/dev/urandom", O_RDONLY);
    if (fd > 0) {
        while (len < blen) {
            rc = read(fd, buffer+len, blen-len);
            if (rc < 0) {
                break;
            }
            len += rc;
        }
        close(fd);    
    }
    
    if (len < blen) {
        RAND_bytes(buffer, blen);
    }
}


int aes_init(uint8_t *key, uint8_t *iv, EVP_CIPHER_CTX *e_ctx, EVP_CIPHER_CTX *d_ctx)
{
    EVP_CIPHER_CTX_init(e_ctx);
    EVP_EncryptInit_ex(e_ctx, EVP_aes_256_cfb128(), NULL, key, iv);
    EVP_CIPHER_CTX_init(d_ctx);
    EVP_DecryptInit_ex(d_ctx, EVP_aes_256_cfb128(), NULL, key, iv);
    return 0;
}


int aes_encrypt(EVP_CIPHER_CTX *e, uint8_t *plain, int len, uint8_t *cipher)
{
    int c_len, f_len = 0;
    EVP_EncryptInit_ex(e, NULL, NULL, NULL, NULL);
    EVP_EncryptUpdate(e, cipher, &c_len, plain, len);
    EVP_EncryptFinal_ex(e, cipher+c_len, &f_len);
    return c_len + f_len;
}


int aes_decrypt(EVP_CIPHER_CTX *e, uint8_t *cipher, int len, uint8_t *plain)
{
    int p_len = len, f_len = 0;
    EVP_DecryptInit_ex(e, NULL, NULL, NULL, NULL);
    EVP_DecryptUpdate(e, plain, &p_len, cipher, len);
    EVP_DecryptFinal_ex(e, plain+p_len, &f_len);
    return p_len + f_len;
}


int aes_cleanup(EVP_CIPHER_CTX *e_ctx, EVP_CIPHER_CTX *d_ctx)
{
    EVP_CIPHER_CTX_cleanup(e_ctx);
    EVP_CIPHER_CTX_cleanup(d_ctx);
    return 1;
}


int fakio_decrypt(context *c, fbuffer *buf)
{
    uint8_t *buffer = FBUF_DATA_AT(buf);
    EVP_DecryptInit_ex(&c->d_ctx, EVP_aes_128_cfb128(), NULL, c->key, buffer+4096);

    int c_len, f_len = 0;
    uint8_t plain[4096];

    EVP_DecryptInit_ex(&c->d_ctx, NULL, NULL, NULL, NULL);
    EVP_DecryptUpdate(&c->d_ctx, plain, &c_len, buffer, 4096);
    EVP_DecryptFinal_ex(&c->d_ctx, plain+c_len, &f_len);
    
    //TODO: 大小端
    uint16_t datalen = *(uint16_t *)(plain+4094);

    memcpy(FBUF_WRITE_AT(buf), plain, datalen);
    FBUF_DATA_LEN(buf) = datalen;
    LOG_DEBUG("recv from client: %d", FBUF_DATA_LEN(buf));
    
    return 1;
}


int fakio_encrypt(context *c, fbuffer *buf)
{   
    uint8_t plain[4096];
    
    LOG_DEBUG("recv from remote: %d", FBUF_DATA_LEN(buf));
    memcpy(plain, FBUF_DATA_AT(buf), FBUF_DATA_LEN(buf));
    *(uint16_t *)(plain+4094) = FBUF_DATA_LEN(buf);

    random_bytes(FBUF_WRITE_SEEK(buf, 4096), 16);

    EVP_EncryptInit_ex(&c->e_ctx, EVP_aes_128_cfb128(), NULL,
                       c->key, FBUF_DATA_SEEK(buf, 4096));

    int c_len, f_len = 0;
    EVP_EncryptInit_ex(&c->e_ctx, NULL, NULL, NULL, NULL);

    EVP_EncryptUpdate(&c->e_ctx, FBUF_WRITE_AT(buf), &c_len, plain, 4096);
    EVP_EncryptFinal_ex(&c->e_ctx, FBUF_WRITE_AT(buf)+c_len, &f_len);
    FBUF_DATA_LEN(buf) = BUFSIZE;

    return 1;
}
