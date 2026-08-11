#include "vmxnet3.h"
#include "nc/platform.h"
#include <string.h>

#define VMWARE_VENDOR_ID 0x15AD

/* VMXNET3 device IDs */
#define VMXNET3_DEVICE_ID 0x07B0

/* VMXNET3 registers */
#define VMXNET3_REG_VERSION 0x0000
#define VMXNET3_REG_VRRS 0x0008
#define VMXNET3_REG_UVRS 0x000C
#define VMXNET3_REG_DSAL 0x0010
#define VMXNET3_REG_DSAH 0x0014
#define VMXNET3_REG_CMD 0x0018
#define VMXNET3_REG_MACL 0x001C
#define VMXNET3_REG_MACH 0x0020
#define VMXNET3_REG_INTR 0x0024
#define VMXNET3_REG_EVENT 0x0028

/* VMXNET3 commands */
#define VMXNET3_CMD_ENABLE 0x00000001
#define VMXNET3_CMD_DISABLE 0x00000002
#define VMXNET3_CMD_RESET 0x00000003
#define VMXNET3_CMD_SET_RXMODE 0x00000006
#define VMXNET3_CMD_SET_FILTER 0x00000007
#define VMXNET3_CMD_SET_RSS 0x00000008
#define VMXNET3_CMD_ACTIVATE_DEV 0x0000000F

/* VMXNET3 interrupt bits */
#define VMXNET3_INTR_TYPE_INTX 0x00000001
#define VMXNET3_INTR_TYPE_MSI 0x00000002
#define VMXNET3_INTR_TYPE_MSIX 0x00000004
#define VMXNET3_INTR_MASK 0x00000008
#define VMXNET3_INTR_SHIFT 0x00000010

/* VMXNET3 RX modes */
#define VMXNET3_RXMODE_UCAST 0x00000001
#define VMXNET3_RXMODE_MCAST 0x00000002
#define VMXNET3_RXMODE_BCAST 0x00000004
#define VMXNET3_RXMODE_ALLMULTI 0x00000008
#define VMXNET3_RXMODE_PROMISC 0x00000010

/* VMXNET3 shared memory layout */
#define VMXNET3_TX_QUEUE_SIZE 256
#define VMXNET3_RX_QUEUE_SIZE 256
#define VMXNET3_RX_BUFFER_SIZE 2048

struct vmxnet3_tx_desc {
    uint64_t addr;
    uint32_t len;
    uint32_t gen;
    uint32_t cmd;
    uint32_t rsvd;
};

struct vmxnet3_rx_desc {
    uint64_t addr;
    uint32_t len;
    uint32_t btype;
    uint32_t gen;
    uint32_t rsvd;
};

struct vmxnet3_comp {
    uint32_t status;
    uint32_t error;
    uint32_t num_pkts;
    uint32_t rsvd;
};

struct vmxnet3_queue_conf {
    uint64_t tx_desc_phys;
    uint64_t tx_comp_phys;
    uint64_t rx_desc_phys;
    uint64_t rx_comp_phys;
    uint32_t tx_desc_size;
    uint32_t tx_comp_size;
    uint32_t rx_desc_size;
    uint32_t rx_comp_size;
    uint32_t tx_num_desc;
    uint32_t rx_num_desc;
    uint32_t intr_idx;
    uint32_t rsvd;
};

struct vmxnet3_priv {
    struct vmxnet3_tx_desc *tx_desc;
    struct vmxnet3_rx_desc *rx_desc;
    struct vmxnet3_comp *tx_comp;
    struct vmxnet3_comp *rx_comp;
    void *tx_desc_ring;
    void *rx_desc_ring;
    void *tx_comp_ring;
    void *rx_comp_ring;
    void *rx_buffers[VMXNET3_RX_QUEUE_SIZE];
    uint64_t tx_desc_phys;
    uint64_t rx_desc_phys;
    uint64_t tx_comp_phys;
    uint64_t rx_comp_phys;
    uint16_t tx_head;
    uint16_t tx_tail;
    uint16_t rx_head;
    uint16_t rx_tail;
    uint8_t mac[6];
    volatile void *mmio_base;
    volatile void *shared_base;
    int initialized;
    int irq_num;
    int use_interrupts;
    uint8_t tx_gen;
    uint8_t rx_gen;
};

static void vmxnet3_irq_handler(void *ctx);

