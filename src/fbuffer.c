#include "fbuffer.h"
#include <stdlib.h>

fbuffer *fbuffer_create()
{
    fbuffer *buffer;
    buffer = (fbuffer *)malloc(sizeof(*buffer));
    if (buffer == NULL) return NULL;

    buffer->length  = BUFSIZE + 1;
    buffer->start = 0;
    buffer->end = 0;

    return buffer;
}


void fbuffer_destroy(fbuffer *buffer)
{
    if (buffer == NULL) return;
    free(buffer);
}

int fbuffer_write(fbuffer *buffer, unsigned char *data, int length)
{
    if (fbuffer_available_data(buffer) == 0) {
        buffer->start = buffer->end = 0;
    }

    if (length > fbuffer_available_space(buffer)) {
        return -1;
    }

    void *result = memcpy(fbuffer_ends_at(buffer), data, length);
    if (result == NULL) {
        return -1;
    }

    fbuffer_commit_write(buffer, length);
    return length;

}

int fbuffer_read(fbuffer *buffer, unsigned char *target, int amount)
{
    if (amount > fbuffer_available_data(buffer)) {
        return -1;
    }

    void *result = memcpy(target, fbuffer_starts_at(buffer), amount);
    if (result == NULL) {
        return -1;
    }

    fbuffer_commit_read(buffer, amount);

    if(buffer->end == buffer->start) {
        buffer->start = buffer->end = 0;
    }

    return amount;
}
