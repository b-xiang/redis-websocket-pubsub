CC = clang
CFLAGS = -Wall -Wextra -Werror -Wformat -Wformat-security -Werror=format-security -Wno-error=deprecated-declarations -D_FORTIFY_SOURCE=2 -fstack-protector-strong -pedantic -std=c11 $(shell pkg-config --cflags hiredis) $(shell pkg-config --cflags libevent) $(shell pkg-config --cflags openssl)
LDFLAGS = $(shell pkg-config --libs hiredis) $(shell pkg-config --libs libevent) $(shell pkg-config --libs openssl)

.PHONY: all clean

all: test-pubsub test-http test-base64

clean:
	-rm -f *.o test-pubsub

test-base64: test-base64.o base64.o
test-http: test-http.o base64.o http.o lexer.o uri.o websocket.o
test-pubsub: test-pubsub.o base64.o http.o lexer.o uri.o websocket.o
