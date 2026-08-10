#include "ahci.h"
#include "nc/platform.h"
#include <string.h>

#define AHCI_CLASS_CODE 0x0106
#define AHCI_VENDOR_ID_ANY 0xFFFF

/* AHCI generic registers */
#define AHCI_CAP 0x00
#define AHCI_GHC 0x04
#define AHCI_IS 0x08
#define AHCI_PI 0x0C
#define AHCI_VS 0x10
#define AHCI_CCC_CTL 0x14
#define AHCI_CCC_PORTS 0x18
#define AHCI_EM_LOC 0x1C
#define AHCI_EM_CTL 0x20
#define AHCI_CAP2 0x24
#define AHCI_BOHC 0x28
#define AHCI_OOB 0x2C
#define AHCI_GHCI 0x30

/* Port registers (offset 0x100 per port) */
#define AHCI_Px_CLB 0x00
#define AHCI_Px_CLBU 0x04
#define AHCI_Px_FB 0x08
#define AHCI_PxFBU 0x0C
#define AHCI_Px_IS 0x10
#define AHCI_Px_IE 0x14
#define AHCI_Px_CMD 0x18
#define AHCI_Px_TFD 0x20
#define AHCI_Px_SIG 0x24
#define AHCI_Px_SSTS 0x28
#define AHCI_Px_SCTL 0x2C
#define AHCI_Px_SERR 0x30
#define AHCI_Px_SACT 0x34
#define AHCI_Px_CI 0x38
#define AHCI_Px_SDB 0x44
#define AHCI_Px_FBS 0x40
#define AHCI_Px_DEVSLP 0x44

#define AHCI_CMD_ST 0x0001
#define AHCI_CMD_SUD 0x0002
#define AHCI_CMD_POD 0x0008
#define AHCI_CMD_CLO 0x0008
#define AHCI_CMD_FRE 0x0010
#define AHCI_CMD_CC 0x8000
#define AHCI_CMD_ICC 0xF000

#define AHCI_PORT_IMPLEMENTED(n) ((n) & 1)

#define AHCI_MAX_PORTS 32
#define AHCI_CMD_SLOT_COUNT 32
#define AHCI_PRD_COUNT 16
#define AHCI_CMD_LIST_SIZE 256
#define AHCI_FIS_SIZE 256
#define AHCI_SECTOR_SIZE 512

/* GHC bits */
#define AHCI_GHC_AE 0x80000000
#define AHCI_GHC_MRSM 0x00000002
#define AHCI_GHC_HR 0x00000001

/* Port interrupt bits */
#define AHCI_Px_IS_DHRS 0x00000001
#define AHCI_Px_IS_PSS 0x00000002
#define AHCI_Px_IS_DSS 0x00000004
#define AHCI_Px_IS_SDBS 0x00000008
#define AHCI_Px_IS_UFS 0x00000010
#define AHCI_Px_IS_DPS 0x00000020
#define AHCI_Px_IS_PCS 0x00000040
#define AHCI_Px_IS_PRCS 0x00000080
#define AHCI_Px_IS_IPMS 0x00000100
#define AHCI_Px_IS_OFS 0x00000200
#define AHCI_Px_IS_INFS 0x00000400
#define AHCI_Px_IS_IFS 0x00000800
#define AHCI_Px_IS_HBDS 0x00001000
#define AHCI_Px_IS_HBFS 0x00002000
#define AHCI_Px_IS_TFES 0x40000000
#define AHCI_Px_IS_CPDS 0x80000000

/* Command header flags */
#define AHCI_CMD_FIS_LEN 5
#define AHCI_CMD_WRITE 0x40
#define AHCI_CMD_PREFETCH 0x20
#define AHCI_CMD_RESET 0x08
#define AHCI_CMD_BIST 0x04
#define AHCI_CMD_CLR_BUSY 0x02

/* FIS types */
#define AHCI_FIS_TYPE_REG_H2D 0x27
#define AHCI_FIS_TYPE_REG_D2H 0x34
#define AHCI_FIS_TYPE_DMA_ACT 0x39
#define AHCI_FIS_TYPE_DMA_SETUP 0x41
#define AHCI_FIS_TYPE_DATA 0x46
#define AHCI_FIS_TYPE_BIST 0x58
#define AHCI_FIS_TYPE_PIO_SETUP 0x5F
#define AHCI_FIS_TYPE_DEV_BITS 0xA1

