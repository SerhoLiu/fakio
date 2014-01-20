#include "fuser.h"
#include <stdlib.h>
#include "base/sha2.h"


static int username_compare(const void *desc, int dlen,
                            const void *src, int slen)
{
    if (dlen != slen) {
        return dlen - slen;
    }

    return memcmp(desc, src, slen);
}

static void user_delete(const void *key, void **value, const void *other)
{
    free(*value);
}

hashmap *fuser_userdict_create(unsigned long size)
{
    return hashmap_new(size, &username_compare);
}

void fuser_userdict_destroy(hashmap *users)
{
    hashmap_map(users, &user_delete, NULL);
    hashmap_free(users);
}

int fuser_add_user(hashmap *users, const char *name, const char *password)
{
    if (users == NULL || name == NULL || password == NULL) return 0;

    size_t nlen = strlen(name);
    if (nlen > MAX_USERNAME) return 0;
    
    size_t plen = strlen(password);

    if (nlen == 0 || plen == 0) return 0;

    fuser_t *user = malloc(sizeof(*user));
    if (user == NULL) return 0;

    int i;
    for (i = 0; i < nlen; i++) {
        user->username[i] = name[i];
    }
    user->name_len = nlen;

    sha2((uint8_t *)password, plen, user->key, 0);

    return hashmap_put(users, user->username, nlen, user);
}


fuser_t *fuser_find_user(hashmap *users, uint8_t* name, int nlen)
{
    if (users == NULL || name == NULL || nlen == 0) return NULL;

    return hashmap_get(users, name, nlen);
}
