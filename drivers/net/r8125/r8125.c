#include "r8125.h"
#include "nc/platform.h"
#include <string.h>

#define REALTEK_VENDOR_ID 0x10EC

/* r8125 device IDs */
#define R8125_DEVICE_ID_8125 0x8125
#define R8125_DEVICE_ID_8125B 0x8125

/* r8125 registers */
#define R8125_MAC 0x0000
#define R8125_MAR0 0x0008
#define R8125_MAR4 0x000C
#define R8125_DTCCR 0x0018
#define R8125_FIFOR 0x001C
#define R8125_TCR 0x0040
#define R8125_TPPOLL 0x0050
#define R8125_IMR 0x0058
#define R8125_ISR 0x005C
#define R8125_TCR0 0x0060
#define R8125_TCR1 0x0064
#define R8125_RCR 0x0044
#define R8125_RCR_AM 0x00000004
#define R8125_RCR_APM 0x00000008
#define R8125_RCR_AAP 0x00000001
#define R8125_RCR_AB 0x00000002
#define R8125_RCR_ACRC32 0x02000000
#define R8125_RCR_AICV 0x40000000
#define R8125_TCR_TXQEN 0x00000004
#define R8125_TCR_IFG2 0x00000060
#define R8125_TCR_IFG1 0x00000010
#define R8125_TCR_MXDMA_2048 0x00000700
#define R8125_TX_DESC_ADDR 0x0020
#define R8125_RX_DESC_ADDR 0x0024
#define R8125_EEPROM 0x0050
#define R8125_CMD 0x0037
#define R8125_IDR0 0x6300
#define R8125_IDR1 0x6304
#define R8125_IDR2 0x6308
#define R8125_IDR3 0x630C
#define R8125_IDR4 0x6310
#define R8125_IDR5 0x6314
#define R8125_TX_CONFIG 0x0040
#define R8125_RX_CONFIG 0x0044
#define R8125_RX_MAX_LENGTH 0x003C

#define R8125_TX_QUEUE_SIZE 256
#define R8125_RX_QUEUE_SIZE 256
#define R8125_RX_BUFFER_SIZE 2048

/* TX descriptor flags */
#define R8125_TX_OWN 0x80000000
#define R8125_TX_FS 0x20000000
#define R8125_TX_LS 0x10000000
#define R8125_TX_LGSEN 0x08000000

/* RX descriptor flags */
#define R8125_RX_OWN 0x80000000
#define R8125_RX_EOR 0x40000000
#define R8125_RX_RES 0x00200000

/* Interrupt bits */
#define R8125_ISR_TX_OK 0x0001
#define R8125_ISR_RX_OK 0x0002
#define R8125_ISR_TX_ERR 0x0004
#define R8125_ISR_RX_ERR 0x0008
#define R8125_ISR_RX_OVF 0x0010
#define R8125_ISR_LINK_CHG 0x0020

struct r8125_tx_desc {
    uint64_t addr;
    uint32_t len;
    uint32_t opts;
};

struct r8125_rx_desc {
    uint64_t addr;
    uint32_t len;
    uint32_t opts;
};

struct r8125_priv {
    struct r8125_tx_desc *tx_desc;
    struct r8125_rx_desc *rx_desc;
    void *tx_ring;
    void *rx_ring;
    void *rx_buffers[R8125_RX_QUEUE_SIZE];
    uint64_t tx_ring_phys;
    uint64_t rx_ring_phys;
    uint16_t tx_head;
    uint16_t tx_tail;
    uint16_t rx_head;
    uint16_t rx_tail;
    uint8_t mac[6];
    volatile void *mmio_base;
    int initialized;
    int irq_num;
    int use_interrupts;
};

static void r8125_irq_handler(void *ctx);

