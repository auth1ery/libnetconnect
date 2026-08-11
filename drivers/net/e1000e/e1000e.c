#include "e1000e.h"
#include "nc/platform.h"
#include <string.h>

#define INTEL_VENDOR_ID 0x8086

/* e1000e device IDs */
#define E1000E_DEVICE_ID_ICH8_IGP_M 0x104B
#define E1000E_DEVICE_ID_ICH8_IGP_M_AMT 0x104C
#define E1000E_DEVICE_ID_ICH8_IGP_M_V 0x104D
#define E1000E_DEVICE_ID_ICH9_IGP_M 0x10F5
#define E1000E_DEVICE_ID_ICH9_IGP_M_AMT 0x10F6
#define E1000E_DEVICE_ID_ICH9_IGP_M_V 0x10F7
#define E1000E_DEVICE_ID_PCH_LM 0x10EA
#define E1000E_DEVICE_ID_PCH_LM_LP 0x10EB
#define E1000E_DEVICE_ID_PCH_V 0x10EF
#define E1000E_DEVICE_ID_PCH_V_LP 0x10F0
#define E1000E_DEVICE_ID_PCH_I 0x10F1
#define E1000E_DEVICE_ID_PCH_I_LP 0x10F2
#define E1000E_DEVICE_ID_PCH_D 0x10D3
#define E1000E_DEVICE_ID_PCH_D_LP 0x10D6
#define E1000E_DEVICE_ID_PCH_N 0x10D7
#define E1000E_DEVICE_ID_PCH_SPT_I219_V 0x156F
#define E1000E_DEVICE_ID_PCH_SPT_I219_LM 0x1570
#define E1000E_DEVICE_ID_PCH_SPT_I219_V2 0x15B8
#define E1000E_DEVICE_ID_PCH_LBG_I219_LM 0x15B9
#define E1000E_DEVICE_ID_PCH_SPT_I219_LM2 0x15B9
#define E1000E_DEVICE_ID_PCH_SPT_I219_V5 0x15D7
#define E1000E_DEVICE_ID_PCH_CNP_I219_LM6 0x15D8
#define E1000E_DEVICE_ID_PCH_CNP_I219_V5 0x15D6
#define E1000E_DEVICE_ID_PCH_CNP_I219_V7 0x15BD
#define E1000E_DEVICE_ID_PCH_CNP_I219_LM7 0x15BE

/* e1000e registers */
#define E1000E_CTRL 0x00000
#define E1000E_STATUS 0x00008
#define E1000E_EECD 0x00010
#define E1000E_EERD 0x00014
#define E1000E_CTRL_EXT 0x00018
#define E1000E_MDIC 0x00020
#define E1000E_SCTL 0x00024
#define E1000E_FCAL 0x00028
#define E1000E_FCAH 0x0002C
#define E1000E_FCT 0x00030
#define E1000E_VET 0x00038
#define E1000E_ICR 0x000C0
#define E1000E_ITR 0x000C4
#define E1000E_ICS 0x000C8
#define E1000E_IMS 0x000D0
#define E1000E_IMC 0x000D8
#define E1000E_RCTL 0x00100
#define E1000E_FCTTV 0x00170
#define E1000E_FCRTL 0x002160
#define E1000E_FCRTH 0x002164
#define E1000E_PBA 0x01000
#define E1000E_PBACL 0x01008
#define E1000E_RDTR 0x02820
#define E1000E_RDBAL 0x02800
#define E1000E_RDBAH 0x02804
#define E1000E_RDLEN 0x02808
#define E1000E_RDH 0x02810
#define E1000E_RDT 0x02818
#define E1000E_RXDCTL 0x02828
#define E1000E_RSRPD 0x02C00
#define E1000E_RDBAL0 0x02800
#define E1000E_RDBAH0 0x02804
#define E1000E_RDLEN0 0x02808
#define E1000E_RDH0 0x02810
#define E1000E_RDT0 0x02818
#define E1000E_RXDCTL0 0x02828
#define E1000E_TCTL 0x00400
#define E1000E_TIPG 0x00410
#define E1000E_TDBAL 0x03800
#define E1000E_TDBAH 0x03804
#define E1000E_TDLEN 0x03808
#define E1000E_TDH 0x03810
#define E1000E_TDT 0x03818
#define E1000E_TXDCTL 0x03828
#define E1000E_TDBAL0 0x03800
#define E1000E_TDBAH0 0x03804
#define E1000E_TDLEN0 0x03808
#define E1000E_TDH0 0x03810
#define E1000E_TDT0 0x03818
#define E1000E_TXDCTL0 0x03828
#define E1000E_TARC0 0x03840
#define E1000E_RAL 0x05400
#define E1000E_RAH 0x05404
#define E1000E_MTA 0x05200

