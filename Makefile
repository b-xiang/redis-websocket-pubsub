# Dependencies: libevent-dev libhiredis-dev libssl-dev pkg-config
BIN_DIR = bin
OBJ_DIR = objs
SRC_DIR = src
TEST_BIN_DIR = test-bin
TEST_OBJ_DIR = test-objs

CFLAGS = \
		-g -pedantic -std=c11 \
		-Wall -Wextra -Werror -Wformat -Wformat-security -Werror=format-security \
		-D_FORTIFY_SOURCE=2 -D_POSIX_SOURCE -D_BSD_SOURCE \
		-I$(SRC_DIR)/ \
		$(shell pkg-config --cflags hiredis) $(shell pkg-config --cflags libevent) $(shell pkg-config --cflags libevent_openssl) $(shell pkg-config --cflags openssl)
LDFLAGS = \
		$(shell pkg-config --libs hiredis) $(shell pkg-config --libs libevent) $(shell pkg-config --libs libevent_openssl) $(shell pkg-config --libs openssl)

TEST_CFLAGS = -fprofile-arcs -ftest-coverage
TEST_LDFLAGS = -fprofile-arcs -ftest-coverage

BASE_HEADERS = \
		$(SRC_DIR)/base64.h \
		$(SRC_DIR)/client_connection.h \
		$(SRC_DIR)/compat_endian.h \
		$(SRC_DIR)/compat_openssl.h \
		$(SRC_DIR)/http.h \
		$(SRC_DIR)/json.h \
		$(SRC_DIR)/lexer.h \
		$(SRC_DIR)/logging.h \
		$(SRC_DIR)/pubsub_manager.h \
		$(SRC_DIR)/status.h \
		$(SRC_DIR)/string_pool.h \
		$(SRC_DIR)/uri.h \
		$(SRC_DIR)/websocket.h \
		$(SRC_DIR)/xxhash.h
BASE_OBJECTS = \
		base64.o \
		client_connection.o \
		compat_openssl.o \
		http.o \
		json.o \
		lexer.o \
		logging.o \
		pubsub_manager.o \
		string_pool.o \
		uri.o \
		websocket.o \
		xxhash.o

OBJECTS = $(addprefix $(OBJ_DIR)/,$(BASE_OBJECTS))
TEST_OBJECTS = $(addprefix $(TEST_OBJ_DIR)/,$(BASE_OBJECTS))

BINARIES = \
		$(BIN_DIR)/server
TEST_BINARIES = \
		$(TEST_BIN_DIR)/test-base64 \
		$(TEST_BIN_DIR)/test-http \
		$(TEST_BIN_DIR)/test-json \
		$(TEST_BIN_DIR)/test-pubsub


.PHONY: all clean wc


all: $(BINARIES) $(TEST_BINARIES)

clean:
	-rm -rf $(BINARIES)
	-rm -rf $(TEST_BINARIES)
	-rm -rf $(OBJECTS)
	-rm -rf $(TEST_OBJECTS)

wc:
	find src -name '*.c' -or -name '*.h' | grep -v test- | xargs wc -l

$(BIN_DIR):
	mkdir -p $@

$(OBJ_DIR):
	mkdir -p $@

$(TEST_BIN_DIR):
	mkdir -p $@

$(TEST_OBJ_DIR):
	mkdir -p $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(BASE_HEADERS) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TEST_OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(BASE_HEADERS) | $(TEST_OBJ_DIR)
	$(CC) $(CFLAGS) $(TEST_CFLAGS) -c -o $@ $<


$(BIN_DIR)/server: $(OBJ_DIR)/server.o $(OBJECTS) | $(BIN_DIR)
	$(CC) -o $@ $^ $(LDFLAGS)

$(TEST_BIN_DIR)/test-base64: $(TEST_OBJ_DIR)/test-base64.o $(TEST_OBJECTS) | $(TEST_BIN_DIR)
	$(CC) -o $@ $^ $(LDFLAGS) $(TEST_LDFLAGS)

$(TEST_BIN_DIR)/test-http: $(TEST_OBJ_DIR)/test-http.o $(TEST_OBJECTS) | $(TEST_BIN_DIR)
	$(CC) -o $@ $^ $(LDFLAGS) $(TEST_LDFLAGS)

$(TEST_BIN_DIR)/test-json: $(TEST_OBJ_DIR)/test-json.o $(TEST_OBJECTS) | $(TEST_BIN_DIR)
	$(CC) -o $@ $^ $(LDFLAGS) $(TEST_LDFLAGS)

$(TEST_BIN_DIR)/test-pubsub: $(TEST_OBJ_DIR)/test-pubsub.o $(TEST_OBJECTS) | $(TEST_BIN_DIR)
	$(CC) -o $@ $^ $(LDFLAGS) $(TEST_LDFLAGS)
