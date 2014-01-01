CC = gcc
CFLAGS = -O2 -Wall -D NDEBUG

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S), Linux)
	CFLAGS += -D USE_EPOLL
endif

OBJ = src/config.o src/fevent.o src/fnet.o src/fcrypt.o src/fcontext.o

all: fakio-server fakio-local

fakio-server: $(OBJ)
	$(CC) $(CFLAGS) -o $@  src/fserver.c $(OBJ)

fakio-local: $(OBJ)
	$(CC) $(CFLAGS) -o $@ src/flocal.c $(OBJ)


.PHONY: clean
clean:
	-rm fakio-server fakio-local *.o