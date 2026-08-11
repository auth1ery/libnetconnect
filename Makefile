# libnetconnect Makefile

CC ?= gcc
AR ?= ar
CFLAGS ?= -Wall -Wextra -O2 -g -Iinclude
LDFLAGS ?=
ARFLAGS ?= rcs

# Directories
SRC_DIR = src/core
DRIVERS_DIR = drivers
BUILD_DIR = build
INCLUDE_DIR = include

# Library source files
LIB_SRCS = $(SRC_DIR)/registry.c
LIB_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(LIB_SRCS))

# Driver source files
VIRTIO_NET_SRCS = $(DRIVERS_DIR)/net/virtio-net/virtio_net.c
VIRTIO_NET_OBJS = $(BUILD_DIR)/virtio_net.o

E1000_SRCS = $(DRIVERS_DIR)/net/e1000/e1000.c
E1000_OBJS = $(BUILD_DIR)/e1000.o

E1000E_SRCS = $(DRIVERS_DIR)/net/e1000e/e1000e.c
E1000E_OBJS = $(BUILD_DIR)/e1000e.o

R8169_SRCS = $(DRIVERS_DIR)/net/r8169/r8169.c
R8169_OBJS = $(BUILD_DIR)/r8169.o

R8125_SRCS = $(DRIVERS_DIR)/net/r8125/r8125.c
R8125_OBJS = $(BUILD_DIR)/r8125.o

VMXNET3_SRCS = $(DRIVERS_DIR)/net/vmxnet3/vmxnet3.c
VMXNET3_OBJS = $(BUILD_DIR)/vmxnet3.o

IWLWIFI_SRCS = $(DRIVERS_DIR)/net/wifi/iwlwifi/iwlwifi.c
IWLWIFI_OBJS = $(BUILD_DIR)/iwlwifi.o

ATH9K_SRCS = $(DRIVERS_DIR)/net/wifi/ath9k/ath9k.c
ATH9K_OBJS = $(BUILD_DIR)/ath9k.o

RTL8188_SRCS = $(DRIVERS_DIR)/net/wifi/rtl8188/rtl8188.c
RTL8188_OBJS = $(BUILD_DIR)/rtl8188.o

AHCI_SRCS = $(DRIVERS_DIR)/storage/ahci/ahci.c
AHCI_OBJS = $(BUILD_DIR)/ahci.o

NVME_SRCS = $(DRIVERS_DIR)/storage/nvme/nvme.c
NVME_OBJS = $(BUILD_DIR)/nvme.o

# All object files
ALL_OBJS = $(LIB_OBJS) $(VIRTIO_NET_OBJS) $(E1000_OBJS) $(E1000E_OBJS) $(R8169_OBJS) $(R8125_OBJS) $(VMXNET3_OBJS) $(IWLWIFI_OBJS) $(ATH9K_OBJS) $(RTL8188_OBJS) $(AHCI_OBJS) $(NVME_OBJS)

# Library output
LIB = $(BUILD_DIR)/libnetconnect.a

# Example
EXAMPLE_DIR = examples/linux-userspace
EXAMPLE_BIN = $(BUILD_DIR)/linux-userspace-example

.PHONY: all clean dirs example

all: dirs $(LIB)

dirs:
	@mkdir -p $(BUILD_DIR)

$(LIB): $(ALL_OBJS)
	$(AR) $(ARFLAGS) $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/virtio_net.o: $(VIRTIO_NET_SRCS)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/e1000.o: $(E1000_SRCS)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/e1000e.o: $(E1000E_SRCS)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/r8169.o: $(R8169_SRCS)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/r8125.o: $(R8125_SRCS)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vmxnet3.o: $(VMXNET3_SRCS)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/iwlwifi.o: $(IWLWIFI_SRCS)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ath9k.o: $(ATH9K_SRCS)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/rtl8188.o: $(RTL8188_SRCS)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ahci.o: $(AHCI_SRCS)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/nvme.o: $(NVME_SRCS)
	$(CC) $(CFLAGS) -c $< -o $@

example: $(LIB)
	$(MAKE) -C $(EXAMPLE_DIR) BUILD_DIR=$(BUILD_DIR) LIB=$(LIB)

clean:
	rm -rf $(BUILD_DIR)
	$(MAKE) -C $(EXAMPLE_DIR) clean
