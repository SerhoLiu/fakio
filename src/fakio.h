#ifndef _FAKIO_H_
#define _FAKIO_H_

#include "config.h"
#include "fcommon.h"
#include "fevent.h"
#include "fcontext.h"

struct fserver {
    config *cfg;
    context_list_t *list;
};

typedef struct fserver fserver_t;

#endif
