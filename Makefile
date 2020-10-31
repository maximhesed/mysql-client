CC = gcc
CFLAGS = -Wall -Wextra -Wno-unused-parameter -Wno-parentheses -g\
	`pkg-config --cflags gtk+-3.0` `mysql_config --cflags`
LDFLAGS = `pkg-config --libs gtk+-3.0` `mysql_config --libs`
OBJS = main.o
PROGNAME = prog

all: $(OBJS)
	$(CC) $(LDFLAGS) -o $(PROGNAME) $^

%.o: %.c
	$(CC) $(CFLAGS) -c $^ -o $@

.PHONY: clean
clean:
	rm -f *.o $(PROGNAME)
