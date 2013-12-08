#ifndef _FAKIO_BUFFER_H_
#define _FAKIO_BUFFER_H_

#include <string.h>
#include <stdlib.h>

#define BUFSIZE 4072

typedef struct {
    unsigned char buffer[BUFSIZE];
    int length;
    int start;
} fbuffer;


#define FBUFFER_CREATE(B) do {     \
    (B) = (fbuffer *)malloc(sizeof(fbuffer)); \
    if ((B) != NULL) {                        \
        (B)->length = (B)->start = 0;         \
    }                                          \
} while (0)

#define FBUFFER_READ_AT(B) ((B)->buffer + start)

#define FBUFFER_READ_LEN(B) ((B)->length)

#define FUBFFER_COMMIT_READ(B, A) do { \
    (B)->length -= (A);                \
    if ((B)->length == 0) {            \
        (B)->start = 0;                \
    } else {                           \
        (B)->start += (A);             \
    }                                  \
} while (0);

#define FUBFFER_COMMIT_WRITE(B, A) ((B)->length += (A))

#endif