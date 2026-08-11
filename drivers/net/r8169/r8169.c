#include "r8169.h"
#include "nc/platform.h"
#include <string.h>

#define REALTEK_VENDOR_ID 0x10EC

/* Supported device IDs for r8169 series */
#define R8169_DEVICE_ID_8169 0x8169
#define R8169_DEVICE_ID_8168 0x8168
#define R8169_DEVICE_ID_8111 0x8111
#define R8169_DEVICE_ID_8411 0x8411

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
#define R8169_RX_BUFFER_SIZE 2048

/* Transmit descriptor options */
#define R8169_TX_OWN 0x80000000
#define R8169_TX_EOR 0x40000000
#define R8169_TX_FS 0x20000000
#define R8169_TX_LS 0x10000000

/* Receive descriptor options */
#define R8169_RX_OWN 0x80000000
#define R8169_RX_EOR 0x40000000
#define R8169_RX_FS 0x20000000
#define R8169_RX_LS 0x10000000

/* Chip command register */
#define R8169_CMD_RX_ENB 0x0008
#define R8169_CMD_TX_ENB 0x0004
#define R8169_CMD_RST 0x0010

/* Interrupt bits */
#define R8169_ISR_TX_OK 0x0001
#define R8169_ISR_RX_OK 0x0002
#define R8169_ISR_TX_ERR 0x0004
#define R8169_ISR_RX_ERR 0x0008
#define R8169_ISR_RX_OVF 0x0010
#define R8169_ISR_LINK_CHG 0x0020

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
    void *rx_buffers[R8169_RX_DESC_COUNT];
    uint64_t rx_phys;
    uint64_t tx_phys;
    size_t rx_ring_bytes;
    size_t tx_ring_bytes;
    uint16_t rx_tail;
    uint16_t tx_tail;
    uint16_t rx_head;
    uint16_t tx_head;
    uint8_t mac[6];
    volatile void *mmio_base;
    int initialized;
    int irq_num;
    int use_interrupts;
};

static void r8169_irq_handler(void *ctx);

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
            break;
        default:
            return -1;
    }

    struct r8169_priv *priv = plat->alloc(sizeof(struct r8169_priv));
    if (!priv) return -1;
    memset(priv, 0, sizeof(*priv));

    priv->mmio_base = dev->bars[0].virt_addr;
    priv->rx_tail = 0;
    priv->rx_head = 0;
    priv->tx_tail = 0;
    priv->tx_head = 0;
    priv->initialized = 0;

    /* Reset device */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8169_TCR), R8169_CMD_RST);
    plat->sleep_ms(10);

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

    /* Allocate RX buffers */
    for (int i = 0; i < R8169_RX_DESC_COUNT; i++) {
        priv->rx_buffers[i] = plat->dma_alloc(R8169_RX_BUFFER_SIZE, (uint64_t *)&priv->rx_desc[i].addr);
        if (!priv->rx_buffers[i]) {
            for (int j = 0; j < i; j++) {
                plat->dma_free(priv->rx_buffers[j], priv->rx_desc[j].addr, R8169_RX_BUFFER_SIZE);
            }
            plat->dma_free(priv->rx_ring, priv->rx_phys, priv->rx_ring_bytes);
            plat->dma_free(priv->tx_ring, priv->tx_phys, priv->tx_ring_bytes);
            plat->free(priv);
            return -1;
        }
        priv->rx_desc[i].len = R8169_RX_BUFFER_SIZE;
        priv->rx_desc[i].opts = R8169_RX_OWN;
    }

    /* Setup RX descriptor ring */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8169_RDESC_ADDR), priv->rx_phys & 0xFFFFFFFF);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8169_RDESC_ADDR_HI), (priv->rx_phys >> 32) & 0xFFFFFFFF);

    /* Setup TX descriptor ring */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8169_TDESC_ADDR), priv->tx_phys & 0xFFFFFFFF);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8169_TDESC_ADDR_HI), (priv->tx_phys >> 32) & 0xFFFFFFFF);

    /* Enable RX and TX */
    uint32_t cmd = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + R8169_TCR));
    cmd |= R8169_CMD_RX_ENB | R8169_CMD_TX_ENB;
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8169_TCR), cmd);

    /* Read MAC address from IDR registers */
    uint32_t idr0 = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + R8169_IDR0));
    uint32_t idr1 = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + R8169_IDR1));
    
    priv->mac[0] = idr0 & 0xFF;
    priv->mac[1] = (idr0 >> 8) & 0xFF;
    priv->mac[2] = (idr0 >> 16) & 0xFF;
    priv->mac[3] = idr0 >> 24;
    priv->mac[4] = idr1 & 0xFF;
    priv->mac[5] = (idr1 >> 8) & 0xFF;

    /* Try to register interrupt handler */
    priv->irq_num = dev->irq;
    priv->use_interrupts = 0;
    if (priv->irq_num >= 0) {
        int irq_result = plat->irq_request(priv->irq_num, r8169_irq_handler, dev);
        if (irq_result == 0) {
            priv->use_interrupts = 1;
            /* Enable interrupts */
            uint32_t imr_val = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + R8169_IMR));
            imr_val = (imr_val & 0xFFFF0000) | (R8169_ISR_TX_OK | R8169_ISR_RX_OK | R8169_ISR_LINK_CHG);
            plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8169_IMR), imr_val);
            plat->log("r8169: interrupt handler registered for IRQ %d\n", priv->irq_num);
        } else {
            plat->log("r8169: interrupt registration failed, using polling\n");
        }
    } else {
        plat->log("r8169: no IRQ available, using polling\n");
    }

    priv->initialized = 1;
    dev->driver_data = priv;

    plat->log("r8169: probed device\n");
    return 0;
}

