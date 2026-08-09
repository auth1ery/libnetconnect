# ath9k Driver References

## Specification

- **Atheros AR9271 Datasheet**  
  URL: https://www.qualcomm.com/products/atheros-communications  
  Note: Full datasheet requires NDA, public information from Linux ath9k_htc driver used

- **Linux ath9k_htc driver source**  
  URL: https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/drivers/net/wireless/ath/ath9k  
  Used for register layout and firmware interface understanding (GPL v2)

- **Open firmware for AR9271**  
  URL: https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/ath9k_htc  
  Open-source firmware available for AR9271 chipset

## Hardware Tested

- **Atheros AR9271** (device ID 0x002E) - not yet tested
- **Atheros AR9280** (device ID 0x002A) - not yet tested
- **Atheros AR9285** (device ID 0x002B) - not yet tested
- **Atheros AR9287** (device ID 0x002D) - not yet tested

## Implementation Notes

- Driver skeleton only - firmware loading and command interface not implemented
- Requires firmware blob loading through nc_platform (now added to interface)
- Firmware communication via USB or PCIe depending on device variant
- TX/RX path requires firmware command protocol implementation
- Scan/connect operations require firmware authentication and association commands
- AR9271 has open-source firmware, making it ideal for open-source OSes
