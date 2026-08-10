#include "virtio_net.h"
#include "nc/platform.h"
#include <string.h>

#define VIRTIO_PCI_VENDOR_ID 0x1AF4
#define VIRTIO_NET_DEVICE_ID 0x1000
#define VIRTIO_NET_DEVICE_ID_MODERN 0x1041

#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

#define VIRTIO_MMIO_MAGIC_VALUE 0x000
#define VIRTIO_MMIO_VERSION 0x004
#define VIRTIO_MMIO_DEVICE_ID 0x008
#define VIRTIO_MMIO_VENDOR_ID 0x00C
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DRIVER_FEATURES 0x014
#define VIRTIO_MMIO_QUEUE_SEL 0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_MMIO_QUEUE_NUM 0x038
#define VIRTIO_MMIO_QUEUE_READY 0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK 0x064
#define VIRTIO_MMIO_STATUS 0x070
#define VIRTIO_MMIO_CONFIG 0x100

#define VIRTIO_INT_RX 0x01
#define VIRTIO_INT_TX 0x02

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8

#define VIRTIO_F_RING_EVENT_IDX 29

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

#define VIRTIO_NET_RX_BUFSIZE 2048

static void virtio_net_irq_handler(void *ctx);

struct virtio_net_priv {
    struct virtq_desc *rx_desc;
    struct virtq_avail *rx_avail;
    struct virtq_used *rx_used;
    struct virtq_desc *tx_desc;
    struct virtq_avail *tx_avail;
    struct virtq_used *tx_used;
    uint16_t queue_size;
    uint16_t tx_avail_idx;
    uint16_t rx_avail_idx;
    uint16_t rx_used_idx;
    uint8_t mac[6];
    volatile void *mmio_base;
    void *rx_ring;
    void *tx_ring;
    void *rx_buffers[VIRTIO_NET_RX_BUFSIZE];
    uint64_t rx_phys;
    uint64_t tx_phys;
    size_t ring_bytes;
    int initialized;
    int irq_num;
    int use_interrupts;
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
    priv->rx_avail_idx = 0;
    priv->rx_used_idx = 0;
    priv->mmio_base = dev->bars[0].virt_addr;
    priv->initialized = 0;

    /* Reset device */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + VIRTIO_MMIO_STATUS), 0);

    /* Acknowledge device */
    uint32_t status = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + VIRTIO_MMIO_STATUS));
    status |= VIRTIO_STATUS_ACKNOWLEDGE;
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + VIRTIO_MMIO_STATUS), status);

    /* Set driver flag */
    status |= VIRTIO_STATUS_DRIVER;
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + VIRTIO_MMIO_STATUS), status);

    /* Read queue size */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + VIRTIO_MMIO_QUEUE_SEL), 0);
    uint32_t max_queue = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + VIRTIO_MMIO_QUEUE_NUM_MAX));
    if (max_queue < priv->queue_size) priv->queue_size = max_queue;

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

    /* Setup RX queue */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + VIRTIO_MMIO_QUEUE_SEL), 0);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + VIRTIO_MMIO_QUEUE_NUM), priv->queue_size);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + VIRTIO_MMIO_QUEUE_READY), 1);

    /* Allocate RX buffers */
    for (int i = 0; i < priv->queue_size; i++) {
        priv->rx_buffers[i] = plat->dma_alloc(2048, (uint64_t *)&priv->rx_desc[i].addr);
        if (!priv->rx_buffers[i]) {
            for (int j = 0; j < i; j++) {
                plat->dma_free(priv->rx_buffers[j], priv->rx_desc[j].addr, 2048);
            }
            plat->dma_free(priv->rx_ring, priv->rx_phys, priv->ring_bytes);
            plat->dma_free(priv->tx_ring, priv->tx_phys, priv->ring_bytes);
            plat->free(priv);
            return -1;
        }
        priv->rx_desc[i].len = 2048;
        priv->rx_desc[i].flags = VIRTQ_DESC_F_WRITE;
        priv->rx_avail->ring[i] = i;
    }
    priv->rx_avail->idx = priv->queue_size;

    /* Read MAC address from device config space */
    for (int i = 0; i < 6; i++) {
        priv->mac[i] = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + VIRTIO_MMIO_CONFIG + i * 4)) & 0xFF;
    }

    /* Set driver OK */
    status |= VIRTIO_STATUS_DRIVER_OK;
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + VIRTIO_MMIO_STATUS), status);

    /* Try to register interrupt handler */
    priv->irq_num = dev->irq;
    priv->use_interrupts = 0;
    if (priv->irq_num >= 0) {
        int irq_result = plat->irq_request(priv->irq_num, virtio_net_irq_handler, dev);
        if (irq_result == 0) {
            priv->use_interrupts = 1;
            plat->log("virtio-net: interrupt handler registered for IRQ %d\n", priv->irq_num);
        } else {
            plat->log("virtio-net: interrupt registration failed, using polling\n");
        }
    } else {
        plat->log("virtio-net: no IRQ available, using polling\n");
    }

    priv->initialized = 1;
    dev->driver_data = priv;

    plat->log("virtio-net: probed device, queue size configured\n");
    return 0;
}

