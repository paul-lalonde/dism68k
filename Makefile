CC = clang
CFLAGS = -g -std=c99 -pedantic -Wall
OBJECTS = dis.o dis68k.o label.o basicblock.o

all: dis

label.o: label.c

dis.o: dis.c
	$(CC) $(CFLAGS) -c dis.c

dis68k.o: dis68k.c
	$(CC) $(CFLAGS) -c dis68k.c

dis: $(OBJECTS)
	$(CC) $(OBJECTS) -g -o dis -lncurses

clean:
	rm -f *.o dis