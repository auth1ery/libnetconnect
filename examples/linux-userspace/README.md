# linux userspace shim

a minimal (still a work in progress) nc_platform implementation for testing drivers on a normal linux machine without touching real hardware!

## what it does

implements every function in nc_platform using standard posix and libc facilities: malloc for general allocation, mmap for dma style buffers, pthread mutexes for locking, clock_gettime for timing. irq_request always fails on purpose, since userspace has no legitimate way to receive a hardware interrupt, which forces any driver tested here to also support a polling path.

## what it is for

- sanity checking that a driver compiles and runs cleanly against the platform interface before testing on qemu or real hardware.
- exercising probe and remove logic, dma allocation paths, and locking, all of which are the same regardless of what device is actually behind them.
- not a substitute for testing against a real or emulated device, since there is no real mmio here, dev.bars are never actually populated with a mapped device.

## build and run

make
./nc_linux_shim

## next step

once a driver is confirmed to build and probe cleanly here, test it against qemu with -device virtio-net-pci and a real mmio mapping through /sys/bus/pci or vfio, which is a more accurate environment and the next milestone for this example...?