#ifndef _FAKIO_CRYPT_H_
#define _FAKIO_CRYPT_H_

#include "fakio.h"
#include <openssl/evp.h>

void random_key(uint8_t *key, size_t keylen);
int aes_init(uint8_t *key, uint8_t *iv, EVP_CIPHER_CTX *e_ctx, EVP_CIPHER_CTX *d_ctx);
int aes_encrypt(EVP_CIPHER_CTX *e, uint8_t *plain, int len, uint8_t *cipher);
int aes_decrypt(EVP_CIPHER_CTX *e, uint8_t *cipher, int len, uint8_t *plain);
int aes_cleanup(EVP_CIPHER_CTX *e_ctx, EVP_CIPHER_CTX *d_ctx);

#endif
