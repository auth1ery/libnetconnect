CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -I../../include
LDFLAGS = -lpthread

SRCS = main.c platform_linux.c
LIB ?= ../../build/libnetconnect.a
BUILD_DIR ?= ../../build
OBJS = $(SRCS:.c=.o)
BIN = nc_linux_shim

all: $(BUILD_DIR) $(BIN)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BIN): $(OBJS) $(LIB)
	$(CC) $(OBJS) $(LIB) -o $(BIN) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: all clean