/* Command FIS */
struct ahci_cmd_fis {
    uint8_t fis_type;
    uint8_t pm_port;
    uint8_t command;
    uint8_t features;
    uint8_t lba_low;
    uint8_t lba_mid;
    uint8_t lba_high;
    uint8_t device;
    uint8_t lba_low_exp;
    uint8_t lba_mid_exp;
    uint8_t lba_high_exp;
    uint8_t features_exp;
    uint8_t sector_count;
    uint8_t sector_count_exp;
    uint8_t reserved;
    uint8_t control;
    uint8_t reserved2[4];
};

struct ahci_command_header {
    uint16_t prd_length;
    uint16_t command_flags;
    uint32_t prdt_offset;
    uint32_t status;
    uint32_t reserved[4];
};

struct ahci_prd {
    uint64_t data_base_addr;
    uint32_t reserved;
    uint32_t interrupt_on_completion;
};

struct ahci_port_priv {
    struct ahci_command_header *cmd_list;
    struct ahci_prd *prd_table;
    struct ahci_cmd_fis *cmd_fis;
    void *cmd_list_virt;
    void *prd_table_virt;
    void *fis_buffer_virt;
    uint64_t cmd_list_phys;
    uint64_t prd_table_phys;
    uint64_t fis_buffer_phys;
    uint32_t port_num;
    uint32_t cmd_slot;
    volatile void *port_base;
    int initialized;
};

struct ahci_priv {
    volatile void *mmio_base;
    struct ahci_port_priv ports[AHCI_MAX_PORTS];
    uint32_t port_count;
    uint64_t total_sectors;
    int irq_num;
    int use_interrupts;
};

static void ahci_irq_handler(void *ctx);

static int ahci_probe(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    if (!plat) return -1;

    struct ahci_priv *priv = plat->alloc(sizeof(struct ahci_priv));
    if (!priv) return -1;
    memset(priv, 0, sizeof(*priv));

    priv->mmio_base = dev->bars[5].virt_addr; /* ABAR is typically BAR 5 */

    /* Enable AHCI mode */
    uint32_t ghc = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + AHCI_GHC));
    ghc |= AHCI_GHC_AE;
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + AHCI_GHC), ghc);

    /* Check implemented ports */
    uint32_t pi = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + AHCI_PI));
    priv->port_count = 0;
    
    /* Initialize first implemented port */
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (AHCI_PORT_IMPLEMENTED(pi >> i)) {
            struct ahci_port_priv *port = &priv->ports[i];
            port->port_num = i;
            port->port_base = (volatile void *)((uint8_t *)priv->mmio_base + 0x100 + i * 0x80);
            port->cmd_slot = 0;
            
            /* Allocate command list */
            port->cmd_list_virt = plat->dma_alloc(AHCI_CMD_LIST_SIZE, &port->cmd_list_phys);
            if (!port->cmd_list_virt) goto cleanup;
            memset(port->cmd_list_virt, 0, AHCI_CMD_LIST_SIZE);
            port->cmd_list = (struct ahci_command_header *)port->cmd_list_virt;
            
            /* Allocate FIS receive buffer */
            port->fis_buffer_virt = plat->dma_alloc(AHCI_FIS_SIZE, &port->fis_buffer_phys);
            if (!port->fis_buffer_virt) goto cleanup;
            memset(port->fis_buffer_virt, 0, AHCI_FIS_SIZE);
            port->cmd_fis = (struct ahci_cmd_fis *)((uint8_t *)port->fis_buffer_virt + 0x40);
            
            /* Allocate PRD table */
            port->prd_table_virt = plat->dma_alloc(sizeof(struct ahci_prd) * AHCI_PRD_COUNT, &port->prd_table_phys);
            if (!port->prd_table_virt) goto cleanup;
            memset(port->prd_table_virt, 0, sizeof(struct ahci_prd) * AHCI_PRD_COUNT);
            port->prd_table = (struct ahci_prd *)port->prd_table_virt;
            
            /* Setup port registers */
            plat->mmio_write32((volatile void *)((uint8_t *)port->port_base + AHCI_Px_CLB), port->cmd_list_phys & 0xFFFFFFFF);
            plat->mmio_write32((volatile void *)((uint8_t *)port->port_base + AHCI_Px_CLBU), (port->cmd_list_phys >> 32) & 0xFFFFFFFF);
            plat->mmio_write32((volatile void *)((uint8_t *)port->port_base + AHCI_Px_FB), port->fis_buffer_phys & 0xFFFFFFFF);
            plat->mmio_write32((volatile void *)((uint8_t *)port->port_base + AHCI_PxFBU), (port->fis_buffer_phys >> 32) & 0xFFFFFFFF);
            
            /* Start command engine */
            uint32_t cmd = plat->mmio_read32((volatile void *)((uint8_t *)port->port_base + AHCI_Px_CMD));
            cmd |= AHCI_CMD_FRE | AHCI_CMD_ST;
            plat->mmio_write32((volatile void *)((uint8_t *)port->port_base + AHCI_Px_CMD), cmd);
            
            port->initialized = 1;
            priv->port_count++;
            break; /* Use first port only for simplicity */
        }
    }

    if (priv->port_count == 0) {
        plat->free(priv);
        return -1;
    }

    /* Try to register interrupt handler */
    priv->irq_num = dev->irq;
    priv->use_interrupts = 0;
    if (priv->irq_num >= 0) {
        int irq_result = plat->irq_request(priv->irq_num, ahci_irq_handler, dev);
        if (irq_result == 0) {
            priv->use_interrupts = 1;
            /* Enable port interrupts */
            for (int i = 0; i < AHCI_MAX_PORTS; i++) {
                if (priv->ports[i].initialized) {
                    plat->mmio_write32((volatile void *)((uint8_t *)priv->ports[i].port_base + AHCI_Px_IE), 
                                       AHCI_Px_IS_DHRS | AHCI_Px_IS_TFES | AHCI_Px_IS_CPDS);
                }
            }
            plat->log("ahci: interrupt handler registered for IRQ %d\n", priv->irq_num);
        } else {
            plat->log("ahci: interrupt registration failed, using polling\n");
        }
    } else {
        plat->log("ahci: no IRQ available, using polling\n");
    }

    dev->driver_data = priv;

    plat->log("ahci: probed device with %d ports\n", priv->port_count);
    return 0;

