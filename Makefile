CC = gcc
CFLAGS = -pedantic -Wall -g -Ilibspnav/src  -I/usr/local/include
LDFLAGS = -Llibspnav -lspnav -lX11 -lm -lpthread
LDFLAGS_JOY = -lrt -lm -lpthread -lstdc++

.PHONY: all
all: single_test multi_test joy_test test_socket_can

# simple_x11: simple.c
# 	$(CC) $(CFLAGS) -DBUILD_X11 -o $@ $< $(LDFLAGS)

single_test: single_main.cpp
	$(CC) $(CFLAGS) -DBUILD_AF_UNIX -o $@ $< $(LDFLAGS)


joy_test: joy_single_main.cpp
	$(CC) $(CFLAGS) -DBUILD_AF_UNIX -o $@ $< $(LDFLAGS_JOY)


multi_test: multi_main.cpp
	$(CC) $(CFLAGS) -DBUILD_AF_UNIX -o $@ $< $(LDFLAGS)


test_socket_can: test_socket_can.c
	$(CC) $(CFLAGS) -DBUILD_AF_UNIX -o $@ $< $(LDFLAGS)

.PHONY: clean
clean:
	rm -f single_test multi_test joy_test
