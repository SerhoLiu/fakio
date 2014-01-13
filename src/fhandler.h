#ifndef _FAKIO_HANDLER_H_
#define _FAKIO_HANDLER_H_

#include "fevent.h"

void server_accept_cb(struct event_loop *loop, int fd, int mask, void *evdata);

#endif