static int r8125_probe(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    if (!plat) return -1;

    if (dev->vendor_id != REALTEK_VENDOR_ID) return -1;
    
    /* Check for supported device IDs */
    switch (dev->device_id) {
        case R8125_DEVICE_ID_8125:
            break;
        default:
            return -1;
    }

    struct r8125_priv *priv = plat->alloc(sizeof(struct r8125_priv));
    if (!priv) return -1;
    memset(priv, 0, sizeof(*priv));

    priv->mmio_base = dev->bars[0].virt_addr;
    priv->tx_head = 0;
    priv->tx_tail = 0;
    priv->rx_head = 0;
    priv->rx_tail = 0;
    priv->initialized = 0;

    /* Reset device */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8125_CMD), 0x10);
    plat->sleep_ms(10);

    /* Setup TX descriptor ring */
    size_t tx_ring_size = sizeof(struct r8125_tx_desc) * R8125_TX_QUEUE_SIZE;
    priv->tx_ring = plat->dma_alloc(tx_ring_size, &priv->tx_ring_phys);
    if (!priv->tx_ring) {
        plat->free(priv);
        return -1;
    }
    memset(priv->tx_ring, 0, tx_ring_size);
    priv->tx_desc = (struct r8125_tx_desc *)priv->tx_ring;

    /* Setup RX descriptor ring */
    size_t rx_ring_size = sizeof(struct r8125_rx_desc) * R8125_RX_QUEUE_SIZE;
    priv->rx_ring = plat->dma_alloc(rx_ring_size, &priv->rx_ring_phys);
    if (!priv->rx_ring) {
        plat->dma_free(priv->tx_ring, priv->tx_ring_phys, tx_ring_size);
        plat->free(priv);
        return -1;
    }
    memset(priv->rx_ring, 0, rx_ring_size);
    priv->rx_desc = (struct r8125_rx_desc *)priv->rx_ring;

    /* Allocate RX buffers */
    for (int i = 0; i < R8125_RX_QUEUE_SIZE; i++) {
        priv->rx_buffers[i] = plat->dma_alloc(R8125_RX_BUFFER_SIZE, (uint64_t *)&priv->rx_desc[i].addr);
        if (!priv->rx_buffers[i]) {
            for (int j = 0; j < i; j++) {
                plat->dma_free(priv->rx_buffers[j], priv->rx_desc[j].addr, R8125_RX_BUFFER_SIZE);
            }
            plat->dma_free(priv->rx_ring, priv->rx_ring_phys, rx_ring_size);
            plat->dma_free(priv->tx_ring, priv->tx_ring_phys, tx_ring_size);
            plat->free(priv);
            return -1;
        }
        priv->rx_desc[i].len = R8125_RX_BUFFER_SIZE;
        priv->rx_desc[i].opts = R8125_RX_OWN;
    }

    /* Setup descriptor addresses */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8125_TX_DESC_ADDR), priv->tx_ring_phys);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8125_RX_DESC_ADDR), priv->rx_ring_phys);

    /* Configure RX */
    uint32_t rcr = R8125_RCR_AB | R8125_RCR_AM | R8125_RCR_APM | R8125_RCR_AAP | R8125_RCR_ACRC32 | R8125_RCR_AICV;
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8125_RX_CONFIG), rcr);

    /* Configure TX */
    uint32_t tcr = R8125_TCR_TXQEN | R8125_TCR_IFG2 | R8125_TCR_IFG1 | R8125_TCR_MXDMA_2048;
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8125_TX_CONFIG), tcr);

    /* Read MAC address from IDR registers */
    priv->mac[0] = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + R8125_IDR0)) & 0xFF;
    priv->mac[1] = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + R8125_IDR1)) & 0xFF;
    priv->mac[2] = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + R8125_IDR2)) & 0xFF;
    priv->mac[3] = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + R8125_IDR3)) & 0xFF;
    priv->mac[4] = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + R8125_IDR4)) & 0xFF;
    priv->mac[5] = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + R8125_IDR5)) & 0xFF;

    /* Try to register interrupt handler */
    priv->irq_num = dev->irq;
    priv->use_interrupts = 0;
    if (priv->irq_num >= 0) {
        int irq_result = plat->irq_request(priv->irq_num, r8125_irq_handler, dev);
        if (irq_result == 0) {
            priv->use_interrupts = 1;
            /* Enable interrupts */
            uint32_t imr_val = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + R8125_IMR));
            imr_val = (imr_val & 0xFFFF0000) | (R8125_ISR_TX_OK | R8125_ISR_RX_OK | R8125_ISR_LINK_CHG);
            plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8125_IMR), imr_val);
            plat->log("r8125: interrupt handler registered for IRQ %d\n", priv->irq_num);
        } else {
            plat->log("r8125: interrupt registration failed, using polling\n");
        }
    } else {
        plat->log("r8125: no IRQ available, using polling\n");
    }

    priv->initialized = 1;
    dev->driver_data = priv;

    plat->log("r8125: probed device\n");
    return 0;
}

