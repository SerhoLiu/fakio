CC = gcc
CFLAGS = -O1 -g -Wall -D NCRYPT -lcrypto

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S), Linux)
	CFLAGS += -D USE_EPOLL
endif

OBJ = src/config.o src/fevent.o src/fnet.o \
      src/fcrypt.o src/fcontext.o src/fhandler.o

all: fakio-server fakio-local

fakio-server: $(OBJ)
	$(CC) $(CFLAGS) -o $@  src/fserver.c $(OBJ)

fakio-local: $(OBJ)
	$(CC) $(CFLAGS) -o $@ src/flocal.c $(OBJ)


.PHONY: clean
clean:
	-rm fakio-server fakio-local src/*.o