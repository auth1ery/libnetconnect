#include "virtio_net.h"
#include "nc/platform.h"
#include <string.h>

#define VIRTIO_PCI_VENDOR_ID 0x1AF4
#define VIRTIO_NET_DEVICE_ID 0x1000
#define VIRTIO_NET_DEVICE_ID_MODERN 0x1041

#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

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
    uint8_t mac[6];
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

    uint64_t rx_phys, tx_phys;
    size_t ring_bytes = sizeof(struct virtq_desc) * priv->queue_size
                       + sizeof(struct virtq_avail) + sizeof(uint16_t) * priv->queue_size
                       + sizeof(struct virtq_used) + sizeof(struct virtq_used_elem) * priv->queue_size;

    void *rx_ring = plat->dma_alloc(ring_bytes, &rx_phys);
    void *tx_ring = plat->dma_alloc(ring_bytes, &tx_phys);
    if (!rx_ring || !tx_ring) {
        plat->free(priv);
        return -1;
    }

    priv->rx_desc = (struct virtq_desc *)rx_ring;
    priv->tx_desc = (struct virtq_desc *)tx_ring;

    dev->driver_data = priv;

    plat->log("virtio-net: probed device, queue size configured\n");
    return 0;
}

static void virtio_net_remove(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    struct virtio_net_priv *priv = dev->driver_data;
    if (!plat || !priv) return;
    plat->free(priv);
    dev->driver_data = NULL;
}

static int virtio_net_send(struct nc_device *dev, const uint8_t *frame, size_t len)
{
    struct virtio_net_priv *priv = dev->driver_data;
    if (!priv || !frame || len == 0) return -1;
    /* TODO: fill tx descriptor, update avail ring, kick queue via mmio_write32 */
    return (int)len;
}

static int virtio_net_recv(struct nc_device *dev, uint8_t *buf, size_t buf_len)
{
    struct virtio_net_priv *priv = dev->driver_data;
    if (!priv || !buf) return -1;
    /* TODO: check used ring, copy frame into buf */
    (void)buf_len;
    return 0;
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