cleanup:
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        struct ahci_port_priv *port = &priv->ports[i];
        if (port->cmd_list_virt) plat->dma_free(port->cmd_list_virt, port->cmd_list_phys, AHCI_CMD_LIST_SIZE);
        if (port->prd_table_virt) plat->dma_free(port->prd_table_virt, port->prd_table_phys, sizeof(struct ahci_prd) * AHCI_PRD_COUNT);
        if (port->fis_buffer_virt) plat->dma_free(port->fis_buffer_virt, port->fis_buffer_phys, AHCI_FIS_SIZE);
    }
    plat->free(priv);
    return -1;
}

static void ahci_irq_handler(void *ctx)
{
    struct nc_device *dev = (struct nc_device *)ctx;
    struct ahci_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !plat) return;

    /* Check global interrupt status */
    uint32_t is = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + AHCI_IS));
    
    /* Check each port for interrupts */
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (priv->ports[i].initialized) {
            uint32_t px_is = plat->mmio_read32((volatile void *)((uint8_t *)priv->ports[i].port_base + AHCI_Px_IS));
            
            if (px_is) {
                /* Acknowledge port interrupt */
                plat->mmio_write32((volatile void *)((uint8_t *)priv->ports[i].port_base + AHCI_Px_IS), px_is);
                
                /* Handle task file error */
                if (px_is & AHCI_Px_IS_TFES) {
                    /* Task file error occurred */
                }
                
                /* Handle device notification */
                if (px_is & AHCI_Px_IS_DHRS) {
                    /* Device notification */
                }
                
                /* Handle cold presence detect */
                if (px_is & AHCI_Px_IS_CPDS) {
                    /* Cold presence detect */
                }
            }
        }
    }
    
    /* Acknowledge global interrupt */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + AHCI_IS), is);
}

static void ahci_remove(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    struct ahci_priv *priv = dev->driver_data;
    if (!plat || !priv) return;

    /* Disable port interrupts */
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (priv->ports[i].initialized) {
            plat->mmio_write32((volatile void *)((uint8_t *)priv->ports[i].port_base + AHCI_Px_IE), 0);
        }
    }
    
    /* Unregister interrupt handler if registered */
    if (priv->use_interrupts && priv->irq_num >= 0) {
        plat->irq_free(priv->irq_num);
    }

    /* Stop command engine on each port */
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        struct ahci_port_priv *port = &priv->ports[i];
        if (port->initialized) {
            uint32_t cmd = plat->mmio_read32((volatile void *)((uint8_t *)port->port_base + AHCI_Px_CMD));
            cmd &= ~(AHCI_CMD_FRE | AHCI_CMD_ST);
            plat->mmio_write32((volatile void *)((uint8_t *)port->port_base + AHCI_Px_CMD), cmd);
        }
    }

    /* Clean up each port */
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        struct ahci_port_priv *port = &priv->ports[i];
        if (port->initialized) {
            if (port->cmd_list_virt) plat->dma_free(port->cmd_list_virt, port->cmd_list_phys, AHCI_CMD_LIST_SIZE);
            if (port->prd_table_virt) plat->dma_free(port->prd_table_virt, port->prd_table_phys, sizeof(struct ahci_prd) * AHCI_PRD_COUNT);
            if (port->fis_buffer_virt) plat->dma_free(port->fis_buffer_virt, port->fis_buffer_phys, AHCI_FIS_SIZE);
        }
    }

    plat->free(priv);
    dev->driver_data = NULL;
}

