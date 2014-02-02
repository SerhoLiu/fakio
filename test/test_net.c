#include "../src/fnet.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>


int main(int argc, char const *argv[])
{
    unsigned char buffer[64] = {0x44, 0x41, 0x6A, 0xC2, 0xD1, 0xF5, 0x3C, 0x58,
            0x33, 0x03, 0x91, 0x7E, 0x6B, 0xE9, 0xEB, 0xE0,
            0x05, 0x53, 0x45, 0x72, 0x48, 0x6F, 0x05, 0x01,
            0xCA, 0x67, 0xBE, 0x1B, 0x21, 0x1C};
    frequest_t req;

    fakio_request_resolve(buffer, 64, &req, FNET_RESOLVE_USER);
    printf("%s\n", req.username);
    printf("%d\n", req.rlen);
    
    fakio_request_resolve(buffer, 64, &req, FNET_RESOLVE_NET);
    printf("%s\n", req.addr);
    printf("%s\n", req.port);
    
    printf("%d\n", req.rlen);
    return 0;
}