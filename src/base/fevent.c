/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 */

#include "fevent.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include "minheap.h"


#ifdef USE_EPOLL

#include <sys/epoll.h>

typedef struct ev_api_state {
    int epfd;
    struct epoll_event *events;
} ev_api_state;

static int ev_api_create(event_loop *loop)
{
    ev_api_state *state = malloc(sizeof(ev_api_state));
    if (state == NULL) return -1;
    
    state->events = malloc(sizeof(struct epoll_event) *loop->setsize);
    if (state->events == NULL) {
        free(state);
        return -1;
    }
    state->epfd = epoll_create(1024); /* 1024 is just an hint for the kernel */
    if (state->epfd == -1) {
        free(state->events);
        free(state);
        return -1;
    }
    loop->apidata = state;
    return 0;
}

static void ev_api_free(event_loop *loop)
{
    ev_api_state *state = loop->apidata;
    close(state->epfd);
    free(state->events);
    free(state);
}

static int ev_api_addevent(event_loop *loop, int fd, int mask)
{
    ev_api_state *state = loop->apidata;
    struct epoll_event ee;
    
    int op = loop->events[fd].mask == EV_NONE ?
            EPOLL_CTL_ADD : EPOLL_CTL_MOD;

    ee.events = 0;
    mask |= loop->events[fd].mask; /* Merge old events */
    if (mask & EV_RDABLE) ee.events |= EPOLLIN;
    if (mask & EV_WRABLE) ee.events |= EPOLLOUT;
    ee.data.u64 = 0;
    ee.data.fd = fd;
    if (epoll_ctl(state->epfd, op, fd, &ee) == -1) return -1;
    return 0;
}

static void ev_api_delevent(event_loop *loop, int fd, int mask)
{
    ev_api_state *state = loop->apidata;
    struct epoll_event ee;
    int nmask = loop->events[fd].mask & (~mask);

    ee.events = 0;
    if (nmask & EV_RDABLE) ee.events |= EPOLLIN;
    if (nmask & EV_WRABLE) ee.events |= EPOLLOUT;
    ee.data.u64 = 0; /* avoid valgrind warning */
    ee.data.fd = fd;
    if (nmask != EV_NONE) {
        epoll_ctl(state->epfd, EPOLL_CTL_MOD, fd, &ee);
    } else {
        /* Note, Kernel < 2.6.9 requires a non null event pointer even for
         * EPOLL_CTL_DEL. */
        epoll_ctl(state->epfd, EPOLL_CTL_DEL, fd, &ee);
    }
}

static int ev_api_poll(event_loop *loop, struct timeval *tvp)
{
    ev_api_state *state = loop->apidata;
    int retval, numevents = 0;

    retval = epoll_wait(state->epfd, state->events, loop->setsize,
            tvp ? (tvp->tv_sec*1000 + tvp->tv_usec/1000) : -1);
    if (retval > 0) {
        int j;

        numevents = retval;
        for (j = 0; j < numevents; j++) {
            int mask = 0;
            struct epoll_event *e = state->events+j;

            if (e->events & EPOLLIN) mask |= EV_RDABLE;
            if (e->events & EPOLLOUT) mask |= EV_WRABLE;
            if (e->events & EPOLLERR) mask |= EV_WRABLE;
            if (e->events & EPOLLHUP) mask |= EV_WRABLE;
            loop->fireds[j].fd = e->data.fd;
            loop->fireds[j].mask = mask;
        }
    }
    return numevents;
}

static char *ev_api_name(void)
{
    return "epoll";
}

#else

#include <sys/select.h>

typedef struct ev_api_state {
    fd_set rfds, wfds;
    fd_set crfds, cwfds;
} ev_api_state;

static int ev_api_create(event_loop *loop)
{
    ev_api_state *state = malloc(sizeof(ev_api_state));
    if (state == NULL) return -1;
    
    FD_ZERO(&state->rfds);
    FD_ZERO(&state->wfds);
    loop->apidata = state;
    return 0;
}

static void ev_api_free(event_loop *loop)
{
    free(loop->apidata);
}

static int ev_api_addevent(event_loop *loop, int fd, int mask)
{
    ev_api_state *state = loop->apidata;
    if (state == NULL) return -1;
    
    if (mask & EV_RDABLE) FD_SET(fd, &state->rfds);
    if (mask & EV_WRABLE) FD_SET(fd, &state->wfds);
    return 0;
}

static void ev_api_delevent(event_loop *loop, int fd, int mask)
{
    ev_api_state *state = loop->apidata;
    if (state == NULL) return;

    if (mask & EV_RDABLE) FD_CLR(fd, &state->rfds);
    if (mask & EV_WRABLE) FD_CLR(fd, &state->wfds);
}

