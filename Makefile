CC := gcc
CFLAGS := -O1 -g -Wall
LIBS  := -lcrypto

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S), Linux)
	CFLAGS += -D USE_EPOLL
endif

BASE_OBJ = src/base/hashmap.o src/base/sha2.o src/base/ini.o src/base/fevent.o
ALL_OBJ = src/fconfig.o src/fnet.o src/fcrypt.o src/fcontext.o \
          src/fhandler.o src/fuser.o $(BASE_OBJ)

all: fakio-server fakio-client

fakio-server: $(ALL_OBJ)
	$(CC) $(CFLAGS) -o $@  $(ALL_OBJ) src/fserver.c  $(LIBS)

fakio-client: $(OBJ)
	$(CC) $(CFLAGS) -I ./src -o $@ clients/fclient.c $(OBJ) -lcrypto


.PHONY: clean
clean:
	-rm fakio-server fakio-local src/*.o src/base/*.o