#ifndef _FAKIO_H_
#define _FAKIO_H_

#include "fcommon.h"
#include "fevent.h"
#include "fcontext.h"

struct fserver {
    ev_callback *handshake;
    context_list_t *list;
};

typedef struct fserver fserver_t;

#endif
