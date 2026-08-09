# Linux Userspace Shim

A minimal (still a work in progress) nc_platform implementation for testing drivers on a normal Linux machine without touching real hardware!

## What It Does

Implements every function in nc_platform using standard POSIX and libc facilities: malloc for general allocation, mmap for DMA-style buffers, pthread mutexes for locking, clock_gettime for timing. irq_request always fails on purpose, since userspace has no legitimate way to receive a hardware interrupt, which forces any driver tested here to also support a polling path.

## What It Is For

- Sanity checking that a driver compiles and runs cleanly against the platform interface before testing on QEMU or real hardware.
- Exercising probe and remove logic, DMA allocation paths, and locking, all of which are the same regardless of what device is actually behind them.
- Not a substitute for testing against a real or emulated device, since there is no real MMIO here, dev.bars are never actually populated with a mapped device.

## Build and Run

```bash
make
./nc_linux_shim
```

## Next Step

Once a driver is confirmed to build and probe cleanly here, test it against QEMU with -device virtio-net-pci and a real MMIO mapping through /sys/bus/pci or VFIO, which is a more accurate environment and the next milestone for this example.