# WiFi Driver Guide

## WiFi-Specific Challenges

WiFi drivers are significantly more complex than Ethernet drivers because:

1. **Firmware dependency** - Most WiFi chips require loading proprietary firmware blobs at runtime
2. **Authentication protocols** - WPA2/WPA3 require complex cryptographic operations
3. **State management** - Scanning, connecting, roaming, and power management states
4. **Regulatory domains** - Channel availability and power limits vary by country
5. **Fragmentation** - WiFi frames are smaller than Ethernet, requiring reassembly

## The nc_wifi_driver Interface

The WiFi driver extends the network driver with WiFi-specific operations:

- **scan**: Discover available networks, returning SSID, BSSID, signal strength, security type, and channel
- **connect**: Authenticate and associate with a specific network using SSID and passphrase
- **disconnect**: Deauthenticate from the current network
- **get_status**: Query current connection state and associated SSID

## Firmware Loading

Most WiFi chips (Intel iwlwifi, Atheros ath9k, etc.) require firmware. The nc_platform interface currently does not include firmware loading. This is a known limitation that needs to be addressed before WiFi drivers can be fully functional.

Proposed addition to nc_platform:
```c
int (*firmware_load)(const char *name, void **data, size_t *len);
void (*firmware_free)(void *data);
```

## Cryptographic Requirements

WPA2-PSK and WPA3-SAE require:
- SHA-256 hashing
- HMAC-SHA1 for key derivation
- AES-CCM for encryption
- Diffie-Hellman key exchange for WPA3

These should be provided through nc_platform or a separate crypto library, not implemented in each driver.

## Scan Implementation

Scanning typically involves:
1. Sending a scan command to firmware
2. Waiting for scan complete notification via interrupt
3. Parsing beacon/probe response frames from firmware
4. Building nc_wifi_network structures for each found network

The scan operation may take several seconds and should be interruptible.

## Connect Implementation

Connecting involves:
1. Setting authentication parameters (SSID, passphrase)
2. Triggering firmware authentication sequence
3. Handling EAPOL handshake for WPA2/WPA3
4. Waiting for association confirmation
5. Setting up TX/RX data paths

## Driver State Machine

WiFi drivers should maintain internal state:
- IDLE: Not connected, not scanning
- SCANNING: Actively scanning for networks
- AUTHENTICATING: In progress of authentication
- ASSOCIATED: Connected to a network
- DISCONNECTING: In process of disconnecting

## Testing WiFi Drivers

Without real hardware, WiFi drivers are difficult to test. Options:
1. QEMU with virtio-wifi (limited support)
2. Real hardware testing with userspace shim
3. Firmware simulation for basic protocol testing

## Common Chipsets

### Intel iwlwifi Series
- Requires firmware blob loading
- Firmware command protocol over shared memory
- Complex initialization sequence
- Well-documented via Linux driver source

### Realtek rtl8188/rtl8821 Series
- Some variants work without firmware
- Direct register access for basic operations
- Common in USB WiFi dongles
- Simpler than Intel but less performant

### Atheros ath9k
- Open firmware available (AR9271)
- Direct register access
- Well-documented register layout
- Good choice for open-source OSes

## Security Considerations

- Never hardcode credentials in driver code
- Passphrases should be zeroed after use
- Firmware blobs should be validated if possible
- Handle deauthentication attacks gracefully
