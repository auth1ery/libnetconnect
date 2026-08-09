# iwlwifi Driver References

## Specification

- **Intel Wireless WiFi Link 22000/9000 Series Technical Documentation**  
  URL: https://www.intel.com/content/www/us/en/wireless-products/wireless-downloads.html  
  Note: Full datasheet requires NDA, public information from Linux iwlwifi driver used

- **Linux iwlwifi driver source**  
  URL: https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git  
  Used for register layout and firmware interface understanding (GPL v2)

## Hardware Tested

- **Intel Wireless-AC 9260** (device ID 0x2526) - not yet tested
- **Intel Wireless 8265** (device ID 0x24fd) - not yet tested
- **Intel Dual Band Wireless-AC 7265** (device ID 0x095a) - not yet tested
- **Intel Wireless 3165** (device ID 0x3165) - not yet tested

## Implementation Notes

- Driver skeleton only - firmware loading and command interface not implemented
- Requires firmware blob loading through nc_platform (not yet added to interface)
- Firmware communication via shared memory and interrupts
- TX/RX path requires firmware command protocol implementation
- Scan/connect operations require firmware authentication and association commands
