CC=gcc
CFLAGS=-ggdb -02 -DDO_LFDEBUG -D_REENTRANT -D_GNU_SOURCE -Wall -Wno-unused -fno-strict-aliasing -DBASE_THREADSAFE -I/usr/include/libxml2
LIBS=-lxml2 -lzmq -lcurl -ljson

all: linkfinder.o
	$(CC) $(CFLAGS) $(LIBS) linkfinder.o -o linkfinder

linkfinder.o: 
	$(CC) $(CFLAGS) -c linkfinder.c -o linkfinder.o

clean:
	rm -rf linkfinder.o
	rm -rf linkfinder
