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
#define E1000_IMC 0x000D8
#define E1000_RCTL 0x00100
#define E1000_TCTL 0x00400
#define E1000_TDT 0x00380
#define E1000_TDLEN 0x00388
#define E1000_TDBAL 0x00380
#define E1000_TDBAH 0x00384
#define E1000_RDT 0x00180
#define E1000_RDH 0x00110
#define E1000_RDLEN 0x00108
#define E1000_RDBAL 0x00100
#define E1000_RDBAH 0x00104
#define E1000_RAL 0x05400
#define E1000_RAH 0x05404

#define E1000_RX_DESC_COUNT 256
#define E1000_TX_DESC_COUNT 256
#define E1000_RX_BUFFER_SIZE 2048

/* Control register bits */
#define E1000_CTRL_RST 0x04000000
#define E1000_CTRL_ASDE 0x00000020
#define E1000_CTRL_SLU 0x00000040
#define E1000_CTRL_ILOS 0x00000080
#define E1000_CTRL_VME 0x00040000

/* Receive control bits */
#define E1000_RCTL_EN 0x00000002
#define E1000_RCTL_SBP 0x00000004
#define E1000_RCTL_UPE 0x00000008
#define E1000_RCTL_MPE 0x00000010
#define E1000_RCTL_LPE 0x00000020
#define E1000_RCTL_LBM_NO 0x00000000
#define E1000_RCTL_LBM_MAC 0x00000040
#define E1000_RCTL_LBM_SLP 0x00000080
#define E1000_RCTL_LBM_TCVR 0x000000C0
#define E1000_RCTL_RDMTS_HALF 0x00000000
#define E1000_RCTL_RDMTS_QUAT 0x00000100
#define E1000_RCTL_RDMTS_EIGTH 0x00000200
#define E1000_RCTL_MO_SHIFT 12
#define E1000_RCTL_MO_0 0x00000000
#define E1000_RCTL_MO_1 0x00001000
#define E1000_RCTL_MO_2 0x00002000
#define E1000_RCTL_MO_3 0x00003000
#define E1000_RCTL_BAM 0x00008000
#define E1000_RCTL_BSIZE_256 0x00000000
#define E1000_RCTL_BSIZE_512 0x00010000
#define E1000_RCTL_BSIZE_1024 0x00020000
#define E1000_RCTL_BSIZE_2048 0x00030000
#define E1000_RCTL_BSIZE_4096 0x00040000
#define E1000_RCTL_BSIZE_8192 0x00050000
#define E1000_RCTL_BSIZE_16384 0x00060000
#define E1000_RCTL_SECRC 0x04000000

/* Transmit control bits */
#define E1000_TCTL_EN 0x00000002
#define E1000_TCTL_PSP 0x00000008
#define E1000_TCTL_CT_SHIFT 4
#define E1000_TCTL_COLD_SHIFT 12
#define E1000_TCTL_RTLC 0x01000000
#define E1000_TCTL_NRTU 0x02000000

/* Transmit descriptor command bits */
#define E1000_TXD_CMD_EOP 0x01
#define E1000_TXD_CMD_RS 0x02
#define E1000_TXD_CMD_VLE 0x04
#define E1000_TXD_CMD_IC 0x08
#define E1000_TXD_CMD_IDE 0x10
#define E1000_TXD_CMD_RPS 0x20
#define E1000_TXD_CMD_RSV 0x40
#define E1000_TXD_CMD_DEXT 0x20

/* Interrupt bits */
#define E1000_ICR_TXDW 0x00000001
#define E1000_ICR_TXQE 0x00000002
#define E1000_ICR_LSC 0x00000004
#define E1000_ICR_RXDMT0 0x00000010
#define E1000_ICR_RXO 0x00000040
#define E1000_ICR_RXT0 0x00000080
#define E1000_ICR_MDAC 0x00000200
#define E1000_ICR_RXCFG 0x00000400
#define E1000_ICR_GPI_EN0 0x00000800
#define E1000_ICR_GPI_EN1 0x00001000
#define E1000_ICR_GPI_EN2 0x00002000
#define E1000_ICR_GPI_EN3 0x00004000

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
    uint8_t css;
    uint8_t status;
    uint8_t cmd;
    uint32_t special;
};

