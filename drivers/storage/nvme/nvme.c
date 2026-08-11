#include "nvme.h"
#include "nc/platform.h"
#include <string.h>

#define NVME_CLASS_CODE 0x0108
#define NVME_VENDOR_ID_ANY 0xFFFF

/* NVMe controller registers */
#define NVME_CAP 0x0000
#define NVME_VS 0x0008
#define NVME_CC 0x0014
#define NVME_CSTS 0x001C
#define NVME_AQA 0x0024
#define NVME_ASQ 0x0028
#define NVME_ACQ 0x0030
#define NVME_CMBLOC 0x0038
#define NVME_CMBSZ 0x003C

/* Queue registers */
#define NVME_SQ0TDBL 0x1000
#define NVME_CQ0HDBL 0x1004
#define NVME_SQ1TDBL 0x1040
#define NVME_CQ1HDBL 0x1044

/* CAP bits */
#define NVME_CAP_MQES_SHIFT 0
#define NVME_CAP_MQES_MASK 0xFFFF
#define NVME_CAP_CQR 0x00010000
#define NVME_CAP_AMS_SHIFT 17
#define NVME_CAP_AMS_MASK 0x000E0000
#define NVME_CAP_TO_SHIFT 24
#define NVME_CAP_TO_MASK 0x3F000000
#define NVME_CAP_DSTRD_SHIFT 32
#define NVME_CAP_DSTRD_MASK 0x000F0000
#define NVME_CAP_NSSRS_SHIFT 33
#define NVME_CAP_NSSRS_MASK 0x00020000
#define NVME_CAP_CSS_SHIFT 37
#define NVME_CAP_CSS_MASK 0x00780000
#define NVME_CAP_MPSMIN_SHIFT 48
#define NVME_CAP_MPSMIN_MASK 0x000F0000
#define NVME_CAP_MPSMAX_SHIFT 52
#define NVME_CAP_MPSMAX_MASK 0x000F0000

/* CC bits */
#define NVME_CC_EN 0x00000001
#define NVME_CC_CSS_SHIFT 4
#define NVME_CC_CSS_MASK 0x00000070
#define NVME_CC_MPS_SHIFT 7
#define NVME_CC_MPS_MASK 0x00000380
#define NVME_CC_AMS_SHIFT 11
#define NVME_CC_AMS_MASK 0x00003800
#define NVME_CC_SHN_SHIFT 14
#define NVME_CC_SHN_MASK 0x0000C000
#define NVME_CC_IOSQES_SHIFT 16
#define NVME_CC_IOSQES_MASK 0x000F0000
#define NVME_CC_IOCQES_SHIFT 20
#define NVME_CC_IOCQES_MASK 0x00F00000

/* CSTS bits */
#define NVME_CSTS_RDY 0x00000001
#define NVME_CSTS_CFS 0x00000002
#define NVME_CSTS_SHST_SHIFT 2
#define NVME_CSTS_SHST_MASK 0x0000000C

/* AQA bits */
#define NVME_AQA_ASQS_SHIFT 0
#define NVME_AQA_ASQS_MASK 0x00000FFF
#define NVME_AQA_ACQS_SHIFT 16
#define NVME_AQA_ACQS_MASK 0x0FFF0000

/* Command opcodes */
#define NVME_CMD_FLUSH 0x00
#define NVME_CMD_WRITE 0x01
#define NVME_CMD_READ 0x02
#define NVME_CMD_WRITE_UNCORRECTABLE 0x04
#define NVME_CMD_COMPARE 0x05
#define NVME_CMD_WRITE_ZEROES 0x08
#define NVME_CMD_DATASET_MANAGEMENT 0x09
#define NVME_CMD_RESERVATION_REGISTER 0x0D
#define NVME_CMD_RESERVATION_REPORT 0x0E
#define NVME_CMD_RESERVATION_ACQUIRE 0x11
#define NVME_CMD_RESERVATION_RELEASE 0x15

#define NVME_QUEUE_SIZE 256
#define NVME_PRP_OFFSET 8
#define NVME_PRP_ENTRY_SIZE 8
#define NVME_PAGE_SIZE 4096

struct nvme_command {
    uint32_t cdw0;
    uint32_t nsid;
    uint64_t rsvd1;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
};

struct nvme_completion {
    uint32_t result0;
    uint32_t result1;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cmd_id;
    uint16_t status;
};

