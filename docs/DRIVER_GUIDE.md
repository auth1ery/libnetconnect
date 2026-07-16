# driver guide

## before you start

find the datasheet or specification for the device class you want to support. if it is closed source hardware with no public datasheet, check whether an existing open source driver exists that documents the register layout through clean room notes, since reimplementing from an existing GPL driver directly can create license complications.

## the shape of a driver

a driver is a translation layer between nc_platform and the device class contract, for example nc_net_driver. it does three things:

1. probe: given an nc_device with bars and irq already populated by the host, verify vendor and device id, allocate any dma buffers or rings the device needs through plat->dma_alloc, and store private state in dev->driver_data.
2. class operations: implement send, recv, or whatever operations the device class contract defines, using only nc_platform primitives to touch hardware.
3. remove: free everything allocated in probe.

## mmio access

never dereference a bar pointer directly. always go through plat->mmio_read32 and plat->mmio_write32, even though it looks like unnecessary overhead. this is what lets a host insert bus specific barriers, or run the driver against a simulated device for testing.

## dma buffers

request dma memory through plat->dma_alloc, which hands back both a virtual pointer for the driver to use and a physical address for the device to be told about, for example when filling a descriptor ring. never assume virtual and physical addresses are the same, some hosts will run with an iommu or with virtual memory enabled.

## interrupts vs polling

drivers should support being called from an interrupt context through plat->irq_request, but should not assume it is available. if plat->irq_request is null, the driver should still function correctly if the host calls a poll style entry point instead. keep any state touched by both paths behind plat->lock_acquire and plat->lock_release.

## testing without real hardware

qemu is the easiest way to test network and storage drivers since virtio devices in qemu behave identically to real virtio silicon. build the example platform shim in examples/ and run your driver against a qemu instance before requesting review.
