# libnetconnect

a community maintained, strong set of niche and mainstream WIFI drivers for hobby OSes and more for whatever use case

## why??

writing drivers from scratch is the biggest wall hobby os developers hit. libnetconnect gives you a shared, reviewed, datasheet driven pool of drivers that only need a small shim implemented to run on your kernel!

## structure

- include/nc: the public interface. platform.h and device.h are the only two headers a new OS needs to implement against.
- src/core: registry and glue code shared by all drivers.
- drivers: actual hardware drivers, organized by class. drivers/net, drivers/storage, drivers/input as the project grows.
- docs: architecture notes, driver writing guide, contribution rules.
- examples: minimal reference platform implementations, for example a linux userspace shim and a qemu virtio target.

## status

early stage. virtio-net is the first reference driver, chosen because it has no firmware blob and a stable spec, making it the cleanest thing to validate the interface against.

## getting involved

see docs/CONTRIBUTING.md and docs/DRIVER_GUIDE.md!
