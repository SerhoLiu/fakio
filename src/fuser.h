#ifndef _FAKIO_USER_H_
#define _FAKIO_USER_H_

#include "fcommon.h"

struct fuser {
    char username[MAX_USERNAME];
    uint8_t key[16];
};

typedef struct user fuser_t;

#endif
