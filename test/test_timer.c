#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../src/base/fevent.h"

long print_callback(struct event_loop *loop, void *evdata)
{
    char *str = evdata;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    printf("%s: %ld %ld\n", str, t.tv_sec, t.tv_nsec);
    return 2000;
}

int main(int argc, char **argv)
{
    event_loop *loop = create_event_loop(100);

    char *str1 = strdup("Test1");
    create_time_event(loop, 10000, &print_callback, str1);

    char *str2 = strdup("Test2");
    create_time_event(loop, 5000, &print_callback, str2);

    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    printf("%ld %ld\n", t.tv_sec, t.tv_nsec);
    start_event_loop(loop);
    return 0;
}