/* Control register bits */
#define E1000E_CTRL_FD 0x00000001
#define E1000E_CTRL_BEM 0x00000002
#define E1000E_CTRL_PRIOR 0x00000004
#define E1000E_CTRL_LRST 0x00000008
#define E1000E_CTRL_TME 0x00000010
#define E1000E_CTRL_SLE 0x00000020
#define E1000E_CTRL_ALD 0x00000040
#define E1000E_CTRL_RFR 0x00000080
#define E1000E_CTRL_RCTL 0x00000100
#define E1000E_CTRL_RCVE 0x00000200
#define E1000E_CTRL_RST 0x00000400
#define E1000E_CTRL_RFCE 0x00000800
#define E1000E_CTRL_TFCE 0x00001000
#define E1000E_CTRL_VME 0x00004000
#define E1000E_CTRL_PHY_RST 0x00008000
#define E1000E_CTRL_SWDPIN0 0x00010000
#define E1000E_CTRL_SWDPIN1 0x00020000
#define E1000E_CTRL_SWDPIN2 0x00040000
#define E1000E_CTRL_SWDPIN3 0x00080000
#define E1000E_CTRL_SWDPIO0 0x00100000
#define E1000E_CTRL_SWDPIO1 0x00200000
#define E1000E_CTRL_SWDPIO2 0x00400000
#define E1000E_CTRL_SWDPIO3 0x00800000
#define E1000E_CTRL_RST_AN 0x01000000
#define E1000E_CTRL_GIO_MASTER_DISABLE 0x04000000
#define E1000E_CTRL_LINK_RST 0x08000000
#define E1000E_CTRL_EXT_LINK_EN 0x10000000

/* Status register bits */
#define E1000E_STATUS_FD 0x00000001
#define E1000E_STATUS_LU 0x00000002
#define E1000E_STATUS_FUNC_MASK 0x0000000C
#define E1000E_STATUS_TXOFF 0x00000010
#define E1000E_STATUS_TBIMODE 0x00000020
#define E1000E_STATUS_SPEED_MASK 0x000000C0
#define E1000E_STATUS_SPEED_10 0x00000000
#define E1000E_STATUS_SPEED_100 0x00000040
#define E1000E_STATUS_SPEED_1000 0x00000080
#define E1000E_STATUS_ASDV 0x00000300
#define E1000E_STATUS_MTXCKOK 0x00000400
#define E1000E_STATUS_PCI66 0x00000800
#define E1000E_STATUS_BUS64 0x00001000
#define E1000E_STATUS_PCIX_MODE 0x00002000
#define E1000E_STATUS_PCIXSPD_MASK 0x0000C000
#define E1000E_STATUS_BMC2O 0x00010000
#define E1000E_STATUS_BMCST 0x00020000
#define E1000E_STATUS_TX_PAUSE 0x00040000
#define E1000E_STATUS_RX_PAUSE 0x00080000
#define E1000E_STATUS_LAN_INIT_DONE 0x00200000

/* Interrupt bits */
#define E1000E_INT_TXDW 0x00000001
#define E1000E_INT_TXQE 0x00000002
#define E1000E_INT_LSC 0x00000004
#define E1000E_INT_RXDMT0 0x00000010
#define E1000E_INT_RXSEQ 0x00000008
#define E1000E_INT_RXO 0x00000040
#define E1000E_INT_RXT0 0x00000080
#define E1000E_INT_MDAC 0x00000200
#define E1000E_INT_RXCFG 0x00000400
#define E1000E_INT_GPRSC 0x00001000
#define E1000E_INT_PHYINT 0x00004000
#define E1000E_INT_EPRST 0x00008000
#define E1000E_INT_SRPD 0x00010000