static int ahci_read(struct nc_device *dev, uint64_t lba, void *buffer, uint32_t sectors)
{
    struct ahci_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !buffer || sectors == 0 || !plat) return -1;
    
    /* Security: validate sector count */
    if (sectors > 256) return -1; /* Maximum sectors per command */
    
    /* Security: validate LBA */
    if (lba > (1ULL << 48)) return -1; /* Maximum LBA for 48-bit addressing */

    /* Find initialized port */
    struct ahci_port_priv *port = NULL;
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (priv->ports[i].initialized) {
            port = &priv->ports[i];
            break;
        }
    }
    if (!port) return -1;

    uint32_t slot = port->cmd_slot;
    struct ahci_command_header *cmd_hdr = &port->cmd_list[slot];
    struct ahci_cmd_fis *fis = port->cmd_fis;
    
    /* Setup command FIS */
    memset(fis, 0, sizeof(*fis));
    fis->fis_type = AHCI_FIS_TYPE_REG_H2D;
    fis->command = 0x25; /* READ DMA EXT */
    fis->lba_low = lba & 0xFF;
    fis->lba_mid = (lba >> 8) & 0xFF;
    fis->lba_high = (lba >> 16) & 0xFF;
    fis->lba_low_exp = (lba >> 24) & 0xFF;
    fis->lba_mid_exp = (lba >> 32) & 0xFF;
    fis->lba_high_exp = (lba >> 40) & 0xFF;
    fis->device = 0x40; /* LBA mode */
    fis->sector_count = sectors & 0xFF;
    fis->sector_count_exp = (sectors >> 8) & 0xFF;
    
    /* Setup command header */
    cmd_hdr->prd_length = (sectors + AHCI_PRD_COUNT - 1) / AHCI_PRD_COUNT;
    cmd_hdr->command_flags = AHCI_CMD_FIS_LEN | (1 << 2); /* Clear busy on OK */
    cmd_hdr->prdt_offset = sizeof(struct ahci_command_header) * slot;
    
    /* Setup PRD table */
    uint64_t buffer_phys = (uint64_t)(uintptr_t)buffer;
    uint32_t bytes_remaining = sectors * AHCI_SECTOR_SIZE;
    for (uint32_t i = 0; i < cmd_hdr->prd_length && i < AHCI_PRD_COUNT; i++) {
        uint32_t chunk_size = bytes_remaining > 0x200000 ? 0x200000 : bytes_remaining;
        port->prd_table[i].data_base_addr = buffer_phys;
        uint32_t is_last = (i == (uint32_t)(cmd_hdr->prd_length - 1)) ? 0x80000000 : 0;
        port->prd_table[i].interrupt_on_completion = is_last;
        buffer_phys += chunk_size;
        bytes_remaining -= chunk_size;
    }
    
    __sync_synchronize();
    
    /* Issue command */
    plat->mmio_write32((volatile void *)((uint8_t *)port->port_base + AHCI_Px_CI), 1 << slot);
    
    /* Wait for completion (polling) */
    uint32_t timeout = 10000;
    while (timeout--) {
        uint32_t ci = plat->mmio_read32((volatile void *)((uint8_t *)port->port_base + AHCI_Px_CI));
        if (!(ci & (1 << slot))) break;
        plat->sleep_ms(1);
    }
    
    if (timeout == 0) return -1;
    
    return (int)sectors;
}

