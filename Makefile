CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99 -DLCAR_STANDALONE -Isrc

all: lcar_esp32

lcar_esp32: src/lcar.h src/main.c
	$(CC) $(CFLAGS) -o $@ src/main.c

test: lcar_esp32
	./lcar_esp32

clean:
	rm -f lcar_esp32

.PHONY: all test clean
