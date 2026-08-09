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
    void *fis_buffer;
    void *cmd_list_virt;
    void *prd_table_virt;
    void *fis_buffer_virt;
    uint64_t cmd_list_phys;
    uint64_t prd_table_phys;
    uint64_t fis_buffer_phys;
    uint32_t port_num;
    int initialized;
};

struct ahci_priv {
    volatile void *mmio_base;
    struct ahci_port_priv ports[AHCI_MAX_PORTS];
    uint32_t port_count;
    uint64_t total_sectors;
};

static int ahci_probe(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    if (!plat) return -1;

    /* Check class code for SATA controller */
    uint8_t class_code = (dev->device_id >> 8) & 0xFF;
    if (class_code != AHCI_CLASS_CODE && dev->vendor_id != AHCI_VENDOR_ID_ANY) {
        return -1;
    }

    struct ahci_priv *priv = plat->alloc(sizeof(struct ahci_priv));
    if (!priv) return -1;
    memset(priv, 0, sizeof(*priv));

    priv->mmio_base = dev->bars[5].virt_addr; /* ABAR is typically BAR 5 */

    /* Check implemented ports */
    uint32_t pi = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + AHCI_PI));
    priv->port_count = 0;
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (AHCI_PORT_IMPLEMENTED(pi >> i)) {
            priv->port_count++;
        }
    }

    if (priv->port_count == 0) {
        plat->free(priv);
        return -1;
    }

    dev->driver_data = priv;

    plat->log("ahci: probed device with %d ports\n", priv->port_count);
    return 0;
}

static void ahci_remove(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    struct ahci_priv *priv = dev->driver_data;
    if (!plat || !priv) return;

    /* Clean up each port */
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        struct ahci_port_priv *port = &priv->ports[i];
        if (port->initialized) {
            if (port->cmd_list_virt) plat->dma_free(port->cmd_list_virt, port->cmd_list_phys, 256);
            if (port->prd_table_virt) plat->dma_free(port->prd_table_virt, port->prd_table_phys, sizeof(struct ahci_prd) * AHCI_PRD_COUNT);
            if (port->fis_buffer_virt) plat->dma_free(port->fis_buffer_virt, port->fis_buffer_phys, 256);
        }
    }

    plat->free(priv);
    dev->driver_data = NULL;
}

static int ahci_read(struct nc_device *dev, uint64_t lba, void *buffer, uint32_t sectors)
{
    struct ahci_priv *priv = dev->driver_data;
    if (!priv || !buffer || sectors == 0) return -1;
    /* TODO: implement read operation with command list and PRD */
    (void)lba;
    return -1;
}

static int ahci_write(struct nc_device *dev, uint64_t lba, const void *buffer, uint32_t sectors)
{
    struct ahci_priv *priv = dev->driver_data;
    if (!priv || !buffer || sectors == 0) return -1;
    /* TODO: implement write operation with command list and PRD */
    (void)lba;
    return -1;
}

static int ahci_flush(struct nc_device *dev)
{
    struct ahci_priv *priv = dev->driver_data;
    if (!priv) return -1;
    /* TODO: implement cache flush */
    return -1;
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
