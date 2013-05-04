#include "../fcrypt.h"
#include <string.h>
#include <stdio.h>


int main(int argc, char const *argv[])
{
    fcrypt_ctx fctx;
    unsigned char key[8] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };
    FCRYPT_INIT(&fctx, key, 8);

    unsigned char buf[10] = "serho liu";
    unsigned char buf1[10] = "secho lia";

    printf("old %s\n", buf);

    FCRYPT_ENCRYPT(&fctx, 10, buf);
    FCRYPT_ENCRYPT(&fctx, 10, buf1);
    printf("en %s\n", buf);
    printf("en %s\n", buf1);


    FCRYPT_DECRYPT(&fctx, 10, buf);
    FCRYPT_DECRYPT(&fctx, 10, buf1);
    printf("dn %s\n", buf);
    printf("dn %s\n", buf1);
    return 0;
}