static void r8169_irq_handler(void *ctx)
{
    struct nc_device *dev = (struct nc_device *)ctx;
    struct r8169_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !plat) return;

    /* Read interrupt status */
    uint32_t isr = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + R8169_ISR));
    isr &= 0xFFFF;
    
    /* Acknowledge interrupt by writing back */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8169_ISR), isr);
    
    /* Handle RX interrupt */
    if (isr & R8169_ISR_RX_OK) {
        /* RX processing will be done in recv() */
    }
    
    /* Handle TX interrupt */
    if (isr & R8169_ISR_TX_OK) {
        /* TX completion will be handled in next send() */
    }
    
    /* Handle link change interrupt */
    if (isr & R8169_ISR_LINK_CHG) {
        /* Link status changed */
    }
    
    /* Handle error interrupts */
    if (isr & (R8169_ISR_TX_ERR | R8169_ISR_RX_ERR | R8169_ISR_RX_OVF)) {
        /* Error occurred */
    }
}

static void r8169_remove(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    struct r8169_priv *priv = dev->driver_data;
    if (!plat || !priv) return;
    
    /* Disable interrupts */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8169_IMR), 0xFFFFFFFF);
    
    /* Unregister interrupt handler if registered */
    if (priv->use_interrupts && priv->irq_num >= 0) {
        plat->irq_free(priv->irq_num);
    }
    
    /* Disable RX and TX */
    uint32_t cmd = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + R8169_TCR));
    cmd &= ~(R8169_CMD_RX_ENB | R8169_CMD_TX_ENB);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8169_TCR), cmd);
    
    /* Free RX buffers */
    for (int i = 0; i < R8169_RX_DESC_COUNT; i++) {
        if (priv->rx_buffers[i]) {
            plat->dma_free(priv->rx_buffers[i], priv->rx_desc[i].addr, R8169_RX_BUFFER_SIZE);
        }
    }
    
    if (priv->rx_ring) plat->dma_free(priv->rx_ring, priv->rx_phys, priv->rx_ring_bytes);
    if (priv->tx_ring) plat->dma_free(priv->tx_ring, priv->tx_phys, priv->tx_ring_bytes);
    
    plat->free(priv);
    dev->driver_data = NULL;
}

static int r8169_send(struct nc_device *dev, const uint8_t *frame, size_t len)
{
    struct r8169_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !frame || len == 0 || !plat || !priv->initialized) return -1;
    
    /* Security: validate frame length */
    if (len > 9600) return -1; /* Maximum jumbo frame size */

    uint16_t idx = priv->tx_tail % R8169_TX_DESC_COUNT;
    
    /* Check if descriptor is available */
    if (priv->tx_desc[idx].opts & R8169_TX_OWN) return -1;
    
    priv->tx_desc[idx].addr = (uint64_t)(uintptr_t)frame;
    priv->tx_desc[idx].len = (uint32_t)len;
    priv->tx_desc[idx].opts = R8169_TX_FS | R8169_TX_LS | R8169_TX_OWN;
    
    __sync_synchronize();
    
    priv->tx_tail++;
    
    /* Trigger TX */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + R8169_TPPOLL), 0x40);
    
    return (int)len;
}

static int r8169_recv(struct nc_device *dev, uint8_t *buf, size_t buf_len)
{
    struct r8169_priv *priv = dev->driver_data;
    if (!priv || !buf || !priv->initialized) return -1;
    
    /* Security: validate buffer length */
    if (buf_len == 0 || buf_len > 9600) return -1;

    __sync_synchronize();
    
    uint16_t idx = priv->rx_tail % R8169_RX_DESC_COUNT;
    
    /* Check if descriptor is owned by driver (not device) */
    if (priv->rx_desc[idx].opts & R8169_RX_OWN) return 0;
    
    /* Security: bounds check */
    if (priv->rx_desc[idx].len > buf_len || priv->rx_desc[idx].len > 9600) return -1;
    
    memcpy(buf, (void *)(uintptr_t)priv->rx_desc[idx].addr, priv->rx_desc[idx].len);
    
    uint32_t pkt_len = priv->rx_desc[idx].len;
    
    /* Return buffer to device */
    priv->rx_desc[idx].len = R8169_RX_BUFFER_SIZE;
    priv->rx_desc[idx].opts = R8169_RX_OWN;
    priv->rx_tail++;
    
    return (int)pkt_len;
}

static void r8169_get_mac(struct nc_device *dev, uint8_t mac_out[6])
{
    struct r8169_priv *priv = dev->driver_data;
    if (!priv || !mac_out) return;
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
