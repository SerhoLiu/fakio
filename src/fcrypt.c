#include "fcrypt.h"
#include <fcntl.h>
#include <sys/time.h>
#include "base/aes.h"

struct fcrypt_rand {
    aes_context aes;

    int budget;
    uint8_t key[16];
    uint8_t time[16];
    uint8_t dst[16];
    uint8_t seed[16];
};


fcrypt_rand_t *fcrypt_rand_new()
{
    fcrypt_rand_t *r = malloc(sizeof(*r));
    if (r == NULL) return NULL;
    
    r->budget = 0;
    return r;
}


static inline int entropy_reader(uint8_t *bytes, size_t len)
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

    return rlen;
}


void random_bytes(fcrypt_rand_t *r, uint8_t *bytes, size_t len)
{
    int rc = entropy_reader(bytes, len);
    if (rc == len) return;

    int mlen = len - rc;
    
    while (mlen > 0) {
        if (r->budget == 0) {
            // 这里就不管是否读完了
            entropy_reader(r->seed, 16);
            entropy_reader(r->key, 16);
            aes_setkey_enc(&r->aes, r->key, 128);
            r->budget = (1 << 20);
        }

        r->budget -= 16;
        
        struct timeval tv;
        gettimeofday(&tv, NULL);
        long long ust = ((long long)tv.tv_sec) * 1000000;
        ust += tv.tv_usec;
        
        int i;
        for (i = 0; i < 8; i++) {
            r->time[i] = ust >> (56 - (i << 3));
        }
        
        aes_crypt_ecb(&r->aes, AES_ENCRYPT, r->time, r->time);
        for (i = 0; i < 16; i++) {
            r->dst[i] = r->time[i] ^ r->seed[i];
        }
        aes_crypt_ecb(&r->aes, AES_ENCRYPT, r->dst, r->dst);
        
        for (i = 0; i < 16; i++) {
            r->seed[i] = r->time[i] ^ r->dst[i];
        }

        aes_crypt_ecb(&r->aes, AES_ENCRYPT, r->seed, r->seed);

        int clen = (mlen <= 16) ? mlen : 16;
        memcpy(bytes+rc, r->dst, clen);
        mlen -= clen;
    }
}