struct nvme_sq_entry {
    struct nvme_command cmd;
};

struct nvme_cq_entry {
    struct nvme_completion cqe;
};

struct nvme_priv {
    volatile void *mmio_base;
    struct nvme_sq_entry *submission_queue;
    struct nvme_cq_entry *completion_queue;
    uint64_t sq_phys;
    uint64_t cq_phys;
    uint16_t sq_head;
    uint16_t sq_tail;
    uint16_t cq_head;
    uint16_t cq_tail;
    uint32_t nsid;
    uint64_t total_sectors;
    uint32_t page_size;
    int initialized;
    int irq_num;
    int use_interrupts;
};

static void nvme_irq_handler(void *ctx);

static int nvme_probe(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    if (!plat) return -1;

    struct nvme_priv *priv = plat->alloc(sizeof(struct nvme_priv));
    if (!priv) return -1;
    memset(priv, 0, sizeof(*priv));

    priv->mmio_base = dev->bars[0].virt_addr;
    priv->page_size = NVME_PAGE_SIZE;
    priv->sq_head = 0;
    priv->sq_tail = 0;
    priv->cq_head = 0;
    priv->cq_tail = 0;
    priv->initialized = 0;

    /* Read controller capabilities */
    uint64_t cap = plat->mmio_read64((volatile void *)((uint8_t *)priv->mmio_base + NVME_CAP));
    
    /* Validate queue size from capabilities */
    uint32_t max_queues = (cap >> NVME_CAP_MQES_SHIFT) & NVME_CAP_MQES_MASK;
    if (NVME_QUEUE_SIZE > max_queues) {
        plat->log("nvme: requested queue size %d exceeds max %d\n", NVME_QUEUE_SIZE, max_queues);
        plat->free(priv);
        return -1;
    }
    
    /* Check if controller is ready */
    uint32_t csts = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + NVME_CSTS));
    if (!(csts & NVME_CSTS_RDY)) {
        plat->log("nvme: controller not ready\n");
        plat->free(priv);
        return -1;
    }

    /* Disable controller */
    uint32_t cc = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + NVME_CC));
    cc &= ~NVME_CC_EN;
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + NVME_CC), cc);

    /* Wait for controller to disable */
    uint32_t timeout = 10000;
    while (timeout--) {
        csts = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + NVME_CSTS));
        if (!(csts & NVME_CSTS_RDY)) break;
        plat->sleep_ms(1);
    }

    if (timeout == 0) {
        plat->log("nvme: failed to disable controller\n");
        plat->free(priv);
        return -1;
    }

    /* Allocate submission queue */
    size_t sq_size = sizeof(struct nvme_sq_entry) * NVME_QUEUE_SIZE;
    priv->submission_queue = plat->dma_alloc(sq_size, &priv->sq_phys);
    if (!priv->submission_queue) {
        plat->free(priv);
        return -1;
    }
    memset(priv->submission_queue, 0, sq_size);

    /* Allocate completion queue */
    size_t cq_size = sizeof(struct nvme_cq_entry) * NVME_QUEUE_SIZE;
    priv->completion_queue = plat->dma_alloc(cq_size, &priv->cq_phys);
    if (!priv->completion_queue) {
        plat->dma_free(priv->submission_queue, priv->sq_phys, sq_size);
        plat->free(priv);
        return -1;
    }
    memset(priv->completion_queue, 0, cq_size);

    /* Setup admin queue attributes */
    uint32_t aqa = ((NVME_QUEUE_SIZE - 1) << NVME_AQA_ASQS_SHIFT) | 
                   ((NVME_QUEUE_SIZE - 1) << NVME_AQA_ACQS_SHIFT);
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + NVME_AQA), aqa);

    /* Setup admin queue addresses */
    plat->mmio_write64((volatile void *)((uint8_t *)priv->mmio_base + NVME_ASQ), priv->sq_phys);
    plat->mmio_write64((volatile void *)((uint8_t *)priv->mmio_base + NVME_ACQ), priv->cq_phys);

    /* Configure controller */
    cc = (0 << NVME_CC_IOSQES_SHIFT) | (0 << NVME_CC_IOCQES_SHIFT) | 
         (0 << NVME_CC_SHN_SHIFT) | (0 << NVME_CC_AMS_SHIFT) | 
         (0 << NVME_CC_MPS_SHIFT) | (0 << NVME_CC_CSS_SHIFT) | NVME_CC_EN;
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + NVME_CC), cc);

    /* Wait for controller to enable */
    timeout = 10000;
    while (timeout--) {
        csts = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + NVME_CSTS));
        if (csts & NVME_CSTS_RDY) break;
        plat->sleep_ms(1);
    }

    if (timeout == 0) {
        plat->log("nvme: failed to enable controller\n");
        plat->dma_free(priv->submission_queue, priv->sq_phys, sq_size);
        plat->dma_free(priv->completion_queue, priv->cq_phys, cq_size);
        plat->free(priv);
        return -1;
    }

    /* Set namespace ID to 1 (first namespace) */
    priv->nsid = 1;
    
    /* Try to register interrupt handler */
    priv->irq_num = dev->irq;
    priv->use_interrupts = 0;
    if (priv->irq_num >= 0) {
        int irq_result = plat->irq_request(priv->irq_num, nvme_irq_handler, dev);
        if (irq_result == 0) {
            priv->use_interrupts = 1;
            plat->log("nvme: interrupt handler registered for IRQ %d\n", priv->irq_num);
        } else {
            plat->log("nvme: interrupt registration failed, using polling\n");
        }
    } else {
        plat->log("nvme: no IRQ available, using polling\n");
    }

    priv->initialized = 1;
    dev->driver_data = priv;

    plat->log("nvme: probed device\n");
    return 0;
}

