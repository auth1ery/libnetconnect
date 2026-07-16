CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -I../../include -I../../drivers/net/virtio-net
LDFLAGS = -lpthread

SRCS = main.c platform_linux.c ../../src/core/registry.c ../../drivers/net/virtio-net/virtio_net.c
OBJS = $(SRCS:.c=.o)
BIN = nc_linux_shim

all: $(BIN)

$(BIN): $(OBJS)
$(CC) $(OBJS) -o $(BIN) $(LDFLAGS)

%.o: %.c
$(CC) $(CFLAGS) -c $< -o $@

clean:
rm -f $(OBJS) $(BIN)

.PHONY: all clean