struct e1000_priv {
    struct e1000_rx_desc *rx_desc;
    struct e1000_tx_desc *tx_desc;
    void *rx_ring;
    void *tx_ring;
    void *rx_buffers[E1000_RX_DESC_COUNT];
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

static void e1000_irq_handler(void *ctx);

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
    priv->rx_head = 0;
    priv->tx_tail = 0;
    priv->tx_head = 0;
    priv->initialized = 0;

    /* Reset device */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000_CTRL), E1000_CTRL_RST);
    plat->sleep_ms(10);

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

    /* Allocate RX buffers */
    for (int i = 0; i < E1000_RX_DESC_COUNT; i++) {
        priv->rx_buffers[i] = plat->dma_alloc(E1000_RX_BUFFER_SIZE, (uint64_t *)&priv->rx_desc[i].buffer_addr);
        if (!priv->rx_buffers[i]) {
            for (int j = 0; j < i; j++) {
                plat->dma_free(priv->rx_buffers[j], priv->rx_desc[j].buffer_addr, E1000_RX_BUFFER_SIZE);
            }
            plat->dma_free(priv->rx_ring, priv->rx_phys, priv->rx_ring_bytes);
            plat->dma_free(priv->tx_ring, priv->tx_phys, priv->tx_ring_bytes);
            plat->free(priv);
            return -1;
        }
        priv->rx_desc[i].length = 0;
        priv->rx_desc[i].status = 0;
    }

    /* Setup RX descriptor ring */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000_RDBAL), priv->rx_phys & 0xFFFFFFFF);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000_RDBAH), (priv->rx_phys >> 32) & 0xFFFFFFFF);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000_RDLEN), priv->rx_ring_bytes);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000_RDT), E1000_RX_DESC_COUNT - 1);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000_RDH), 0);

    /* Enable RX */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000_RCTL), 
                       E1000_RCTL_EN | E1000_RCTL_SBP | E1000_RCTL_UPE | E1000_RCTL_MPE |
                       E1000_RCTL_BAM | E1000_RCTL_BSIZE_2048);

    /* Setup TX descriptor ring */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000_TDBAL), priv->tx_phys & 0xFFFFFFFF);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000_TDBAH), (priv->tx_phys >> 32) & 0xFFFFFFFF);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000_TDLEN), priv->tx_ring_bytes);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000_TDT), 0);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000_TDLEN), priv->tx_ring_bytes);

    /* Enable TX */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000_TCTL), 
                       E1000_TCTL_EN | E1000_TCTL_PSP);

    /* Read MAC address from EEPROM */
    uint32_t ral = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + E1000_RAL));
    uint32_t rah = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + E1000_RAH));
    
    priv->mac[0] = ral & 0xFF;
    priv->mac[1] = (ral >> 8) & 0xFF;
    priv->mac[2] = (ral >> 16) & 0xFF;
    priv->mac[3] = ral >> 24;
    priv->mac[4] = rah & 0xFF;
    priv->mac[5] = (rah >> 8) & 0xFF;

    /* Try to register interrupt handler */
    priv->irq_num = dev->irq;
    priv->use_interrupts = 0;
    if (priv->irq_num >= 0) {
        int irq_result = plat->irq_request(priv->irq_num, e1000_irq_handler, dev);
        if (irq_result == 0) {
            priv->use_interrupts = 1;
            /* Enable interrupts */
            plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000_IMS), 
                               E1000_ICR_TXDW | E1000_ICR_RXDMT0 | E1000_ICR_RXT0);
            plat->log("e1000: interrupt handler registered for IRQ %d\n", priv->irq_num);
        } else {
            plat->log("e1000: interrupt registration failed, using polling\n");
        }
    } else {
        plat->log("e1000: no IRQ available, using polling\n");
    }

    priv->initialized = 1;
    dev->driver_data = priv;

    plat->log("e1000: probed device\n");
    return 0;
}