/* Receive control bits */
#define E1000E_RCTL_EN 0x00000002
#define E1000E_RCTL_SBP 0x00000004
#define E1000E_RCTL_UPE 0x00000008
#define E1000E_RCTL_MPE 0x00000010
#define E1000E_RCTL_LPE 0x00000020
#define E1000E_RCTL_LBM_MASK 0x000000C0
#define E1000E_RCTL_LBM_MAC 0x00000040
#define E1000E_RCTL_LBM_SLP 0x00000080
#define E1000E_RCTL_LBM_TCVR 0x000000C0
#define E1000E_RCTL_RDMTS_MASK 0x00000300
#define E1000E_RCTL_RDMTS_HALF 0x00000000
#define E1000E_RCTL_RDMTS_QUART 0x00000100
#define E1000E_RCTL_RDMTS_EIGTH 0x00000200
#define E1000E_RCTL_MO_MASK 0x00003000
#define E1000E_RCTL_MO_0 0x00000000
#define E1000E_RCTL_MO_1 0x00001000
#define E1000E_RCTL_MO_2 0x00002000
#define E1000E_RCTL_MO_3 0x00003000
#define E1000E_RCTL_BAM 0x00008000
#define E1000E_RCTL_BSIZE_MASK 0x00030000
#define E1000E_RCTL_BSIZE_2048 0x00000000
#define E1000E_RCTL_BSIZE_1024 0x00010000
#define E1000E_RCTL_BSIZE_512 0x00020000
#define E1000E_RCTL_BSIZE_256 0x00030000
#define E1000E_RCTL_VFE 0x00040000
#define E1000E_RCTL_CFIEN 0x00080000
#define E1000E_RCTL_CFI 0x00100000
#define E1000E_RCTL_DPF 0x00400000
#define E1000E_RCTL_PMCF 0x00800000
#define E1000E_RCTL_SECRC 0x04000000

/* Transmit control bits */
#define E1000E_TCTL_EN 0x00000002
#define E1000E_TCTL_PSP 0x00000008
#define E1000E_TCTL_CT_MASK 0x000000FF
#define E1000E_TCTL_CT_SHIFT 4
#define E1000E_TCTL_COLD_MASK 0x00FF0000
#define E1000E_TCTL_COLD_SHIFT 16
#define E1000E_TCTL_SWXOFF 0x01000000
#define E1000E_TCTL_PBE 0x10000000
#define E1000E_TCTL_RTLC 0x40000000
#define E1000E_TCTL_NRTU 0x80000000

#define E1000E_RX_DESC_COUNT 256
#define E1000E_TX_DESC_COUNT 256
#define E1000E_RX_BUFFER_SIZE 2048

struct e1000e_rx_desc {
    uint64_t buffer_addr;
    uint16_t length;
    uint16_t csum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
};

struct e1000e_tx_desc {
    uint64_t buffer_addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
};

