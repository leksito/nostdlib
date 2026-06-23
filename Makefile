CC     = gcc
# CFLAGS = -std=gnu11 -Wall -Wextra -g
CFLAGS = -Wall -Wextra -g

.PHONY: all test valgrind clean

all: test

test: test.c nostdlib.h
	$(CC) $(CFLAGS) test.c -o test && ./test

valgrind: test.c nostdlib.h
	$(CC) $(CFLAGS) test.c -o test
	valgrind --leak-check=full --error-exitcode=1 ./test

clean:
	rm -f test a.out