static int ev_api_poll(event_loop *loop, struct timeval *tvp)
{
    ev_api_state *state = loop->apidata;
    if (state == NULL) return -1;

    int retval, j, numevents = 0;

    memcpy(&state->crfds, &state->rfds, sizeof(fd_set));
    memcpy(&state->cwfds, &state->wfds, sizeof(fd_set));

    retval = select(loop->maxfd + 1, &state->crfds, &state->cwfds, NULL, tvp);

    if (retval > 0) {
        for (j = 0; j <= loop->maxfd; j++) {
            int mask = 0;
            ev_event *ev = &loop->events[j];

            if (ev->mask == EV_NONE) continue;
            if (ev->mask & EV_RDABLE && FD_ISSET(j, &state->crfds))
                mask |= EV_RDABLE;
            if (ev->mask & EV_WRABLE && FD_ISSET(j, &state->cwfds))
                mask |= EV_WRABLE;
            loop->fireds[numevents].fd = j;
            loop->fireds[numevents].mask = mask;
            numevents++;
        }
    }
    return numevents;
}

static char *ev_api_name(void)
{
    return "select";
}

#endif

// 创建一个新的事件状态
event_loop *create_event_loop(int setsize)
{
    event_loop *loop;
    int i;

    if ((loop = malloc(sizeof(*loop))) == NULL) {
        return NULL;
    }

    loop->timeheap = malloc(sizeof(min_heap_t));
    if (loop->timeheap == NULL) {
        free(loop);
        return NULL;
    }
    min_heap_ctor(loop->timeheap);

    loop->events = malloc(sizeof(ev_event) * setsize);
    loop->fireds =  malloc(sizeof(ev_fired) * setsize);
    if (loop->events == NULL || loop->fireds == NULL) {
        free(loop->events);
        free(loop->fireds);
        free(loop->timeheap);
        free(loop);
        return NULL;
    }

    loop->setsize = setsize;
    loop->stop = 0;
    loop->maxfd = -1;
    if (ev_api_create(loop) == -1) {
        free(loop->events);
        free(loop->fireds);
        free(loop);
        return NULL;
    }

    for (i = 0; i < setsize; i++)
        loop->events[i].mask = EV_NONE;
    return loop;
}

void delete_event_loop(event_loop *loop)
{
    ev_api_free(loop);
    free(loop->events);
    free(loop->fireds);
    min_heap_dtor(loop->timeheap);
    free(loop->timeheap);
    free(loop);
}


// 创建文件事件
int create_event(event_loop *loop, int fd, int mask, 
                 ev_callback *cb, void *evdata)
{
    if (fd < 1) return -1;
    // fd 的数量超过 eventLoop 允许的最大数量
    if (fd >= loop->setsize) return -1;
    ev_event *ev = &loop->events[fd];

    // 将 fd 入队
    if (ev_api_addevent(loop, fd, mask) == -1)
        return -1;

    //LOG_INFO("%d add envent!\n", fd);
    ev->mask |= mask;

    if (mask & EV_RDABLE) ev->ev_read = cb;
    if (mask & EV_WRABLE) ev->ev_write = cb;
    ev->evdata = evdata;

    // 如果有需要，就更新 eventLoop 的 maxfd 属性
    if (fd > loop->maxfd)
        loop->maxfd = fd;

    return 0;
}

// 删除文件事件
void delete_event(event_loop *loop, int fd, int mask)
{
    if (fd >= loop->setsize) return;
    ev_event *ev = &loop->events[fd];

    if (ev->mask == EV_NONE) return;

    // 恢复 mask
    ev->mask = ev->mask & (~mask);

    if (fd == loop->maxfd && ev->mask == EV_NONE) {
        /* Update the max fd */
        int j;

        // 尝试减少 eventLoop->maxfd 的值
        for (j = loop->maxfd-1; j >= 0; j--)
            if (loop->events[j].mask != EV_NONE) break;
        loop->maxfd = j;
    }
    ev_api_delevent(loop, fd, mask);
}

// 获取和给定 fd 对应的文件事件的 mask 值
int get_event_mask(event_loop *loop, int fd)
{
    if (fd >= loop->setsize) return 0;
    ev_event *ev = &loop->events[fd];
    return ev->mask;
}


/* 时间事件 */
static inline void get_time(long *seconds, long *microseconds)
{
#ifdef USE_MONOTONIC

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    *seconds = ts.tv_sec;
    *microseconds = ts.tv_nsec / 1000;

#else

    struct timeval tv;
    gettimeofday(&tv, NULL);
    *seconds = tv.tv_sec;
    *microseconds = tv.tv_usec;

#endif
}

