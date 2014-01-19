#include <stdio.h>
#include <time.h>


int main(int argc, char **argv)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    printf("%ld %ld\n", t.tv_sec, t.tv_nsec);
    return 0;
}