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
    EVP_EncryptInit_ex(e_ctx, EVP_aes_128_cfb128(), NULL, key, iv);
    EVP_CIPHER_CTX_init(d_ctx);
    EVP_DecryptInit_ex(d_ctx, EVP_aes_128_cfb128(), NULL, key, iv);
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
