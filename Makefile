CC = clang
CFLAGS = \
		-g -pedantic -std=c11 \
		-Wall -Wextra -Werror -Wformat -Wformat-security -Werror=format-security -Wno-error=deprecated-declarations \
		-D_FORTIFY_SOURCE=2 -D_POSIX_SOURCE -D_BSD_SOURCE \
		$(shell pkg-config --cflags hiredis) $(shell pkg-config --cflags libevent) $(shell pkg-config --cflags openssl)
LDFLAGS = \
		$(shell pkg-config --libs hiredis) $(shell pkg-config --libs libevent) $(shell pkg-config --libs openssl)

TEST_CFLAGS = -fprofile-instr-generate -fcoverage-mapping
TEST_LDFLAGS = -fprofile-instr-generate -fcoverage-mapping

BIN_DIR = bin
OBJ_DIR = objs
TEST_BIN_DIR = test-bin
TEST_OBJ_DIR = test-objs

BASE_HEADERS = \
		src/base64.h \
		src/client_connection.h \
		src/compat_endian.h \
		src/http.h \
		src/json.h \
		src/lexer.h \
		src/logging.h \
		src/pubsub_manager.h \
		src/status.h \
		src/string_pool.h \
		src/uri.h \
		src/websocket.h \
		src/xxhash.h
BASE_OBJECTS = \
		base64.o \
		client_connection.o \
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


all: $(BIN_DIR) $(OBJ_DIR) $(TEST_BIN_DIR) $(TEST_OBJ_DIR) $(BINARIES) $(TEST_BINARIES)

clean:
	-rm -rf $(BINARIES)
	-rm -rf $(TEST_BINARIES)
	-rm -rf $(OBJECTS)
	-rm -rf $(TEST_OBJECTS)

wc:
	find . -name '*.c' -or -name '*.h' | grep -v test- | xargs wc -l

$(BIN_DIR):
	mkdir -p $@

$(OBJ_DIR):
	mkdir -p $@

$(TEST_BIN_DIR):
	mkdir -p $@

$(TEST_OBJ_DIR):
	mkdir -p $@

$(OBJECTS): $(BASE_HEADERS)
$(TEST_OBJECTS): $(BASE_HEADERS)

$(OBJ_DIR)/%.o: src/%.c $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TEST_OBJ_DIR)/%.o: src/%.c $(TEST_OBJ_DIR)
	$(CC) $(CFLAGS) $(TEST_CFLAGS) -c -o $@ $<


$(BIN_DIR)/server: $(OBJ_DIR)/server.o $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

$(TEST_BIN_DIR)/test-base64: $(TEST_OBJ_DIR)/test-base64.o $(TEST_OBJECTS)
	$(CC) $(LDFLAGS) $(TEST_LDFLAGS) -o $@ $^

$(TEST_BIN_DIR)/test-http: $(TEST_OBJ_DIR)/test-http.o $(TEST_OBJECTS)
	$(CC) $(LDFLAGS) $(TEST_LDFLAGS) -o $@ $^

$(TEST_BIN_DIR)/test-json: $(TEST_OBJ_DIR)/test-json.o $(TEST_OBJECTS)
	$(CC) $(LDFLAGS) $(TEST_LDFLAGS) -o $@ $^

$(TEST_BIN_DIR)/test-pubsub: $(TEST_OBJ_DIR)/test-pubsub.o $(TEST_OBJECTS)
	$(CC) $(LDFLAGS) $(TEST_LDFLAGS) -o $@ $^