static int vmxnet3_probe(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    if (!plat) return -1;

    if (dev->vendor_id != VMWARE_VENDOR_ID) return -1;
    if (dev->device_id != VMXNET3_DEVICE_ID) return -1;

    struct vmxnet3_priv *priv = plat->alloc(sizeof(struct vmxnet3_priv));
    if (!priv) return -1;
    memset(priv, 0, sizeof(*priv));

    priv->mmio_base = dev->bars[0].virt_addr;
    priv->shared_base = dev->bars[1].virt_addr;
    priv->tx_head = 0;
    priv->tx_tail = 0;
    priv->rx_head = 0;
    priv->rx_tail = 0;
    priv->tx_gen = 1;
    priv->rx_gen = 1;
    priv->initialized = 0;

    /* Check version */
    uint32_t version = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + VMXNET3_REG_VERSION));
    if (version < 2) {
        plat->log("vmxnet3: unsupported version %d\n", version);
        plat->free(priv);
        return -1;
    }

    /* Set version */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + VMXNET3_REG_VRRS), 1);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + VMXNET3_REG_UVRS), 1);

    /* Allocate TX descriptor ring */
    size_t tx_desc_size = sizeof(struct vmxnet3_tx_desc) * VMXNET3_TX_QUEUE_SIZE;
    priv->tx_desc_ring = plat->dma_alloc(tx_desc_size, &priv->tx_desc_phys);
    if (!priv->tx_desc_ring) {
        plat->free(priv);
        return -1;
    }
    memset(priv->tx_desc_ring, 0, tx_desc_size);
    priv->tx_desc = (struct vmxnet3_tx_desc *)priv->tx_desc_ring;

    /* Allocate TX completion ring */
    size_t tx_comp_size = sizeof(struct vmxnet3_comp) * VMXNET3_TX_QUEUE_SIZE;
    priv->tx_comp_ring = plat->dma_alloc(tx_comp_size, &priv->tx_comp_phys);
    if (!priv->tx_comp_ring) {
        plat->dma_free(priv->tx_desc_ring, priv->tx_desc_phys, tx_desc_size);
        plat->free(priv);
        return -1;
    }
    memset(priv->tx_comp_ring, 0, tx_comp_size);
    priv->tx_comp = (struct vmxnet3_comp *)priv->tx_comp_ring;

    /* Allocate RX descriptor ring */
    size_t rx_desc_size = sizeof(struct vmxnet3_rx_desc) * VMXNET3_RX_QUEUE_SIZE;
    priv->rx_desc_ring = plat->dma_alloc(rx_desc_size, &priv->rx_desc_phys);
    if (!priv->rx_desc_ring) {
        plat->dma_free(priv->tx_desc_ring, priv->tx_desc_phys, tx_desc_size);
        plat->dma_free(priv->tx_comp_ring, priv->tx_comp_phys, tx_comp_size);
        plat->free(priv);
        return -1;
    }
    memset(priv->rx_desc_ring, 0, rx_desc_size);
    priv->rx_desc = (struct vmxnet3_rx_desc *)priv->rx_desc_ring;

    /* Allocate RX completion ring */
    size_t rx_comp_size = sizeof(struct vmxnet3_comp) * VMXNET3_RX_QUEUE_SIZE;
    priv->rx_comp_ring = plat->dma_alloc(rx_comp_size, &priv->rx_comp_phys);
    if (!priv->rx_comp_ring) {
        plat->dma_free(priv->tx_desc_ring, priv->tx_desc_phys, tx_desc_size);
        plat->dma_free(priv->tx_comp_ring, priv->tx_comp_phys, tx_comp_size);
        plat->dma_free(priv->rx_desc_ring, priv->rx_desc_phys, rx_desc_size);
        plat->free(priv);
        return -1;
    }
    memset(priv->rx_comp_ring, 0, rx_comp_size);
    priv->rx_comp = (struct vmxnet3_comp *)priv->rx_comp_ring;

    /* Allocate RX buffers */
    for (int i = 0; i < VMXNET3_RX_QUEUE_SIZE; i++) {
        priv->rx_buffers[i] = plat->dma_alloc(VMXNET3_RX_BUFFER_SIZE, (uint64_t *)&priv->rx_desc[i].addr);
        if (!priv->rx_buffers[i]) {
            for (int j = 0; j < i; j++) {
                plat->dma_free(priv->rx_buffers[j], priv->rx_desc[j].addr, VMXNET3_RX_BUFFER_SIZE);
            }
            plat->dma_free(priv->tx_desc_ring, priv->tx_desc_phys, tx_desc_size);
            plat->dma_free(priv->tx_comp_ring, priv->tx_comp_phys, tx_comp_size);
            plat->dma_free(priv->rx_desc_ring, priv->rx_desc_phys, rx_desc_size);
            plat->dma_free(priv->rx_comp_ring, priv->rx_comp_phys, rx_comp_size);
            plat->free(priv);
            return -1;
        }
        priv->rx_desc[i].len = VMXNET3_RX_BUFFER_SIZE;
        priv->rx_desc[i].gen = priv->rx_gen;
    }

    /* Setup queue configuration */
    struct vmxnet3_queue_conf *conf = (struct vmxnet3_queue_conf *)priv->shared_base;
    conf->tx_desc_phys = priv->tx_desc_phys;
    conf->tx_comp_phys = priv->tx_comp_phys;
    conf->rx_desc_phys = priv->rx_desc_phys;
    conf->rx_comp_phys = priv->rx_comp_phys;
    conf->tx_desc_size = sizeof(struct vmxnet3_tx_desc);
    conf->tx_comp_size = sizeof(struct vmxnet3_comp);
    conf->rx_desc_size = sizeof(struct vmxnet3_rx_desc);
    conf->rx_comp_size = sizeof(struct vmxnet3_comp);
    conf->tx_num_desc = VMXNET3_TX_QUEUE_SIZE;
    conf->rx_num_desc = VMXNET3_RX_QUEUE_SIZE;
    conf->intr_idx = 0;

    /* Read MAC address */
    uint32_t macl = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + VMXNET3_REG_MACL));
    uint32_t mach = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + VMXNET3_REG_MACH));
    priv->mac[0] = macl & 0xFF;
    priv->mac[1] = (macl >> 8) & 0xFF;
    priv->mac[2] = (macl >> 16) & 0xFF;
    priv->mac[3] = macl >> 24;
    priv->mac[4] = mach & 0xFF;
    priv->mac[5] = (mach >> 8) & 0xFF;

    /* Set RX mode */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + VMXNET3_REG_CMD), VMXNET3_CMD_SET_RXMODE);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + VMXNET3_REG_CMD), 
                       VMXNET3_RXMODE_UCAST | VMXNET3_RXMODE_BCAST | VMXNET3_RXMODE_ALLMULTI);

    /* Activate device */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + VMXNET3_REG_CMD), VMXNET3_CMD_ACTIVATE_DEV);

    /* Try to register interrupt handler */
    priv->irq_num = dev->irq;
    priv->use_interrupts = 0;
    if (priv->irq_num >= 0) {
        int irq_result = plat->irq_request(priv->irq_num, vmxnet3_irq_handler, dev);
        if (irq_result == 0) {
            priv->use_interrupts = 1;
            /* Enable interrupts */
            plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + VMXNET3_REG_INTR), 
                               VMXNET3_INTR_TYPE_INTX);
            plat->log("vmxnet3: interrupt handler registered for IRQ %d\n", priv->irq_num);
        } else {
            plat->log("vmxnet3: interrupt registration failed, using polling\n");
        }
    } else {
        plat->log("vmxnet3: no IRQ available, using polling\n");
    }

    priv->initialized = 1;
    dev->driver_data = priv;

    plat->log("vmxnet3: probed device\n");
    return 0;
}

