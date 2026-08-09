#include "virtio_net.h"
#include "nc/platform.h"
#include <string.h>

#define VIRTIO_PCI_VENDOR_ID 0x1AF4
#define VIRTIO_NET_DEVICE_ID 0x1000
#define VIRTIO_NET_DEVICE_ID_MODERN 0x1041

#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

#define VIRTIO_MMIO_QUEUE_NOTIFY 0x50
#define VIRTIO_MMIO_QUEUE_READY 0x44
#define VIRTIO_MMIO_DRIVER_OK 0x104
#define VIRTIO_MMIO_CONFIG 0x100

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
};

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
};

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[];
};

struct virtio_net_priv {
    struct virtq_desc *rx_desc;
    struct virtq_avail *rx_avail;
    struct virtq_used *rx_used;
    struct virtq_desc *tx_desc;
    struct virtq_avail *tx_avail;
    struct virtq_used *tx_used;
    uint16_t queue_size;
    uint16_t tx_avail_idx;
    uint16_t rx_used_idx;
    uint8_t mac[6];
    volatile void *mmio_base;
    void *rx_ring;
    void *tx_ring;
    uint64_t rx_phys;
    uint64_t tx_phys;
    size_t ring_bytes;
};

static int virtio_net_probe(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    if (!plat) return -1;

    if (dev->vendor_id != VIRTIO_PCI_VENDOR_ID) return -1;
    if (dev->device_id != VIRTIO_NET_DEVICE_ID &&
        dev->device_id != VIRTIO_NET_DEVICE_ID_MODERN) return -1;

    struct virtio_net_priv *priv = plat->alloc(sizeof(struct virtio_net_priv));
    if (!priv) return -1;
    memset(priv, 0, sizeof(*priv));

    priv->queue_size = 256;
    priv->tx_avail_idx = 0;
    priv->rx_used_idx = 0;
    priv->mmio_base = dev->bars[0].virt_addr;

    priv->ring_bytes = sizeof(struct virtq_desc) * priv->queue_size
                       + sizeof(struct virtq_avail) + sizeof(uint16_t) * priv->queue_size
                       + sizeof(struct virtq_used) + sizeof(struct virtq_used_elem) * priv->queue_size;

    priv->rx_ring = plat->dma_alloc(priv->ring_bytes, &priv->rx_phys);
    priv->tx_ring = plat->dma_alloc(priv->ring_bytes, &priv->tx_phys);
    if (!priv->rx_ring || !priv->tx_ring) {
        if (priv->rx_ring) plat->dma_free(priv->rx_ring, priv->rx_phys, priv->ring_bytes);
        if (priv->tx_ring) plat->dma_free(priv->tx_ring, priv->tx_phys, priv->ring_bytes);
        plat->free(priv);
        return -1;
    }

    memset(priv->rx_ring, 0, priv->ring_bytes);
    memset(priv->tx_ring, 0, priv->ring_bytes);

    priv->rx_desc = (struct virtq_desc *)priv->rx_ring;
    priv->rx_avail = (struct virtq_avail *)((uint8_t *)priv->rx_ring + sizeof(struct virtq_desc) * priv->queue_size);
    priv->rx_used = (struct virtq_used *)((uint8_t *)priv->rx_avail + sizeof(struct virtq_avail) + sizeof(uint16_t) * priv->queue_size);
    
    priv->tx_desc = (struct virtq_desc *)priv->tx_ring;
    priv->tx_avail = (struct virtq_avail *)((uint8_t *)priv->tx_ring + sizeof(struct virtq_desc) * priv->queue_size);
    priv->tx_used = (struct virtq_used *)((uint8_t *)priv->tx_avail + sizeof(struct virtq_avail) + sizeof(uint16_t) * priv->queue_size);

    /* Read MAC address from device config space */
    for (int i = 0; i < 6; i++) {
        priv->mac[i] = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + VIRTIO_MMIO_CONFIG + i * 4)) & 0xFF;
    }

    dev->driver_data = priv;

    plat->log("virtio-net: probed device, queue size configured\n");
    return 0;
}

static void virtio_net_remove(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    struct virtio_net_priv *priv = dev->driver_data;
    if (!plat || !priv) return;
    
    if (priv->rx_ring) plat->dma_free(priv->rx_ring, priv->rx_phys, priv->ring_bytes);
    if (priv->tx_ring) plat->dma_free(priv->tx_ring, priv->tx_phys, priv->ring_bytes);
    
    plat->free(priv);
    dev->driver_data = NULL;
}

static int virtio_net_send(struct nc_device *dev, const uint8_t *frame, size_t len)
{
    struct virtio_net_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !frame || len == 0 || !plat) return -1;

    uint16_t idx = priv->tx_avail_idx % priv->queue_size;
    
    priv->tx_desc[idx].addr = (uint64_t)(uintptr_t)frame;
    priv->tx_desc[idx].len = (uint32_t)len;
    priv->tx_desc[idx].flags = 0;
    
    priv->tx_avail->ring[idx] = idx;
    priv->tx_avail_idx++;
    priv->tx_avail->idx = priv->tx_avail_idx;
    
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + VIRTIO_MMIO_QUEUE_NOTIFY), 0);
    
    return (int)len;
}

static int virtio_net_recv(struct nc_device *dev, uint8_t *buf, size_t buf_len)
{
    struct virtio_net_priv *priv = dev->driver_data;
    if (!priv || !buf) return -1;

    if (priv->rx_used_idx == priv->rx_used->idx) return 0;
    
    uint16_t idx = priv->rx_used_idx % priv->queue_size;
    struct virtq_used_elem *elem = &priv->rx_used->ring[idx];
    
    if (elem->len > buf_len) return -1;
    
    memcpy(buf, (void *)(uintptr_t)priv->rx_desc[elem->id].addr, elem->len);
    priv->rx_used_idx++;
    
    return (int)elem->len;
}

static void virtio_net_get_mac(struct nc_device *dev, uint8_t mac_out[6])
{
    struct virtio_net_priv *priv = dev->driver_data;
    if (!priv) return;
    memcpy(mac_out, priv->mac, 6);
}

struct nc_net_driver nc_virtio_net_driver = {
    .name = "virtio-net",
    .probe = virtio_net_probe,
    .remove = virtio_net_remove,
    .send = virtio_net_send,
    .recv = virtio_net_recv,
    .get_mac = virtio_net_get_mac,
};
