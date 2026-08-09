#include "r8169.h"
#include "nc/platform.h"
#include <string.h>

#define REALTEK_VENDOR_ID 0x10EC

/* Supported device IDs for r8169 series */
#define R8169_DEVICE_ID_8169 0x8169
#define R8169_DEVICE_ID_8168 0x8168
#define R8169_DEVICE_ID_8111 0x8111
#define R8169_DEVICE_ID_8411 0x8411
#define R8169_DEVICE_ID_8168EP 0x8168

/* Register offsets */
#define R8169_IDR0 0x00
#define R8169_IDR1 0x04
#define R8169_IDR2 0x08
#define R8169_IDR3 0x0C
#define R8169_TCR 0x40
#define R8169_RCR 0x44
#define R8169_TPPOLL 0xE0
#define R8169_IMR 0xE2
#define R8169_ISR 0xE4
#define R8169_TDESC_ADDR 0x20
#define R8169_RDESC_ADDR 0xE4
#define R8169_TDESC_ADDR_HI 0xE8
#define R8169_RDESC_ADDR_HI 0xEC

#define R8169_RX_DESC_COUNT 256
#define R8169_TX_DESC_COUNT 256

struct r8169_rx_desc {
    uint64_t addr;
    uint32_t len;
    uint32_t opts;
};

struct r8169_tx_desc {
    uint64_t addr;
    uint32_t len;
    uint32_t opts;
};

struct r8169_priv {
    struct r8169_rx_desc *rx_desc;
    struct r8169_tx_desc *tx_desc;
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

static int r8169_probe(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    if (!plat) return -1;

    if (dev->vendor_id != REALTEK_VENDOR_ID) return -1;
    
    /* Check for supported device IDs */
    switch (dev->device_id) {
        case R8169_DEVICE_ID_8169:
        case R8169_DEVICE_ID_8168:
        case R8169_DEVICE_ID_8111:
        case R8169_DEVICE_ID_8411:
        case R8169_DEVICE_ID_8168EP:
            break;
        default:
            return -1;
    }

    struct r8169_priv *priv = plat->alloc(sizeof(struct r8169_priv));
    if (!priv) return -1;
    memset(priv, 0, sizeof(*priv));

    priv->mmio_base = dev->bars[0].virt_addr;
    priv->rx_tail = 0;
    priv->tx_tail = 0;

    /* Allocate RX descriptor ring */
    priv->rx_ring_bytes = sizeof(struct r8169_rx_desc) * R8169_RX_DESC_COUNT;
    priv->rx_ring = plat->dma_alloc(priv->rx_ring_bytes, &priv->rx_phys);
    if (!priv->rx_ring) {
        plat->free(priv);
        return -1;
    }
    memset(priv->rx_ring, 0, priv->rx_ring_bytes);
    priv->rx_desc = (struct r8169_rx_desc *)priv->rx_ring;

    /* Allocate TX descriptor ring */
    priv->tx_ring_bytes = sizeof(struct r8169_tx_desc) * R8169_TX_DESC_COUNT;
    priv->tx_ring = plat->dma_alloc(priv->tx_ring_bytes, &priv->tx_phys);
    if (!priv->tx_ring) {
        plat->dma_free(priv->rx_ring, priv->rx_phys, priv->rx_ring_bytes);
        plat->free(priv);
        return -1;
    }
    memset(priv->tx_ring, 0, priv->tx_ring_bytes);
    priv->tx_desc = (struct r8169_tx_desc *)priv->tx_ring;

    /* Read MAC address from IDR registers */
    uint32_t idr0 = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + R8169_IDR0));
    uint32_t idr1 = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + R8169_IDR1));
    
    priv->mac[0] = idr0 & 0xFF;
    priv->mac[1] = (idr0 >> 8) & 0xFF;
    priv->mac[2] = (idr0 >> 16) & 0xFF;
    priv->mac[3] = idr0 >> 24;
    priv->mac[4] = idr1 & 0xFF;
    priv->mac[5] = (idr1 >> 8) & 0xFF;

    dev->driver_data = priv;

    plat->log("r8169: probed device\n");
    return 0;
}

static void r8169_remove(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    struct r8169_priv *priv = dev->driver_data;
    if (!plat || !priv) return;
    
    if (priv->rx_ring) plat->dma_free(priv->rx_ring, priv->rx_phys, priv->rx_ring_bytes);
    if (priv->tx_ring) plat->dma_free(priv->tx_ring, priv->tx_phys, priv->tx_ring_bytes);
    
    plat->free(priv);
    dev->driver_data = NULL;
}

static int r8169_send(struct nc_device *dev, const uint8_t *frame, size_t len)
{
    struct r8169_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !frame || len == 0 || !plat) return -1;

    uint16_t idx = priv->tx_tail % R8169_TX_DESC_COUNT;
    
    priv->tx_desc[idx].addr = (uint64_t)(uintptr_t)frame;
    priv->tx_desc[idx].len = (uint32_t)len;
    priv->tx_desc[idx].opts = 0x3F0001; /* LS | FS | EOR */
    
    priv->tx_tail++;
    
    /* Trigger TX */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8169_TPPOLL), 0x40);
    
    return (int)len;
}

static int r8169_recv(struct nc_device *dev, uint8_t *buf, size_t buf_len)
{
    struct r8169_priv *priv = dev->driver_data;
    if (!priv || !buf) return -1;

    uint16_t idx = priv->rx_tail % R8169_RX_DESC_COUNT;
    
    if (!(priv->rx_desc[idx].opts & 0x01)) return 0; /* Descriptor not ready */
    
    if (priv->rx_desc[idx].len > buf_len) return -1;
    
    memcpy(buf, (void *)(uintptr_t)priv->rx_desc[idx].addr, priv->rx_desc[idx].len);
    
    priv->rx_desc[idx].opts = 0;
    priv->rx_tail++;
    
    return (int)priv->rx_desc[idx].len;
}

static void r8169_get_mac(struct nc_device *dev, uint8_t mac_out[6])
{
    struct r8169_priv *priv = dev->driver_data;
    if (!priv) return;
    memcpy(mac_out, priv->mac, 6);
}

struct nc_net_driver nc_r8169_driver = {
    .name = "r8169",
    .probe = r8169_probe,
    .remove = r8169_remove,
    .send = r8169_send,
    .recv = r8169_recv,
    .get_mac = r8169_get_mac,
};
