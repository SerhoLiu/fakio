#include "fcrypt.h"
#include <fcntl.h>


void random_bytes(uint8_t *bytes, size_t len)
{
    int fd, rc, rlen;

    rlen = 0;
    fd = open("/dev/urandom", O_RDONLY);
    if (fd > 0) {
        while (rlen < len) {
            rc = read(fd, bytes+rlen, len-rlen);
            if (rc < 0) {
                break;
            }
            rlen += rc;
        }
        close(fd);    
    }
    
    if (rlen < len) {
        //do sth.
    }
}
