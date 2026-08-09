# rtl8188 Driver References

## Specification

- **Realtek RTL8188CUS/RTL8188EU Datasheet**  
  URL: https://www.realtek.com/en/products/communications-network-ics/item/rtl8188cus  
  Note: Full datasheet requires NDA, public information from Linux rtl8xxxu driver used

- **Linux rtl8xxxu driver source**  
  URL: https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/drivers/net/wireless/realtek/rtl8xxxu  
  Used for register layout and initialization sequence (GPL v2)

## Hardware Tested

- **Realtek RTL8188CUS** (device ID 0x8179) - not yet tested
- **Realtek RTL8188CE** (device ID 0x8176) - not yet tested
- **Realtek RTL8188ETV** (device ID 0x8188) - not yet tested
- **Realtek RTL8188EU** (device ID 0x8189) - not yet tested

## Implementation Notes

- Driver skeleton only - register initialization and firmware loading not implemented
- Direct register access via MMIO, no firmware required for basic operation
- TX/RX path requires descriptor ring setup and DMA configuration
- Scan/connect operations require register-level command sequences
- Common in USB WiFi dongles and mini-PCIe cards
