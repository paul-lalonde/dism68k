CC = gcc
CFLAGS = -g -std=c99 -pedantic -Wall
OBJECTS = dis.o dis68k.o label.o basicblock.o buffer.o

all: dis

%.o: %.c dat.h
	$(CC) $(CFLAGS) -c $< -o $@

dis.o: dis.c
	$(CC) $(CFLAGS) -c dis.c

dis68k.o: dis68k.c
	$(CC) $(CFLAGS) -c dis68k.c

dis: $(OBJECTS)
	$(CC) $(OBJECTS) -g -o dis -lncurses

clean:
	rm -f *.o dis