static void r8125_irq_handler(void *ctx)
{
    struct nc_device *dev = (struct nc_device *)ctx;
    struct r8125_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !plat) return;

    /* Read interrupt status */
    uint32_t isr = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + R8125_ISR));
    isr &= 0xFFFF;
    
    /* Acknowledge interrupt by writing back */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8125_ISR), isr);
    
    /* Handle RX interrupt */
    if (isr & R8125_ISR_RX_OK) {
        /* RX processing will be done in recv() */
    }
    
    /* Handle TX interrupt */
    if (isr & R8125_ISR_TX_OK) {
        /* TX completion will be handled in next send() */
    }
    
    /* Handle error interrupts */
    if (isr & (R8125_ISR_TX_ERR | R8125_ISR_RX_ERR)) {
        /* Error occurred */
    }
    
    /* Handle link status change */
    if (isr & R8125_ISR_LINK_CHG) {
        /* Link status changed */
    }
}

static void r8125_remove(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    struct r8125_priv *priv = dev->driver_data;
    if (!plat || !priv) return;

    /* Disable interrupts */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8125_IMR), 0xFFFFFFFF);
    
    /* Unregister interrupt handler if registered */
    if (priv->use_interrupts && priv->irq_num >= 0) {
        plat->irq_free(priv->irq_num);
    }

    /* Disable RX and TX */
    uint32_t tcr = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + R8125_TX_CONFIG));
    tcr &= ~R8125_TCR_TXQEN;
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8125_TX_CONFIG), tcr);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8125_RX_CONFIG), 0);

    /* Free RX buffers */
    for (int i = 0; i < R8125_RX_QUEUE_SIZE; i++) {
        if (priv->rx_buffers[i]) {
            plat->dma_free(priv->rx_buffers[i], priv->rx_desc[i].addr, R8125_RX_BUFFER_SIZE);
        }
    }

    if (priv->tx_ring) plat->dma_free(priv->tx_ring, priv->tx_ring_phys, sizeof(struct r8125_tx_desc) * R8125_TX_QUEUE_SIZE);
    if (priv->rx_ring) plat->dma_free(priv->rx_ring, priv->rx_ring_phys, sizeof(struct r8125_rx_desc) * R8125_RX_QUEUE_SIZE);

    plat->free(priv);
    dev->driver_data = NULL;
}

static int r8125_send(struct nc_device *dev, const uint8_t *frame, size_t len)
{
    struct r8125_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !frame || len == 0 || !plat || !priv->initialized) return -1;
    
    /* Security: validate frame length */
    if (len > 9600) return -1;

    uint16_t idx = priv->tx_tail % R8125_TX_QUEUE_SIZE;
    
    /* Check if descriptor is available */
    if (priv->tx_desc[idx].opts & R8125_TX_OWN) return -1;
    
    priv->tx_desc[idx].addr = (uint64_t)(uintptr_t)frame;
    priv->tx_desc[idx].len = (uint32_t)len;
    priv->tx_desc[idx].opts = R8125_TX_FS | R8125_TX_LS | R8125_TX_OWN;
    
    __sync_synchronize();
    
    priv->tx_tail++;
    
    /* Trigger TX */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8125_TPPOLL), 0x40);
    
    return (int)len;
}

static int r8125_recv(struct nc_device *dev, uint8_t *buf, size_t buf_len)
{
    struct r8125_priv *priv = dev->driver_data;
    if (!priv || !buf || !priv->initialized) return -1;
    
    /* Security: validate buffer length */
    if (buf_len == 0 || buf_len > 9600) return -1;

    __sync_synchronize();
    
    uint16_t idx = priv->rx_tail % R8125_RX_QUEUE_SIZE;
    
    /* Check if descriptor is owned by driver (not device) */
    if (priv->rx_desc[idx].opts & R8125_RX_OWN) return 0;
    
    /* Security: bounds check */
    if (priv->rx_desc[idx].len > buf_len || priv->rx_desc[idx].len > 9600) return -1;
    
    memcpy(buf, (void *)(uintptr_t)priv->rx_desc[idx].addr, priv->rx_desc[idx].len);
    
    uint32_t pkt_len = priv->rx_desc[idx].len;
    
    /* Return buffer to device */
    priv->rx_desc[idx].len = R8125_RX_BUFFER_SIZE;
    priv->rx_desc[idx].opts = R8125_RX_OWN;
    priv->rx_tail++;
    
    return (int)pkt_len;
}

static void r8125_get_mac(struct nc_device *dev, uint8_t mac_out[6])
{
    struct r8125_priv *priv = dev->driver_data;
    if (!priv || !mac_out) return;
    memcpy(mac_out, priv->mac, 6);
}

struct nc_net_driver nc_r8125_driver = {
    .name = "r8125",
    .probe = r8125_probe,
    .remove = r8125_remove,
    .send = r8125_send,
    .recv = r8125_recv,
    .get_mac = r8125_get_mac,
};
