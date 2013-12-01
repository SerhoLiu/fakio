CC = gcc
CFLAGS = -O1 -g -Wall -D NCRYPT

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S), Linux)
	CFLAGS += -D USE_EPOLL
endif

OBJ = config.o fevent.o fnet.o fcrypt.o fcontext.o

all: fakio-server fakio-local

fakio-server: $(OBJ)
	$(CC) $(CFLAGS) -o $@  fserver.c $(OBJ)

fakio-local: $(OBJ)
	$(CC) $(CFLAGS) -o $@ flocal.c $(OBJ)


.PHONY: clean
clean:
	-rm fakio-server fakio-local *.o