struct e1000e_priv {
    struct e1000e_rx_desc *rx_desc;
    struct e1000e_tx_desc *tx_desc;
    void *rx_ring;
    void *tx_ring;
    void *rx_buffers[E1000E_RX_DESC_COUNT];
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

static void e1000e_irq_handler(void *ctx);

static int e1000e_probe(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    if (!plat) return -1;

    if (dev->vendor_id != INTEL_VENDOR_ID) return -1;
    
    /* Check for supported device IDs */
    switch (dev->device_id) {
        case E1000E_DEVICE_ID_ICH8_IGP_M:
        case E1000E_DEVICE_ID_ICH8_IGP_M_AMT:
        case E1000E_DEVICE_ID_ICH8_IGP_M_V:
        case E1000E_DEVICE_ID_ICH9_IGP_M:
        case E1000E_DEVICE_ID_ICH9_IGP_M_AMT:
        case E1000E_DEVICE_ID_ICH9_IGP_M_V:
        case E1000E_DEVICE_ID_PCH_LM:
        case E1000E_DEVICE_ID_PCH_LM_LP:
        case E1000E_DEVICE_ID_PCH_V:
        case E1000E_DEVICE_ID_PCH_V_LP:
        case E1000E_DEVICE_ID_PCH_I:
        case E1000E_DEVICE_ID_PCH_I_LP:
        case E1000E_DEVICE_ID_PCH_D:
        case E1000E_DEVICE_ID_PCH_D_LP:
        case E1000E_DEVICE_ID_PCH_N:
        case E1000E_DEVICE_ID_PCH_SPT_I219_V:
        case E1000E_DEVICE_ID_PCH_SPT_I219_LM:
        case E1000E_DEVICE_ID_PCH_SPT_I219_V2:
        case E1000E_DEVICE_ID_PCH_SPT_I219_V5:
        case E1000E_DEVICE_ID_PCH_CNP_I219_LM6:
        case E1000E_DEVICE_ID_PCH_CNP_I219_V5:
        case E1000E_DEVICE_ID_PCH_CNP_I219_V7:
        case E1000E_DEVICE_ID_PCH_CNP_I219_LM7:
            break;
        default:
            return -1;
    }

    struct e1000e_priv *priv = plat->alloc(sizeof(struct e1000e_priv));
    if (!priv) return -1;
    memset(priv, 0, sizeof(*priv));

    priv->mmio_base = dev->bars[0].virt_addr;
    priv->rx_tail = 0;
    priv->tx_tail = 0;
    priv->rx_head = 0;
    priv->tx_head = 0;
    priv->initialized = 0;

    /* Reset device */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_CTRL), E1000E_CTRL_RST);
    plat->sleep_ms(10);

    /* Disable interrupts */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_IMC), 0xFFFFFFFF);

    /* Setup RX descriptor ring */
    priv->rx_ring_bytes = sizeof(struct e1000e_rx_desc) * E1000E_RX_DESC_COUNT;
    priv->rx_ring = plat->dma_alloc(priv->rx_ring_bytes, &priv->rx_phys);
    if (!priv->rx_ring) {
        plat->free(priv);
        return -1;
    }
    memset(priv->rx_ring, 0, priv->rx_ring_bytes);
    priv->rx_desc = (struct e1000e_rx_desc *)priv->rx_ring;

    /* Setup TX descriptor ring */
    priv->tx_ring_bytes = sizeof(struct e1000e_tx_desc) * E1000E_TX_DESC_COUNT;
    priv->tx_ring = plat->dma_alloc(priv->tx_ring_bytes, &priv->tx_phys);
    if (!priv->tx_ring) {
        plat->dma_free(priv->rx_ring, priv->rx_phys, priv->rx_ring_bytes);
        plat->free(priv);
        return -1;
    }
    memset(priv->tx_ring, 0, priv->tx_ring_bytes);
    priv->tx_desc = (struct e1000e_tx_desc *)priv->tx_ring;

    /* Allocate RX buffers */
    for (int i = 0; i < E1000E_RX_DESC_COUNT; i++) {
        priv->rx_buffers[i] = plat->dma_alloc(E1000E_RX_BUFFER_SIZE, (uint64_t *)&priv->rx_desc[i].buffer_addr);
        if (!priv->rx_buffers[i]) {
            for (int j = 0; j < i; j++) {
                plat->dma_free(priv->rx_buffers[j], priv->rx_desc[j].buffer_addr, E1000E_RX_BUFFER_SIZE);
            }
            plat->dma_free(priv->rx_ring, priv->rx_phys, priv->rx_ring_bytes);
            plat->dma_free(priv->tx_ring, priv->tx_phys, priv->tx_ring_bytes);
            plat->free(priv);
            return -1;
        }
        priv->rx_desc[i].length = E1000E_RX_BUFFER_SIZE;
        priv->rx_desc[i].status = 0;
    }

    /* Setup RX registers */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_RDBAL0), priv->rx_phys & 0xFFFFFFFF);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_RDBAH0), (priv->rx_phys >> 32) & 0xFFFFFFFF);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_RDLEN0), priv->rx_ring_bytes);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_RDH0), 0);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_RDT0), E1000E_RX_DESC_COUNT - 1);

    /* Setup TX registers */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_TDBAL0), priv->tx_phys & 0xFFFFFFFF);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_TDBAH0), (priv->tx_phys >> 32) & 0xFFFFFFFF);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_TDLEN0), priv->tx_ring_bytes);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_TDH0), 0);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_TDT0), 0);

    /* Configure RX control */
    uint32_t rctl = E1000E_RCTL_EN | E1000E_RCTL_BAM | E1000E_RCTL_BSIZE_2048 | E1000E_RCTL_SECRC;
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_RCTL), rctl);

    /* Configure TX control */
    uint32_t tctl = E1000E_TCTL_EN | E1000E_TCTL_PSP;
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_TCTL), tctl);

    /* Read MAC address from EEPROM */
    uint32_t ral = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_RAL));
    uint32_t rah = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_RAH));
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
        int irq_result = plat->irq_request(priv->irq_num, e1000e_irq_handler, dev);
        if (irq_result == 0) {
            priv->use_interrupts = 1;
            /* Enable interrupts */
            plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_IMS), 
                               E1000E_INT_TXDW | E1000E_INT_RXDMT0 | E1000E_INT_LSC);
            plat->log("e1000e: interrupt handler registered for IRQ %d\n", priv->irq_num);
        } else {
            plat->log("e1000e: interrupt registration failed, using polling\n");
        }
    } else {
        plat->log("e1000e: no IRQ available, using polling\n");
    }

    priv->initialized = 1;
    dev->driver_data = priv;

    plat->log("e1000e: probed device\n");
    return 0;
}