/* 这里使用毫秒，毕竟纳秒用在这里太小了 */
static void add_millisec_to_now(long long milliseconds, long *sec, long *us)
{
    long cur_sec, cur_us, when_sec, when_us;

    get_time(&cur_sec, &cur_us);

    when_sec = cur_sec + milliseconds / 1000;
    when_us = cur_us + (milliseconds % 1000) * 1000;

    if (when_us >= 1000000) {
        when_sec ++;
        when_us -= 1000000;
    }
    *sec = when_sec;
    *us = when_us;
}

time_event *create_time_event(event_loop *loop, long long milliseconds,
                            time_ev_callback *cb, void *evdata)
{
    time_event *te;
    te = malloc(sizeof(*te));
    if (te == NULL) return NULL;

    min_heap_elem_init(te);

    add_millisec_to_now(milliseconds, &te->when_sec, &te->when_usec);
    
    te->time_call = cb;
    te->evdata = evdata;

    min_heap_push(loop->timeheap, te);

    return te;
}

int delete_time_event(event_loop *loop, time_event *te)
{
    if (min_heap_delete(loop->timeheap, te) != 0) {
        return -1;
    }

    free(te);

    return 0;
}

static int process_time_events(event_loop *loop)
{
    int processed = 0;
    time_event *te;
    
    if (min_heap_size(loop->timeheap) == 0) return 0;

    long now_sec, now_us;

    while ((te = min_heap_top(loop->timeheap))) {
        
        get_time(&now_sec, &now_us);

        if (now_sec < te->when_sec ||
            (now_sec == te->when_sec && now_us < te->when_usec)) break;

        int retval;
        retval = te->time_call(loop, te->evdata);
        processed++;
        
        if (retval != EV_TIMER_END) {
            add_millisec_to_now(retval, &te->when_sec, &te->when_usec);
        } else {
            delete_time_event(loop, te);
        }  
    }

    return processed;
}


int process_events(event_loop *loop, int flags)
{
    int processed = 0, numevents;

    /* 如果 flags 为0,则直接返回 */
    if (!(flags & EV_TIME_EVENTS) && !(flags & EV_FILE_EVENTS)) return 0;
    
    if (loop->maxfd != -1 ||
        ((flags & EV_TIME_EVENTS) && !(flags & EV_DONT_WAIT))) {

        int j;
        time_event *shortest = NULL;
        struct timeval tv, *tvp;

        if (flags & EV_TIME_EVENTS && !(flags & EV_DONT_WAIT)) {
            shortest = min_heap_top(loop->timeheap);
        }

        if (shortest != NULL) {
            long now_sec, now_us;
            get_time(&now_sec, &now_us);

            tvp = &tv;
            tvp->tv_sec = shortest->when_sec - now_sec;
            
            if (shortest->when_usec < now_us) {
                tvp->tv_usec = ((shortest->when_usec+1000000) - now_us);
                tvp->tv_sec--;
            } else {
                tvp->tv_usec = shortest->when_usec - now_us;
            }

            // 时间差小于 0 ，说明事件已经可以执行了，将秒和毫秒设为 0 （不阻塞）
            if (tvp->tv_sec < 0) tvp->tv_sec = 0;
            if (tvp->tv_usec < 0) tvp->tv_usec = 0;
        } else {
            if (flags & EV_DONT_WAIT) {
                tv.tv_sec = tv.tv_usec = 0;
                tvp = &tv;
            } else {
                tvp = NULL;
            }
        }

        // 处理文件事件
        numevents = ev_api_poll(loop, tvp);
        for (j = 0; j < numevents; j++) {
            
            /* 根据 fired 数组，从 events 数组中取出事件 */
            ev_event *ev = &loop->events[loop->fireds[j].fd];
            int mask = loop->fireds[j].mask;
            int fd = loop->fireds[j].fd;
            int rfired = 0;

            /*
             * 因为一个已处理的事件有可能对当前被执行的事件进行了修改, 因此在执行当前事件前,
             * 需要再进行一次检查,确保事件可以被执行
             */
            if (ev->mask & mask & EV_RDABLE) {
                rfired = 1;
                ev->ev_read(loop, fd , mask, ev->evdata);
            }
            if (ev->mask & mask & EV_WRABLE) {
                if (!rfired || ev->ev_read != ev->ev_write)
                    ev->ev_write(loop, fd, mask, ev->evdata);
            }
            processed++;
        }
    }

    if (flags & EV_TIME_EVENTS) {
        processed += process_time_events(loop);
    }
    
    return processed;
}

char *get_event_api_name(void)
{
    return ev_api_name();
}

void start_event_loop(event_loop *loop)
{
    loop->stop = 0;
    while (!loop->stop) {
        process_events(loop, EV_ALL_EVENTS);  /* 不等待 */
    }
}

void stop_event_loop(event_loop *loop)
{
    loop->stop = 1;
}