static void nvme_irq_handler(void *ctx)
{
    struct nc_device *dev = (struct nc_device *)ctx;
    struct nvme_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !plat) return;

    /* Check completion queue */
    __sync_synchronize();
    
    uint16_t cq_head = priv->cq_head;
    struct nvme_cq_entry *cqe = &priv->completion_queue[cq_head % NVME_QUEUE_SIZE];
    
    if (cqe->cqe.status != 0) {
        /* Completion available */
        priv->cq_head = (cq_head + 1) % NVME_QUEUE_SIZE;
        
        /* Update doorbell */
        plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + NVME_CQ0HDBL), priv->cq_head);
    }
}

static void nvme_remove(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    struct nvme_priv *priv = dev->driver_data;
    if (!plat || !priv) return;

    /* Disable controller */
    uint32_t cc = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + NVME_CC));
    cc &= ~NVME_CC_EN;
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + NVME_CC), cc);

    /* Unregister interrupt handler if registered */
    if (priv->use_interrupts && priv->irq_num >= 0) {
        plat->irq_free(priv->irq_num);
    }

    /* Free queues */
    if (priv->submission_queue) plat->dma_free(priv->submission_queue, priv->sq_phys, sizeof(struct nvme_sq_entry) * NVME_QUEUE_SIZE);
    if (priv->completion_queue) plat->dma_free(priv->completion_queue, priv->cq_phys, sizeof(struct nvme_cq_entry) * NVME_QUEUE_SIZE);

    plat->free(priv);
    dev->driver_data = NULL;
}

static int nvme_read(struct nc_device *dev, uint64_t lba, void *buffer, uint32_t sectors)
{
    struct nvme_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !buffer || sectors == 0 || !plat) return -1;
    
    /* Security: validate sector count */
    if (sectors > 256) return -1;
    
    /* Security: validate LBA */
    if (lba > (1ULL << 48)) return -1;

    uint16_t sq_tail = priv->sq_tail;
    struct nvme_sq_entry *sqe = &priv->submission_queue[sq_tail % NVME_QUEUE_SIZE];
    
    memset(&sqe->cmd, 0, sizeof(sqe->cmd));
    sqe->cmd.cdw0 = NVME_CMD_READ;
    sqe->cmd.nsid = priv->nsid;
    sqe->cmd.prp1 = (uint64_t)(uintptr_t)buffer;
    sqe->cmd.cdw10 = (uint32_t)lba;
    sqe->cmd.cdw11 = (uint32_t)(lba >> 32);
    sqe->cmd.cdw12 = (sectors - 1) & 0xFFFF;
    
    __sync_synchronize();
    
    priv->sq_tail++;
    
    /* Update submission queue doorbell */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + NVME_SQ0TDBL), priv->sq_tail);
    
    /* Wait for completion (polling) */
    uint32_t timeout = 10000;
    while (timeout--) {
        __sync_synchronize();
        
        uint16_t cq_head = priv->cq_head;
        struct nvme_cq_entry *cqe = &priv->completion_queue[cq_head % NVME_QUEUE_SIZE];
        
        if (cqe->cqe.status != 0) {
            priv->cq_head = (cq_head + 1) % NVME_QUEUE_SIZE;
            plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + NVME_CQ0HDBL), priv->cq_head);
            
            if ((cqe->cqe.status >> 1) != 0) {
                plat->log("nvme: read error, status=0x%x\n", cqe->cqe.status);
                return -1;
            }
            return (int)sectors;
        }
        plat->sleep_ms(1);
    }
    
    return -1;
}