static int ahci_write(struct nc_device *dev, uint64_t lba, const void *buffer, uint32_t sectors)
{
    struct ahci_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !buffer || sectors == 0 || !plat) return -1;
    
    /* Security: validate sector count */
    if (sectors > 256) return -1; /* Maximum sectors per command */
    
    /* Security: validate LBA */
    if (lba > (1ULL << 48)) return -1; /* Maximum LBA for 48-bit addressing */

    /* Find initialized port */
    struct ahci_port_priv *port = NULL;
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (priv->ports[i].initialized) {
            port = &priv->ports[i];
            break;
        }
    }
    if (!port) return -1;

    uint32_t slot = port->cmd_slot;
    struct ahci_command_header *cmd_hdr = &port->cmd_list[slot];
    struct ahci_cmd_fis *fis = port->cmd_fis;
    
    /* Setup command FIS */
    memset(fis, 0, sizeof(*fis));
    fis->fis_type = AHCI_FIS_TYPE_REG_H2D;
    fis->command = 0x35; /* WRITE DMA EXT */
    fis->lba_low = lba & 0xFF;
    fis->lba_mid = (lba >> 8) & 0xFF;
    fis->lba_high = (lba >> 16) & 0xFF;
    fis->lba_low_exp = (lba >> 24) & 0xFF;
    fis->lba_mid_exp = (lba >> 32) & 0xFF;
    fis->lba_high_exp = (lba >> 40) & 0xFF;
    fis->device = 0x40; /* LBA mode */
    fis->sector_count = sectors & 0xFF;
    fis->sector_count_exp = (sectors >> 8) & 0xFF;
    
    /* Setup command header */
    cmd_hdr->prd_length = (sectors + AHCI_PRD_COUNT - 1) / AHCI_PRD_COUNT;
    cmd_hdr->command_flags = AHCI_CMD_FIS_LEN | AHCI_CMD_WRITE | (1 << 2);
    cmd_hdr->prdt_offset = sizeof(struct ahci_command_header) * slot;
    
    /* Setup PRD table */
    uint64_t buffer_phys = (uint64_t)(uintptr_t)buffer;
    uint32_t bytes_remaining = sectors * AHCI_SECTOR_SIZE;
    for (uint32_t i = 0; i < cmd_hdr->prd_length && i < AHCI_PRD_COUNT; i++) {
        uint32_t chunk_size = bytes_remaining > 0x200000 ? 0x200000 : bytes_remaining;
        port->prd_table[i].data_base_addr = buffer_phys;
        uint32_t is_last = (i == (uint32_t)(cmd_hdr->prd_length - 1)) ? 0x80000000 : 0;
        port->prd_table[i].interrupt_on_completion = is_last;
        buffer_phys += chunk_size;
        bytes_remaining -= chunk_size;
    }
    
    __sync_synchronize();
    
    /* Issue command */
    plat->mmio_write32((volatile void *)((uint8_t *)port->port_base + AHCI_Px_CI), 1 << slot);
    
    /* Wait for completion (polling) */
    uint32_t timeout = 10000;
    while (timeout--) {
        uint32_t ci = plat->mmio_read32((volatile void *)((uint8_t *)port->port_base + AHCI_Px_CI));
        if (!(ci & (1 << slot))) break;
        plat->sleep_ms(1);
    }
    
    if (timeout == 0) return -1;
    
    return (int)sectors;
}

static int ahci_flush(struct nc_device *dev)
{
    struct ahci_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !plat) return -1;

    /* Find initialized port */
    struct ahci_port_priv *port = NULL;
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (priv->ports[i].initialized) {
            port = &priv->ports[i];
            break;
        }
    }
    if (!port) return -1;

    uint32_t slot = port->cmd_slot;
    struct ahci_command_header *cmd_hdr = &port->cmd_list[slot];
    struct ahci_cmd_fis *fis = port->cmd_fis;
    
    /* Setup flush command FIS */
    memset(fis, 0, sizeof(*fis));
    fis->fis_type = AHCI_FIS_TYPE_REG_H2D;
    fis->command = 0xE7; /* FLUSH CACHE EXT */
    
    /* Setup command header */
    cmd_hdr->prd_length = 0;
    cmd_hdr->command_flags = AHCI_CMD_FIS_LEN | (1 << 2);
    cmd_hdr->prdt_offset = 0;
    
    __sync_synchronize();
    
    /* Issue command */
    plat->mmio_write32((volatile void *)((uint8_t *)port->port_base + AHCI_Px_CI), 1 << slot);
    
    /* Wait for completion */
    uint32_t timeout = 10000;
    while (timeout--) {
        uint32_t ci = plat->mmio_read32((volatile void *)((uint8_t *)port->port_base + AHCI_Px_CI));
        if (!(ci & (1 << slot))) break;
        plat->sleep_ms(1);
    }
    
    return timeout == 0 ? -1 : 0;
}

static uint64_t ahci_get_capacity(struct nc_device *dev)
{
    struct ahci_priv *priv = dev->driver_data;
    if (!priv) return 0;
    /* TODO: identify device and return capacity */
    return priv->total_sectors;
}

struct nc_storage_driver nc_ahci_driver = {
    .name = "ahci",
    .probe = ahci_probe,
    .remove = ahci_remove,
    .read = ahci_read,
    .write = ahci_write,
    .flush = ahci_flush,
    .get_capacity = ahci_get_capacity,
};
