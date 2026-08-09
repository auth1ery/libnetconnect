# e1000 Driver References

## Specification

- **Intel 82540EM/82545EM/82546EM Datasheet**  
  URL: https://www.intel.com/content/www/us/en/docs/programmable/683145/current  
  Revision: Current  
  Sections: Register layout, descriptor formats, EEPROM access

- **Intel e1000 driver source**  
  URL: https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/drivers/net/ethernet/intel/e1000  
  Used for register layout and initialization sequence (GPL v2)

## Hardware Tested

- **Intel 82540EM** (device ID 0x100E) - not yet tested
- **Intel 82545EM** (device ID 0x100F) - not yet tested
- **Intel 82546EM** (device ID 0x1010) - not yet tested
- **Intel 82541GI** (device ID 0x1076) - not yet tested
- **Intel 82547EI** (device ID 0x1019) - not yet tested

## Implementation Notes

- Driver implements basic TX/RX with descriptor rings
- MAC address read from EEPROM via RAL/RAH registers
- No firmware required - direct register access
- TX/RX path uses descriptor rings with DMA
- Interrupt handling not yet implemented
- Link status detection not yet implemented
- Common in QEMU virtualization and older server hardware
