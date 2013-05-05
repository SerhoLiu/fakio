#include "../fcrypt.h"
#include <string.h>
#include <stdio.h>

#include <stdlib.h>
#include <sys/time.h>

long long get_ustime_sec(void)
{
    struct timeval tv;
    long long ust;

    gettimeofday(&tv, NULL);
    ust = ((long long)tv.tv_sec)*1000000;
    ust += tv.tv_usec;
    return ust;
}


int main(int argc, char const *argv[])
{
    fcrypt_ctx fctx;
    unsigned char key[8] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };
    long long s, x;
    int i, times;
    times = atoi(argv[1]);
    s = get_ustime_sec();
    for (i = 0; i < times; i++) {
        FAKIO_INIT_CRYPT(&fctx, key, 8);   
    }
    x = get_ustime_sec();
    printf("%d time %lld\n", times, x - s);

    unsigned char buf[10] = "serho liu";
    unsigned char buf1[10] = "secho lia";
    unsigned char buf2[10] = "secoo lia";

    printf("old %s\n", buf);

    FAKIO_ENCRYPT(&fctx, buf, 10);
    printf("en %s\n", buf);
    
    FAKIO_DECRYPT(&fctx, buf, 10);
    printf("dn %s\n", buf);
    FAKIO_ENCRYPT(&fctx, buf1, 10);
    FAKIO_ENCRYPT(&fctx, buf2, 10);
    
    printf("en %s\n", buf1);
    printf("en %s\n", buf2);

    
    FAKIO_DECRYPT(&fctx, buf1, 10);
    FAKIO_DECRYPT(&fctx, buf2, 10);
    
    
    printf("dn %s\n", buf1);
    printf("dn %s\n", buf2);
    return 0;
}