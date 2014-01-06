#include "../src/fnet.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>


int main(int argc, char const *argv[])
{
    unsigned char buffer[64] = {0x05, 0x53, 0x45, 0x72, 0x48, 0x6F, 0x05, 0x01,
    0xCA, 0x67, 0xBE, 0x1B, 0x21, 0x1C};
    request_t req;

    fakio_request_resolve(buffer, 64, &req);
    printf("%s\n", req.addr);
    printf("%s\n", req.port);
    printf("%s\n", req.username);
    printf("len username %lu\n", strlen(req.username));
    printf("%d\n", req.rlen);
    return 0;
}