static void vmxnet3_irq_handler(void *ctx)
{
    struct nc_device *dev = (struct nc_device *)ctx;
    struct vmxnet3_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !plat) return;

    uint32_t event = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + VMXNET3_REG_EVENT));
    
    /* Handle RX interrupt */
    if (event & 0x01) {
        /* RX processing will be done in recv() */
    }
    
    /* Handle TX interrupt */
    if (event & 0x02) {
        /* TX completion will be handled in next send() */
    }
}

static void vmxnet3_remove(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    struct vmxnet3_priv *priv = dev->driver_data;
    if (!plat || !priv) return;

    /* Disable device */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + VMXNET3_REG_CMD), VMXNET3_CMD_DISABLE);
    
    /* Unregister interrupt handler if registered */
    if (priv->use_interrupts && priv->irq_num >= 0) {
        plat->irq_free(priv->irq_num);
    }

    /* Free RX buffers */
    for (int i = 0; i < VMXNET3_RX_QUEUE_SIZE; i++) {
        if (priv->rx_buffers[i]) {
            plat->dma_free(priv->rx_buffers[i], priv->rx_desc[i].addr, VMXNET3_RX_BUFFER_SIZE);
        }
    }

    if (priv->tx_desc_ring) plat->dma_free(priv->tx_desc_ring, priv->tx_desc_phys, sizeof(struct vmxnet3_tx_desc) * VMXNET3_TX_QUEUE_SIZE);
    if (priv->tx_comp_ring) plat->dma_free(priv->tx_comp_ring, priv->tx_comp_phys, sizeof(struct vmxnet3_comp) * VMXNET3_TX_QUEUE_SIZE);
    if (priv->rx_desc_ring) plat->dma_free(priv->rx_desc_ring, priv->rx_desc_phys, sizeof(struct vmxnet3_rx_desc) * VMXNET3_RX_QUEUE_SIZE);
    if (priv->rx_comp_ring) plat->dma_free(priv->rx_comp_ring, priv->rx_comp_phys, sizeof(struct vmxnet3_comp) * VMXNET3_RX_QUEUE_SIZE);

    plat->free(priv);
    dev->driver_data = NULL;
}