static void virtio_net_irq_handler(void *ctx)
{
    struct nc_device *dev = (struct nc_device *)ctx;
    struct virtio_net_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !plat) return;

    /* Read interrupt status */
    uint32_t status = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + VIRTIO_MMIO_INTERRUPT_STATUS));
    
    /* Acknowledge interrupt */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + VIRTIO_MMIO_INTERRUPT_ACK), status);
    
    /* Handle RX interrupt */
    if (status & VIRTIO_INT_RX) {
        /* RX processing will be done in recv() */
    }
    
    /* Handle TX interrupt */
    if (status & VIRTIO_INT_TX) {
        /* TX completion will be handled in next send() */
    }
}

static void virtio_net_remove(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    struct virtio_net_priv *priv = dev->driver_data;
    if (!plat || !priv) return;
    
    /* Unregister interrupt handler if registered */
    if (priv->use_interrupts && priv->irq_num >= 0) {
        plat->irq_free(priv->irq_num);
    }
    
    /* Free RX buffers */
    for (int i = 0; i < priv->queue_size; i++) {
        if (priv->rx_buffers[i]) {
            plat->dma_free(priv->rx_buffers[i], priv->rx_desc[i].addr, 2048);
        }
    }
    
    if (priv->rx_ring) plat->dma_free(priv->rx_ring, priv->rx_phys, priv->ring_bytes);
    if (priv->tx_ring) plat->dma_free(priv->tx_ring, priv->tx_phys, priv->ring_bytes);
    
    plat->free(priv);
    dev->driver_data = NULL;
}

static int virtio_net_send(struct nc_device *dev, const uint8_t *frame, size_t len)
{
    struct virtio_net_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !frame || len == 0 || !plat || !priv->initialized) return -1;
    
    /* Security: validate frame length */
    if (len > 65535) return -1;

    uint16_t idx = priv->tx_avail_idx % priv->queue_size;
    
    /* Check if descriptor is available */
    if ((priv->tx_avail_idx - priv->rx_used_idx) >= priv->queue_size) return -1;
    
    priv->tx_desc[idx].addr = (uint64_t)(uintptr_t)frame;
    priv->tx_desc[idx].len = (uint32_t)len;
    priv->tx_desc[idx].flags = 0;
    
    priv->tx_avail->ring[idx] = idx;
    priv->tx_avail_idx++;
    
    /* Memory barrier to ensure descriptor is written before notifying */
    __sync_synchronize();
    
    priv->tx_avail->idx = priv->tx_avail_idx;
    
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + VIRTIO_MMIO_QUEUE_NOTIFY), 0);
    
    return (int)len;
}

static int virtio_net_recv(struct nc_device *dev, uint8_t *buf, size_t buf_len)
{
    struct virtio_net_priv *priv = dev->driver_data;
    if (!priv || !buf || !priv->initialized) return -1;
    
    /* Security: validate buffer length */
    if (buf_len == 0 || buf_len > 65535) return -1;

    /* Memory barrier to ensure we read the latest idx */
    __sync_synchronize();
    
    if (priv->rx_used_idx == priv->rx_used->idx) return 0;
    
    uint16_t idx = priv->rx_used_idx % priv->queue_size;
    struct virtq_used_elem *elem = &priv->rx_used->ring[idx];
    
    /* Security: bounds check */
    if (elem->len > buf_len || elem->len > 65535) return -1;
    
    memcpy(buf, (void *)(uintptr_t)priv->rx_desc[elem->id].addr, elem->len);
    priv->rx_used_idx++;
    
    /* Return buffer to device */
    uint16_t avail_idx = priv->rx_avail_idx % priv->queue_size;
    priv->rx_avail->ring[avail_idx] = elem->id;
    priv->rx_avail_idx++;
    priv->rx_avail->idx = priv->rx_avail_idx;
    
    return (int)elem->len;
}

static void virtio_net_get_mac(struct nc_device *dev, uint8_t mac_out[6])
{
    struct virtio_net_priv *priv = dev->driver_data;
    if (!priv || !mac_out) return;
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
