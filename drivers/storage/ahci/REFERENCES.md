# AHCI Driver References

## Specification

- **AHCI Specification v1.3**  
  URL: https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/serial-ata-ahci-spec-rev1-3.pdf  
  Revision: 1.3  
  Sections: Register layout, command list, FIS types

- **Linux AHCI driver source**  
  URL: https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/drivers/ata/ahci.c  
  Used for register layout and initialization sequence (GPL v2)

## Hardware Tested

- Generic AHCI-compatible SATA controllers - not yet tested
- Intel ICH series SATA controllers - not yet tested
- AMD SATA controllers - not yet tested

## Implementation Notes

- Driver skeleton only - port initialization not implemented
- Command list and PRD table setup not implemented
- FIS receive buffer setup not implemented
- Read/write operations require command slot management
- Device identify command needed for capacity detection
- Supports up to 32 ports as per AHCI specification
- Common in modern SATA controllers and chipsets
