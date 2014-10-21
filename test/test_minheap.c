#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../src/base/minheap.h"

min_heap_t minheap;

time_event *add_time_event(int sec)
{
    time_event *te = calloc(1, sizeof(*te));
    te->when_sec = sec;

    min_heap_elem_init(te);
    min_heap_push(&minheap, te);
    return te;

}

void test_adds_tv()
{
    time_event *te;

    add_time_event(3);
    te = add_time_event(5);
    add_time_event(4);
    add_time_event(67);
    add_time_event(56);
    add_time_event(34);
    add_time_event(78);

    // time_event **p = minheap.p;

    // int i;
    // for (i = 0; i < minheap.n; i++) {
    //     te = *(p + i);
    //     printf("%ld-%ld\n", te->min_heap_idx, te->when_sec);
    // }

    te->when_sec += 52;
    min_heap_adjust(&minheap, te);

    while ((te = min_heap_pop(&minheap)) != NULL) {
        printf("%ld\n", te->when_sec);
    }
}

int main(int argc, char **argv)
{
    min_heap_ctor(&minheap);

    test_adds_tv();

    return 0;
}