static void e1000_irq_handler(void *ctx)
{
    struct nc_device *dev = (struct nc_device *)ctx;
    struct e1000_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !plat) return;

    /* Read interrupt cause */
    uint32_t icr = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + E1000_ICR));
    
    /* Handle RX interrupt */
    if (icr & (E1000_ICR_RXDMT0 | E1000_ICR_RXT0)) {
        /* RX processing will be done in recv() */
    }
    
    /* Handle TX interrupt */
    if (icr & (E1000_ICR_TXDW | E1000_ICR_TXQE)) {
        /* TX completion will be handled in next send() */
    }
    
    /* Handle link status change */
    if (icr & E1000_ICR_LSC) {
        /* Link status changed */
    }
}

static void e1000_remove(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    struct e1000_priv *priv = dev->driver_data;
    if (!plat || !priv) return;
    
    /* Disable interrupts */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000_IMC), 0xFFFFFFFF);
    
    /* Unregister interrupt handler if registered */
    if (priv->use_interrupts && priv->irq_num >= 0) {
        plat->irq_free(priv->irq_num);
    }
    
    /* Disable RX and TX */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000_RCTL), 0);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000_TCTL), 0);
    
    /* Free RX buffers */
    for (int i = 0; i < E1000_RX_DESC_COUNT; i++) {
        if (priv->rx_buffers[i]) {
            plat->dma_free(priv->rx_buffers[i], priv->rx_desc[i].buffer_addr, E1000_RX_BUFFER_SIZE);
        }
    }
    
    if (priv->rx_ring) plat->dma_free(priv->rx_ring, priv->rx_phys, priv->rx_ring_bytes);
    if (priv->tx_ring) plat->dma_free(priv->tx_ring, priv->tx_phys, priv->tx_ring_bytes);
    
    plat->free(priv);
    dev->driver_data = NULL;
}

static int e1000_send(struct nc_device *dev, const uint8_t *frame, size_t len)
{
    struct e1000_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !frame || len == 0 || !plat || !priv->initialized) return -1;
    
    /* Security: validate frame length */
    if (len > 9600) return -1; /* Maximum jumbo frame size */

    uint16_t idx = priv->tx_tail % E1000_TX_DESC_COUNT;
    
    /* Check if descriptor is available */
    if (priv->tx_desc[idx].status & 0x01) return -1;
    
    priv->tx_desc[idx].buffer_addr = (uint64_t)(uintptr_t)frame;
    priv->tx_desc[idx].length = (uint16_t)len;
    priv->tx_desc[idx].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
    priv->tx_desc[idx].status = 0;
    priv->tx_desc[idx].special = 0;
    
    __sync_synchronize();
    
    priv->tx_tail++;
    
    /* Write tail pointer */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000_TDT), priv->tx_tail);
    
    return (int)len;
}

static int e1000_recv(struct nc_device *dev, uint8_t *buf, size_t buf_len)
{
    struct e1000_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !buf || !priv->initialized) return -1;
    
    /* Security: validate buffer length */
    if (buf_len == 0 || buf_len > 9600) return -1;

    __sync_synchronize();
    
    uint16_t idx = priv->rx_tail % E1000_RX_DESC_COUNT;
    
    if (!(priv->rx_desc[idx].status & 0x01)) return 0; /* Descriptor not ready */
    
    /* Security: bounds check */
    if (priv->rx_desc[idx].length > buf_len || priv->rx_desc[idx].length > 9600) return -1;
    
    memcpy(buf, (void *)(uintptr_t)priv->rx_desc[idx].buffer_addr, priv->rx_desc[idx].length);
    
    priv->rx_desc[idx].status = 0;
    priv->rx_tail++;
    
    /* Update tail pointer */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000_RDT), priv->rx_tail);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000_RDH), priv->rx_head);
    
    return (int)priv->rx_desc[idx].length;
}

static void e1000_get_mac(struct nc_device *dev, uint8_t mac_out[6])
{
    struct e1000_priv *priv = dev->driver_data;
    if (!priv || !mac_out) return;
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