static int vmxnet3_send(struct nc_device *dev, const uint8_t *frame, size_t len)
{
    struct vmxnet3_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !frame || len == 0 || !plat || !priv->initialized) return -1;
    
    /* Security: validate frame length */
    if (len > 9600) return -1;

    uint16_t idx = priv->tx_tail % VMXNET3_TX_QUEUE_SIZE;
    
    /* Check if descriptor is available */
    if (priv->tx_desc[idx].gen == priv->tx_gen) return -1;
    
    priv->tx_desc[idx].addr = (uint64_t)(uintptr_t)frame;
    priv->tx_desc[idx].len = (uint32_t)len;
    priv->tx_desc[idx].gen = priv->tx_gen;
    priv->tx_desc[idx].cmd = 0x01; /* EOP */
    
    __sync_synchronize();
    
    priv->tx_tail++;
    
    /* Toggle generation if wrapped */
    if (priv->tx_tail % VMXNET3_TX_QUEUE_SIZE == 0) {
        priv->tx_gen = 1 - priv->tx_gen;
    }
    
    return (int)len;
}

static int vmxnet3_recv(struct nc_device *dev, uint8_t *buf, size_t buf_len)
{
    struct vmxnet3_priv *priv = dev->driver_data;
    if (!priv || !buf || !priv->initialized) return -1;
    
    /* Security: validate buffer length */
    if (buf_len == 0 || buf_len > 9600) return -1;

    __sync_synchronize();
    
    uint16_t idx = priv->rx_head % VMXNET3_RX_QUEUE_SIZE;
    
    /* Check if descriptor is owned by driver (not device) */
    if (priv->rx_desc[idx].gen != priv->rx_gen) return -1;
    
    /* Security: bounds check */
    if (priv->rx_desc[idx].len > buf_len || priv->rx_desc[idx].len > 9600) return -1;
    
    memcpy(buf, (void *)(uintptr_t)priv->rx_desc[idx].addr, priv->rx_desc[idx].len);
    
    uint32_t pkt_len = priv->rx_desc[idx].len;
    
    /* Return buffer to device */
    priv->rx_desc[idx].gen = 1 - priv->rx_gen;
    priv->rx_head++;
    
    /* Toggle generation if wrapped */
    if (priv->rx_head % VMXNET3_RX_QUEUE_SIZE == 0) {
        priv->rx_gen = 1 - priv->rx_gen;
    }
    
    return (int)pkt_len;
}

static void vmxnet3_get_mac(struct nc_device *dev, uint8_t mac_out[6])
{
    struct vmxnet3_priv *priv = dev->driver_data;
    if (!priv || !mac_out) return;
    memcpy(mac_out, priv->mac, 6);
}

struct nc_net_driver nc_vmxnet3_driver = {
    .name = "vmxnet3",
    .probe = vmxnet3_probe,
    .remove = vmxnet3_remove,
    .send = vmxnet3_send,
    .recv = vmxnet3_recv,
    .get_mac = vmxnet3_get_mac,
};
