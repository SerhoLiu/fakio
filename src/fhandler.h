#ifndef _FAKIO_HANDLER_H_
#define _FAKIO_HANDLER_H_

#include "fevent.h"

void server_accept_cb(struct event_loop *loop, int fd, int mask, void *evdata);

void client_readable_cb(struct event_loop *loop, int fd, int mask, void *evdata);
void client_writable_cb(struct event_loop *loop, int fd, int mask, void *evdata);
void remote_writable_cb(struct event_loop *loop, int fd, int mask, void *evdata);
void remote_readable_cb(struct event_loop *loop, int fd, int mask, void *evdata);

#endif
