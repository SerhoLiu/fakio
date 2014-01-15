#ifndef _FAKIO_USER_H_
#define _FAKIO_USER_H_

#include "fakio.h"

struct fuser {
    uint8_t username[MAX_USERNAME];
    int name_len;
    uint8_t key[32];
};


hashmap *fuser_userdict_create(unsigned long size);
int fuser_add_user(hashmap *users, const char *name, const char *password);
fuser_t *fuser_find_user(hashmap *users, uint8_t* name, int nlen);

#endif
