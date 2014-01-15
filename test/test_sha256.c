#include "../src/base/sha2.h"
#include <string.h>
#include <stdio.h>


int main(int argc, char const *argv[])
{
    uint8_t output[32];
    sha2("SErHo", 5, output, 0);

    int i = 0;
    for (i = 0; i < 32; i++) {
        printf("%d ", output[i]);
    }
    printf("\n");
    return 0;
}