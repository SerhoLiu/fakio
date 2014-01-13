/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 */
 
#ifndef _EVENT_H_
#define _EVENT_H_

#define EV_NONE 0
#define EV_RDABLE 1
#define EV_WRABLE 2

#define EV_WAIT 1

struct event_loop;

typedef void ev_callback(struct event_loop *loop, int fd, int mask, void *evdata);

typedef struct ev_event {
    int mask;
    ev_callback *ev_read;  
    ev_callback *ev_write;
    void *evdata;       
} ev_event;

typedef struct ev_fired {
    int fd;
    int mask;   
} ev_fired;

typedef struct event_loop {
    int maxfd;
    int setsize;
    ev_event *events;
    ev_fired *fireds;
    int stop;
    void *apidata;
} event_loop;

event_loop *create_event_loop(int setsize);
void delete_event_loop(event_loop *loop);
int process_events(event_loop *loop, int flags);
char *get_event_api_name(void);
void start_event_loop(event_loop *loop);
void stop_event_loop(event_loop *loop);

int create_event(event_loop *loop, int fd, int mask,
                 ev_callback *cb, void *evdata);
void delete_event(event_loop *loop, int fd, int mask);
int get_event_mask(event_loop *loop, int fd);

#endif