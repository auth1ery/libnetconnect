# libnetconnect

A community-maintained, strong set of niche and mainstream WiFi drivers for hobby OSes and more for whatever use case.

## Why?

Writing drivers from scratch is the biggest wall hobby OS developers hit. libnetconnect gives you a shared, reviewed, datasheet-driven pool of drivers that only need a small shim implemented to run on your kernel!

## Structure

- include/nc: The public interface. platform.h and device.h are the only two headers a new OS needs to implement against.
- src/core: Registry and glue code shared by all drivers.
- drivers: Actual hardware drivers, organized by class. drivers/net, drivers/storage, drivers/input as the project grows.
- docs: Architecture notes, driver writing guide, contribution rules.
- examples: Minimal reference platform implementations, for example a Linux userspace shim and a QEMU virtio target.

## Status

Early stage. virtio-net is the first reference driver, chosen because it has no firmware blob and a stable spec, making it the cleanest thing to validate the interface against.

## Getting Involved

See docs/CONTRIBUTING.md and docs/DRIVER_GUIDE.md!
