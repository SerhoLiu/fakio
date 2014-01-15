#ifndef _FAKIO_USER_H_
#define _FAKIO_USER_H_

#include "fakio.h"

struct fuser {
    uint8_t username[MAX_USERNAME];
    int name_len;
    uint8_t key[16];
};

#endif
