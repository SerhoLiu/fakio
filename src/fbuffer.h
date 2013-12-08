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


static  inline fbuffer *fbuffer_create()
{
    fbuffer *buffer;
    buffer = (fbuffer *)malloc(sizeof(*buffer));
    if (buffer == NULL) return NULL;

    buffer->length  = 0;
    buffer->start = 0;

    return buffer;
}


static inline void fbuffer_destroy(fbuffer *buffer)
{
    if (buffer != NULL) free(buffer); 
}


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