static int nvme_write(struct nc_device *dev, uint64_t lba, const void *buffer, uint32_t sectors)
{
    struct nvme_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !buffer || sectors == 0 || !plat) return -1;
    
    /* Security: validate sector count */
    if (sectors > 256) return -1;
    
    /* Security: validate LBA */
    if (lba > (1ULL << 48)) return -1;

    uint16_t sq_tail = priv->sq_tail;
    struct nvme_sq_entry *sqe = &priv->submission_queue[sq_tail % NVME_QUEUE_SIZE];
    
    memset(&sqe->cmd, 0, sizeof(sqe->cmd));
    sqe->cmd.cdw0 = NVME_CMD_WRITE;
    sqe->cmd.nsid = priv->nsid;
    sqe->cmd.prp1 = (uint64_t)(uintptr_t)buffer;
    sqe->cmd.cdw10 = (uint32_t)lba;
    sqe->cmd.cdw11 = (uint32_t)(lba >> 32);
    sqe->cmd.cdw12 = (sectors - 1) & 0xFFFF;
    
    __sync_synchronize();
    
    priv->sq_tail++;
    
    /* Update submission queue doorbell */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + NVME_SQ0TDBL), priv->sq_tail);
    
    /* Wait for completion (polling) */
    uint32_t timeout = 10000;
    while (timeout--) {
        __sync_synchronize();
        
        uint16_t cq_head = priv->cq_head;
        struct nvme_cq_entry *cqe = &priv->completion_queue[cq_head % NVME_QUEUE_SIZE];
        
        if (cqe->cqe.status != 0) {
            priv->cq_head = (cq_head + 1) % NVME_QUEUE_SIZE;
            plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + NVME_CQ0HDBL), priv->cq_head);
            
            if ((cqe->cqe.status >> 1) != 0) {
                plat->log("nvme: write error, status=0x%x\n", cqe->cqe.status);
                return -1;
            }
            return (int)sectors;
        }
        plat->sleep_ms(1);
    }
    
    return -1;
}

static int nvme_flush(struct nc_device *dev)
{
    struct nvme_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !plat) return -1;

    uint16_t sq_tail = priv->sq_tail;
    struct nvme_sq_entry *sqe = &priv->submission_queue[sq_tail % NVME_QUEUE_SIZE];
    
    memset(&sqe->cmd, 0, sizeof(sqe->cmd));
    sqe->cmd.cdw0 = NVME_CMD_FLUSH;
    sqe->cmd.nsid = priv->nsid;
    
    __sync_synchronize();
    
    priv->sq_tail++;
    
    /* Update submission queue doorbell */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + NVME_SQ0TDBL), priv->sq_tail);
    
    /* Wait for completion */
    uint32_t timeout = 10000;
    while (timeout--) {
        __sync_synchronize();
        
        uint16_t cq_head = priv->cq_head;
        struct nvme_cq_entry *cqe = &priv->completion_queue[cq_head % NVME_QUEUE_SIZE];
        
        if (cqe->cqe.status != 0) {
            priv->cq_head = (cq_head + 1) % NVME_QUEUE_SIZE;
            plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + NVME_CQ0HDBL), priv->cq_head);
            
            if ((cqe->cqe.status >> 1) != 0) {
                return -1;
            }
            return 0;
        }
        plat->sleep_ms(1);
    }
    
    return -1;
}

static uint64_t nvme_get_capacity(struct nc_device *dev)
{
    struct nvme_priv *priv = dev->driver_data;
    if (!priv) return 0;
    /* TODO: identify namespace and return capacity */
    return priv->total_sectors;
}

struct nc_storage_driver nc_nvme_driver = {
    .name = "nvme",
    .probe = nvme_probe,
    .remove = nvme_remove,
    .read = nvme_read,
    .write = nvme_write,
    .flush = nvme_flush,
    .get_capacity = nvme_get_capacity,
};
