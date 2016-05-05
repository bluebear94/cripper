CC=gcc
CFLAGS=-std=c11 -Wall -Werror -O3

release: cripper

cripper: cripper.o
	$(CC) $(CFLAGS) cripper.o -o cripper -static -lz

cripper.o: cripper.c cripper.h
	$(CC) $(CFLAGS) -c cripper.c

yukkuri:
	@echo "ゆっくりしていってね！"
