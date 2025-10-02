CFLAGS=-Wall -Wextra -std=c11 -pedantic -ggdb

mymalloc: main.c
	$(CC) $(CFLAGS) -o mymalloc main.c
