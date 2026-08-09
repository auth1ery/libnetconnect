#include "e1000.h"
#include "nc/platform.h"
#include <string.h>

#define INTEL_VENDOR_ID 0x8086

/* Supported device IDs for e1000 series */
#define E1000_DEVICE_ID_82540EM 0x100E
#define E1000_DEVICE_ID_82545EM 0x100F
#define E1000_DEVICE_ID_82546EM 0x1010
#define E1000_DEVICE_ID_82541GI 0x1076
#define E1000_DEVICE_ID_82547EI 0x1019

/* Register offsets */
#define E1000_CTRL 0x00000
#define E1000_STATUS 0x00008
#define E1000_EECD 0x00010
#define E1000_EERD 0x00014
#define E1000_CTRL_EXT 0x00018
#define E1000_ICR 0x000C0
#define E1000_IMS 0x000D0
#define E1000_RCTL 0x00100
#define E1000_TCTL 0x00400
#define E1000_TDT 0x00380
#define E1000_TDLEN 0x00388
#define E1000_TDBAL 0x00380
#define E1000_TDBAH 0x00384
#define E1000_RDT 0x00180
#define E1000_RDLEN 0x00108
#define E1000_RDBAL 0x00100
#define E1000_RDBAH 0x00104
#define E1000_RAL 0x05400
#define E1000_RAH 0x05404

#define E1000_RX_DESC_COUNT 256
#define E1000_TX_DESC_COUNT 256

struct e1000_rx_desc {
    uint64_t buffer_addr;
    uint16_t length;
    uint16_t csum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
};

struct e1000_tx_desc {
    uint64_t buffer_addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
};

struct e1000_priv {
    struct e1000_rx_desc *rx_desc;
    struct e1000_tx_desc *tx_desc;
    void *rx_ring;
    void *tx_ring;
    uint64_t rx_phys;
    uint64_t tx_phys;
    size_t rx_ring_bytes;
    size_t tx_ring_bytes;
    uint16_t rx_tail;
    uint16_t tx_tail;
    uint8_t mac[6];
    volatile void *mmio_base;
    int initialized;
};

static int e1000_probe(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    if (!plat) return -1;

    if (dev->vendor_id != INTEL_VENDOR_ID) return -1;
    
    /* Check for supported device IDs */
    switch (dev->device_id) {
        case E1000_DEVICE_ID_82540EM:
        case E1000_DEVICE_ID_82545EM:
        case E1000_DEVICE_ID_82546EM:
        case E1000_DEVICE_ID_82541GI:
        case E1000_DEVICE_ID_82547EI:
            break;
        default:
            return -1;
    }

    struct e1000_priv *priv = plat->alloc(sizeof(struct e1000_priv));
    if (!priv) return -1;
    memset(priv, 0, sizeof(*priv));

    priv->mmio_base = dev->bars[0].virt_addr;
    priv->rx_tail = 0;
    priv->tx_tail = 0;

    /* Allocate RX descriptor ring */
    priv->rx_ring_bytes = sizeof(struct e1000_rx_desc) * E1000_RX_DESC_COUNT;
    priv->rx_ring = plat->dma_alloc(priv->rx_ring_bytes, &priv->rx_phys);
    if (!priv->rx_ring) {
        plat->free(priv);
        return -1;
    }
    memset(priv->rx_ring, 0, priv->rx_ring_bytes);
    priv->rx_desc = (struct e1000_rx_desc *)priv->rx_ring;

    /* Allocate TX descriptor ring */
    priv->tx_ring_bytes = sizeof(struct e1000_tx_desc) * E1000_TX_DESC_COUNT;
    priv->tx_ring = plat->dma_alloc(priv->tx_ring_bytes, &priv->tx_phys);
    if (!priv->tx_ring) {
        plat->dma_free(priv->rx_ring, priv->rx_phys, priv->rx_ring_bytes);
        plat->free(priv);
        return -1;
    }
    memset(priv->tx_ring, 0, priv->tx_ring_bytes);
    priv->tx_desc = (struct e1000_tx_desc *)priv->tx_ring;

    /* Read MAC address from EEPROM */
    uint32_t ral = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + E1000_RAL));
    uint32_t rah = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + E1000_RAH));
    
    priv->mac[0] = ral & 0xFF;
    priv->mac[1] = (ral >> 8) & 0xFF;
    priv->mac[2] = (ral >> 16) & 0xFF;
    priv->mac[3] = ral >> 24;
    priv->mac[4] = rah & 0xFF;
    priv->mac[5] = (rah >> 8) & 0xFF;

    dev->driver_data = priv;

    plat->log("e1000: probed device\n");
    return 0;
}

static void e1000_remove(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    struct e1000_priv *priv = dev->driver_data;
    if (!plat || !priv) return;
    
    if (priv->rx_ring) plat->dma_free(priv->rx_ring, priv->rx_phys, priv->rx_ring_bytes);
    if (priv->tx_ring) plat->dma_free(priv->tx_ring, priv->tx_phys, priv->tx_ring_bytes);
    
    plat->free(priv);
    dev->driver_data = NULL;
}

static int e1000_send(struct nc_device *dev, const uint8_t *frame, size_t len)
{
    struct e1000_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !frame || len == 0 || !plat) return -1;

    uint16_t idx = priv->tx_tail % E1000_TX_DESC_COUNT;
    
    priv->tx_desc[idx].buffer_addr = (uint64_t)(uintptr_t)frame;
    priv->tx_desc[idx].length = (uint16_t)len;
    priv->tx_desc[idx].cmd = 0x0B; /* EOP | RS */
    priv->tx_desc[idx].status = 0;
    
    priv->tx_tail++;
    
    /* Write tail pointer */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000_TDT), priv->tx_tail);
    
    return (int)len;
}

static int e1000_recv(struct nc_device *dev, uint8_t *buf, size_t buf_len)
{
    struct e1000_priv *priv = dev->driver_data;
    if (!priv || !buf) return -1;

    uint16_t idx = priv->rx_tail % E1000_RX_DESC_COUNT;
    
    if (!(priv->rx_desc[idx].status & 0x01)) return 0; /* Descriptor not ready */
    
    if (priv->rx_desc[idx].length > buf_len) return -1;
    
    memcpy(buf, (void *)(uintptr_t)priv->rx_desc[idx].buffer_addr, priv->rx_desc[idx].length);
    
    priv->rx_desc[idx].status = 0;
    priv->rx_tail++;
    
    return (int)priv->rx_desc[idx].length;
}

static void e1000_get_mac(struct nc_device *dev, uint8_t mac_out[6])
{
    struct e1000_priv *priv = dev->driver_data;
    if (!priv) return;
    memcpy(mac_out, priv->mac, 6);
}

struct nc_net_driver nc_e1000_driver = {
    .name = "e1000",
    .probe = e1000_probe,
    .remove = e1000_remove,
    .send = e1000_send,
    .recv = e1000_recv,
    .get_mac = e1000_get_mac,
};
