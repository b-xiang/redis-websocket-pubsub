CC = clang
CFLAGS = -g -Wall -Wextra -Werror -Wformat -Wformat-security -Werror=format-security -Wno-error=deprecated-declarations -pedantic -std=c11 -D_FORTIFY_SOURCE=2 -D_POSIX_SOURCE -D_BSD_SOURCE $(shell pkg-config --cflags hiredis) $(shell pkg-config --cflags libevent) $(shell pkg-config --cflags openssl)
LDFLAGS = $(shell pkg-config --libs hiredis) $(shell pkg-config --libs libevent) $(shell pkg-config --libs openssl)

OBJECTS = base64.o client_connection.o http.o json.o lexer.o logging.o pubsub_manager.o string_pool.o uri.o websocket.o xxhash.o

.PHONY: all clean

all: server test-base64 test-http test-json test-pubsub

clean:
	-rm -f *.o test-pubsub

test-base64: test-base64.o $(OBJECTS)
test-http: test-http.o $(OBJECTS)
test-json: test-json.o $(OBJECTS)
test-pubsub: test-pubsub.o $(OBJECTS)
server: server.o $(OBJECTS)
