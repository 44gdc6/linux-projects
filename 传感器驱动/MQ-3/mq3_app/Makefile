CC ?= gcc
BIN := mq3_test

.PHONY: all clean arm

all: $(BIN)

$(BIN): main.c mq3_app.c mq3_app.h
	$(CC) -o $@ main.c mq3_app.c

arm:
	$(MAKE) CC=arm-linux-gnueabihf-gcc

clean:
	rm -f $(BIN)
