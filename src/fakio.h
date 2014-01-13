#ifndef _FAKIO_H_
#define _FAKIO_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define BUFSIZE (4096+16)
#define MAX_USERNAME 256

#include "fnet.h"
#include "flog.h"
#include "fbuffer.h"
#include "fuser.h"
#include "config.h"
#include "fevent.h"
#include "fcontext.h"
#include "fcrypt.h"

struct fserver {
    config *cfg;
    context_list_t *list;
};

typedef struct fserver fserver_t;

#endif
