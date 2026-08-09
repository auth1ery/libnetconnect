# Driver Guide

## Before You Start

Find the datasheet or specification for the device class you want to support. If it is closed-source hardware with no public datasheet, check whether an existing open source driver exists that documents the register layout through clean room notes, since reimplementing from an existing GPL driver directly can create license complications.

## The Shape of a Driver

A driver is a translation layer between nc_platform and the device class contract, for example nc_net_driver. It does three things:

1. **probe**: Given an nc_device with BARs and IRQ already populated by the host, verify vendor and device ID, allocate any DMA buffers or rings the device needs through plat->dma_alloc, and store private state in dev->driver_data.
2. **class operations**: Implement send, recv, or whatever operations the device class contract defines, using only nc_platform primitives to touch hardware.
3. **remove**: Free everything allocated in probe.

## MMIO Access

Never dereference a BAR pointer directly. Always go through plat->mmio_read32 and plat->mmio_write32, even though it looks like unnecessary overhead. This is what lets a host insert bus-specific barriers, or run the driver against a simulated device for testing.

## DMA Buffers

Request DMA memory through plat->dma_alloc, which hands back both a virtual pointer for the driver to use and a physical address for the device to be told about, for example when filling a descriptor ring. Never assume virtual and physical addresses are the same, some hosts will run with an IOMMU or with virtual memory enabled.

## Interrupts vs Polling

Drivers should support being called from an interrupt context through plat->irq_request, but should not assume it is available. If plat->irq_request is null, the driver should still function correctly if the host calls a poll-style entry point instead. Keep any state touched by both paths behind plat->lock_acquire and plat->lock_release.

## Testing Without Real Hardware

QEMU is the easiest way to test network and storage drivers since virtio devices in QEMU behave identically to real virtio silicon. Build the example platform shim in examples/ and run your driver against a QEMU instance before requesting review.
