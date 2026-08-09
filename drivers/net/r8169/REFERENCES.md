# r8169 Driver References

## Specification

- **Realtek RTL8169/RTL8111 Datasheet**  
  URL: https://www.realtek.com/en/products/communications-network-ics/item/rtl8111-cg  
  Note: Full datasheet requires NDA, public information from Linux r8169 driver used

- **Linux r8169 driver source**  
  URL: https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/drivers/net/ethernet/realtek/r8169  
  Used for register layout and initialization sequence (GPL v2)

## Hardware Tested

- **Realtek RTL8169** (device ID 0x8169) - not yet tested
- **Realtek RTL8168** (device ID 0x8168) - not yet tested
- **Realtek RTL8111** (device ID 0x8111) - not yet tested
- **Realtek RTL8411** (device ID 0x8411) - not yet tested

## Implementation Notes

- Driver implements basic TX/RX with descriptor rings
- MAC address read from IDR registers
- No firmware required - direct register access
- TX/RX path uses descriptor rings with DMA
- Interrupt handling not yet implemented
- Link status detection not yet implemented
- Common in consumer motherboards and add-in cards
