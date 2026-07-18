CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -g -std=c11
CFLAGS += -pthread
CPPFLAGS += -Iinclude
LDLIBS += -lsqlite3 -pthread

SRC := $(wildcard src/*.c src/*/*.c)
OBJ := $(SRC:.c=.o)
BIN := bin/webserver
TEST_PAYLOAD_BIN := tests/test_payload_cache
TEST_CONFIG_BIN := tests/test_web_config
TEST_BINS := $(TEST_PAYLOAD_BIN) $(TEST_CONFIG_BIN)

.PHONY: all clean test run

all: $(BIN)

$(BIN): $(OBJ)
	mkdir -p bin
	$(CC) $(OBJ) -o $@ $(LDFLAGS) $(LDLIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

test: $(TEST_BINS)
	./$(TEST_PAYLOAD_BIN)
	./$(TEST_CONFIG_BIN)

$(TEST_PAYLOAD_BIN): tests/test_payload_cache.c src/ingest/payload_parser.c src/ingest/realtime_cache.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(TEST_CONFIG_BIN): tests/test_web_config.c src/config/web_config.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

run: all
	./$(BIN) -p 8080 -r ./www --db ./sensor_history.db

clean:
	rm -f $(OBJ) $(BIN) $(TEST_BINS) tests/test_payload_cache.exe tests/test_web_config.exe tests/web_config_test.json