static void e1000e_irq_handler(void *ctx)
{
    struct nc_device *dev = (struct nc_device *)ctx;
    struct e1000e_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !plat) return;

    uint32_t icr = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_ICR));
    
    /* Handle RX interrupt */
    if (icr & E1000E_INT_RXDMT0) {
        /* RX processing will be done in recv() */
    }
    
    /* Handle TX interrupt */
    if (icr & E1000E_INT_TXDW) {
        /* TX completion will be handled in next send() */
    }
    
    /* Handle link status change */
    if (icr & E1000E_INT_LSC) {
        /* Link status changed */
    }
}

static void e1000e_remove(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    struct e1000e_priv *priv = dev->driver_data;
    if (!plat || !priv) return;

    /* Disable interrupts */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_IMC), 0xFFFFFFFF);
    
    /* Unregister interrupt handler if registered */
    if (priv->use_interrupts && priv->irq_num >= 0) {
        plat->irq_free(priv->irq_num);
    }

    /* Disable RX and TX */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_RCTL), 0);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_TCTL), 0);

    /* Free RX buffers */
    for (int i = 0; i < E1000E_RX_DESC_COUNT; i++) {
        if (priv->rx_buffers[i]) {
            plat->dma_free(priv->rx_buffers[i], priv->rx_desc[i].buffer_addr, E1000E_RX_BUFFER_SIZE);
        }
    }

    if (priv->rx_ring) plat->dma_free(priv->rx_ring, priv->rx_phys, priv->rx_ring_bytes);
    if (priv->tx_ring) plat->dma_free(priv->tx_ring, priv->tx_phys, priv->tx_ring_bytes);

    plat->free(priv);
    dev->driver_data = NULL;
}

static int e1000e_send(struct nc_device *dev, const uint8_t *frame, size_t len)
{
    struct e1000e_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !frame || len == 0 || !plat || !priv->initialized) return -1;
    
    /* Security: validate frame length */
    if (len > 9600) return -1;

    uint16_t idx = priv->tx_tail % E1000E_TX_DESC_COUNT;
    
    /* Check if descriptor is available */
    if (priv->tx_desc[idx].status & 0x01) return -1;
    
    priv->tx_desc[idx].buffer_addr = (uint64_t)(uintptr_t)frame;
    priv->tx_desc[idx].length = (uint16_t)len;
    priv->tx_desc[idx].cmd = 0x0B; /* EOP + RS */
    priv->tx_desc[idx].status = 0;
    priv->tx_desc[idx].special = 0;
    
    __sync_synchronize();
    
    priv->tx_tail++;
    
    /* Write tail pointer */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_TDT0), priv->tx_tail);
    
    return (int)len;
}

static int e1000e_recv(struct nc_device *dev, uint8_t *buf, size_t buf_len)
{
    struct e1000e_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !buf || !priv->initialized) return -1;
    
    /* Security: validate buffer length */
    if (buf_len == 0 || buf_len > 9600) return -1;

    __sync_synchronize();
    
    uint16_t idx = priv->rx_tail % E1000E_RX_DESC_COUNT;
    
    if (!(priv->rx_desc[idx].status & 0x01)) return 0;
    
    /* Security: bounds check */
    if (priv->rx_desc[idx].length > buf_len || priv->rx_desc[idx].length > 9600) return -1;
    
    memcpy(buf, (void *)(uintptr_t)priv->rx_desc[idx].buffer_addr, priv->rx_desc[idx].length);
    
    uint16_t pkt_len = priv->rx_desc[idx].length;
    priv->rx_desc[idx].status = 0;
    priv->rx_tail++;
    
    /* Update tail pointer */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + E1000E_RDT0), priv->rx_tail);
    
    return (int)pkt_len;
}

static void e1000e_get_mac(struct nc_device *dev, uint8_t mac_out[6])
{
    struct e1000e_priv *priv = dev->driver_data;
    if (!priv || !mac_out) return;
    memcpy(mac_out, priv->mac, 6);
}

struct nc_net_driver nc_e1000e_driver = {
    .name = "e1000e",
    .probe = e1000e_probe,
    .remove = e1000e_remove,
    .send = e1000e_send,
    .recv = e1000e_recv,
    .get_mac = e1000e_get_mac,
};
