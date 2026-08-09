# virtio-net Driver References

## Specification

- **VirtIO Specification v1.3**  
  URL: https://docs.oasis-open.org/virtio/virtio/v1.3/virtio-v1.3.html  
  Revision: 1.3  
  Sections: 2.4 (Network Device), 2.6 (Virtqueue Operation)

## Hardware Tested

- **QEMU virtio-net device**  
  Tested against QEMU 8.0+ with virtio-net-pci and virtio-net-mmio transports  
  No physical silicon tested yet - contributions welcome

## Implementation Notes

- Driver implements legacy virtio interface (0x1000) and modern interface (0x1041)
- Queue size fixed at 256 descriptors for simplicity
- MMIO register offsets follow virtio 1.3 specification
- Does not implement multiqueue or indirect descriptors
- MAC address reading not yet implemented (requires config space access)
