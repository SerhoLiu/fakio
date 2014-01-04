#include "fcrypt.h"
#include <unistd.h>
#include <fcntl.h>


void random_key(unsigned char *key, size_t keylen)
{
    int fd, rc, len;

    len = 0;
    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        return;
    }
    
    while (len < keylen) {
        rc = read(fd, key+len, keylen-len);
        if (rc < 0) {
            break;
        }
        len += rc;
    }
    close(fd);
}
