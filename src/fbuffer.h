#ifndef _FAKIO_BUFFER_H_
#define _FAKIO_BUFFER_H_

#include <string.h>

#define BUFSIZE 1536

typedef struct {
    unsigned char buffer[BUFSIZE];
    int length;
    int start;
    int end;
} fbuffer;

fbuffer *fbuffer_create(int length);

void fbuffer_destroy(fbuffer *buffer);

int fbuffer_read(fbuffer *buffer, unsigned char *target, int amount);

int fbuffer_write(fbuffer *buffer, unsigned char *data, int length);

int fbuffer_empty(fbuffer *buffer);

int fbuffer_full(fbuffer *buffer);

int fbuffer_available_data(fbuffer *buffer);

int fbuffer_available_space(fbuffer *buffer);

char *fbuffer_gets(fbuffer *buffer, int amount);

#define fbuffer_available_data(B) (((B)->end + 1) % (B)->length - (B)->start - 1)

#define fbuffer_available_space(B) ((B)->length - (B)->end - 1)

#define fbuffer_full(B) (fbuffer_available_data((B)) - (B)->length == 0)

#define fbuffer_empty(B) (fbuffer_available_data((B)) == 0)

#define fbuffer_puts(B, D) fbuffer_write((B), bdata((D)), blength((D)))

#define fbuffer_get_all(B) fbuffer_gets((B), fbuffer_available_data((B)))

#define fbuffer_starts_at(B) ((B)->buffer + (B)->start)

#define fbuffer_ends_at(B) ((B)->buffer + (B)->end)

#define fbuffer_commit_read(B, A) ((B)->start = ((B)->start + (A)) % (B)->length)

#define fbuffer_commit_write(B, A) ((B)->end = ((B)->end + (A)) % (B)->length)

#endif