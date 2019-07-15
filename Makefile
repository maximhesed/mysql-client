CC=gcc

CFLAGS=-Wall -g -Wno-deprecated-declarations `pkg-config --cflags gtk+-3.0` `mysql_config --cflags`
LDFLAGS=`pkg-config --libs gtk+-3.0` `mysql_config --libs`

OBJS=main.o

PROGNAME=prog

.PHONY: clean

all: $(OBJS)
	$(CC) $(LDFLAGS) -o $(PROGNAME) $^

%.o: %.c
	$(CC) $(CFLAGS) -c $^ -o $@

clean:
	rm -f *.o $(PROGNAME)
