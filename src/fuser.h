#ifndef _FAKIO_USER_H_
#define _FAKIO_USER_H_

#include "fakio.h"

struct fuser {
    char username[MAX_USERNAME];
    uint8_t key[16];
};

#endif
