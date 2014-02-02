#include "../src/fakio.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
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

long long bench_urandom(int times)
{
    int i;
    FILE *urandom;
    unsigned char key[1024];

    long long start = get_ustime_sec();
    for (i = 0; i < times; i++) {
        urandom = fopen ("/dev/urandom", "r");
        if (urandom == NULL) {
            fprintf (stderr, "Cannot open /dev/urandom!\n");
            return 0;
        }
        fread (key, sizeof(char), 1024, urandom);
        fclose(urandom);    
    }

    return (get_ustime_sec() - start);
}

long long bench_urandom2(int times)
{
    int fd, i, rc, len;
    unsigned char key[1024];
    len = 0;
    long long start = get_ustime_sec();
    for (i = 0; i < times; i++) {
        fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0) {
            fprintf (stderr, "Cannot open /dev/urandom!\n");
            return 0;
        }

        while (len < 1024) {
            rc = read(fd, key+len, 1024-len);
            if (rc < 0) {
                fprintf (stderr, "Cannot open /dev/urandom!\n");
                break;
            }
            len += rc;
        }
        close(fd);
        len = 0;
    }
    return (get_ustime_sec() - start);
}


long long bench_random(int times)
{
    int i;
    fcrypt_rand_t *r;
    r = fcrypt_rand_new();

    unsigned char key[1024];

    long long start = get_ustime_sec();
    for (i = 0; i < times; i++) {
        random_bytes(r, key, 1024);
    }
    return (get_ustime_sec() - start);
}



int main(int argc, char const *argv[])
{
    long long times1 = bench_urandom(atoi(argv[1]));
    long long times2 = bench_urandom2(atoi(argv[1]));
    long long times3 = bench_random(atoi(argv[1]));
    printf("%lld vs %lld vs %lld\n", times1, times2, times3);
    return 0;
}