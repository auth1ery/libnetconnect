# Architecture

## Goals

- One small interface, nc_platform, that any OS or kernel implements to get access to every driver in the tree.
- Drivers never call into libc, never call into a specific kernel, and never assume a threading model. Everything they need comes through nc_platform or is passed in through nc_device.
- Devices are discovered by the host OS, not by libnetconnect. Bus enumeration, PCI/USB/SDIO walking, and resource allocation are the responsibility of the integrating OS. libnetconnect receives an already populated nc_device describing BARs, IRQ line, and IDs.

## Layers

Host OS
  -> Enumerates bus, fills nc_device
  -> Implements nc_platform
  -> Calls driver probe

libnetconnect Core
  -> Registry of drivers
  -> Shared helper code, not hardware specific

Driver
  -> Pure logic against nc_platform and nc_device
  -> No OS-specific code anywhere

## The Platform Interface

nc_platform covers five categories of primitives a driver needs regardless of target:

1. General purpose memory, alloc and free.
2. DMA-safe memory, physically contiguous and mapped for device access.
3. MMIO access, so drivers do not read or write raw pointers directly and the host can insert barriers or tracing.
4. Timing, both a monotonic clock and a blocking sleep.
5. Locking and interrupt registration, kept intentionally minimal.

Expect nc_platform to grow. The version in include/nc/platform.h is a starting point, not the final shape. Any addition needs a real driver that requires it before it gets merged.

## Driver Contract

Every driver exports a struct matching its device class, for example nc_net_driver for anything Ethernet or WiFi shaped. The struct is a plain table of function pointers: probe, remove, and class-specific operations like send and recv for networking. Drivers register themselves through the core registry so a host can enumerate what is available without hardcoding driver names.

## Why virtio-net First

virtio has a public, stable specification, requires no closed firmware blob, and runs identically under QEMU regardless of host OS, which makes it the best first target to validate that the platform interface is sufficient before tackling messier real hardware.
