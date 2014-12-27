#ifndef _FAKIO_BUFFER_H_
#define _FAKIO_BUFFER_H_

#include <stdlib.h>

#include "fakio.h"

struct fbuffer {
    uint8_t buffer[BUFSIZE];
    int length;
    int start;
};


#define FBUF_CREATE(B) do {     \
    (B) = (struct fbuffer *)malloc(sizeof(struct fbuffer)); \
    if ((B) != NULL) {                        \
        (B)->length = (B)->start = 0;         \
    }                                          \
} while (0)

#define FBUF_FREE(B) (free(B))

#define FBUF_WRITE_AT(B) ((B)->buffer)

#define FBUF_COMMIT_WRITE(B, A) ((B)->length += (A))

#define FBUF_DATA_AT(B) ((B)->buffer + (B)->start)

#define FBUF_DATA_LEN(B) ((B)->length)

#define FBUF_COMMIT_READ(B, A) do { \
    (B)->length -= (A);                \
    if ((B)->length == 0) {            \
        (B)->start = 0;                \
    } else {                           \
        (B)->start += (A);             \
    }                                  \
} while (0);

#define FBUF_REST(B) ((B)->length = (B)->start = 0)
#define FBUF_WRITE_SEEK(B, A) ((B)->buffer+(A))
#define FBUF_DATA_SEEK(B, A) ((B)->buffer+(A))

#endif
