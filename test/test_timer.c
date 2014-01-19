#include <stdio.h>
#include <time.h>
#include "../src/base/fevent.h"

long print_callback(struct event_loop *loop, long long id, void *evdata)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    printf("%ld %ld\n", t.tv_sec, t.tv_nsec);
    return 2000;
}

int main(int argc, char **argv)
{
    event_loop *loop = create_event_loop(100);
    create_time_event(loop, 10000, &print_callback, NULL);

    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    printf("%ld %ld\n", t.tv_sec, t.tv_nsec);
    start_event_loop(loop);
    return 0;
}