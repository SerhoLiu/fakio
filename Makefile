CC=gcc
CFLAGS= -O1 -g -Wall

OBJ = fevent.o fnet.o fcrypt.o

all: server local

server: $(OBJ)
	$(CC) $(CFLAGS) -o server  server.c $(OBJ)

local: $(OBJ)
	$(CC) $(CFLAGS) -o local local.c $(OBJ)


.PHONY: clean
clean:
	-rm server local *.o