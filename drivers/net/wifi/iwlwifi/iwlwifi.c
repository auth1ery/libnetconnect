#include "iwlwifi.h"
#include "nc/platform.h"
#include <string.h>

#define INTEL_VENDOR_ID 0x8086

/* Supported device IDs for common iwlwifi chipsets */
#define IWL_DEVICE_22000 0x0000
#define IWL_DEVICE_9260 0x2526
#define IWL_DEVICE_8265 0x24fd
#define IWL_DEVICE_7265 0x095a
#define IWL_DEVICE_3165 0x3165

#define IWL_PRPH_SCRATCH 0xA02A74
#define IWL_CSR_GP1 0x000024
#define IWL_CSR_RESET 0x000028
#define IWL_CSR_INT 0x000008
#define IWL_CSR_INT_MASK 0x00000C
#define IWL_CSR_FH_INT_STATUS 0x000010
#define IWL_FH_TCSR_CHICKEN_REG 0x0002E0
#define IWL_FH_TFDIB_CTRL0 0x0002D0
#define IWL_FH_TFDIB_CTRL1 0x0002D4
#define IWL_FH_KW_MEM_ADDR_REG 0x0002A0

/* Interrupt bits */
#define IWL_INT_RX 0x00000001
#define IWL_INT_TX 0x00000002
#define IWL_INT_ALIVE 0x00000004
#define IWL_INT_WAKEUP 0x00000008
#define IWL_INT_ERROR 0x00000010

#define IWL_TX_QUEUE_SIZE 256
#define IWL_RX_QUEUE_SIZE 256
#define IWL_CMD_QUEUE_SIZE 32

/* Command IDs */
#define IWL_CMD_TX 0x1F
#define IWL_CMD_RX 0x2A
#define IWL_CMD_SCAN 0x16
#define IWL_CMD_ASSOCIATE 0x24
#define IWL_CMD_DISASSOCIATE 0x2B

struct iwlwifi_cmd {
    uint32_t cmd_id;
    uint32_t len;
    uint8_t data[256];
};

struct iwlwifi_tx_desc {
    uint32_t addr;
    uint32_t len;
    uint8_t cmd;
    uint8_t flags;
};

struct iwlwifi_priv {
    uint8_t mac[6];
    volatile void *mmio_base;
    void *firmware;
    size_t firmware_len;
    struct iwlwifi_cmd *cmd_queue;
    void *tx_ring;
    void *rx_ring;
    uint64_t cmd_queue_phys;
    uint64_t tx_ring_phys;
    uint64_t rx_ring_phys;
    uint16_t tx_head;
    uint16_t tx_tail;
    uint16_t rx_head;
    uint16_t rx_tail;
    uint16_t cmd_head;
    uint16_t cmd_tail;
    int initialized;
    int firmware_loaded;
    int irq_num;
    int use_interrupts;
};

static void iwlwifi_irq_handler(void *ctx);

static int iwlwifi_probe(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    if (!plat) return -1;

    if (dev->vendor_id != INTEL_VENDOR_ID) return -1;
    
    /* Check for supported device IDs */
    switch (dev->device_id) {
        case IWL_DEVICE_22000:
        case IWL_DEVICE_9260:
        case IWL_DEVICE_8265:
        case IWL_DEVICE_7265:
        case IWL_DEVICE_3165:
            break;
        default:
            return -1;
    }

    struct iwlwifi_priv *priv = plat->alloc(sizeof(struct iwlwifi_priv));
    if (!priv) return -1;
    memset(priv, 0, sizeof(*priv));

    priv->mmio_base = dev->bars[0].virt_addr;
    priv->tx_head = 0;
    priv->tx_tail = 0;
    priv->rx_head = 0;
    priv->rx_tail = 0;
    priv->cmd_head = 0;
    priv->cmd_tail = 0;
    priv->initialized = 0;
    priv->firmware_loaded = 0;

    /* Load firmware */
    const char *fw_name = "iwlwifi-";
    int fw_result = plat->firmware_load(fw_name, &priv->firmware, &priv->firmware_len);
    if (fw_result != 0) {
        plat->log("iwlwifi: firmware load failed, continuing without firmware\n");
    } else {
        priv->firmware_loaded = 1;
        plat->log("iwlwifi: firmware loaded (%zu bytes)\n", priv->firmware_len);
    }

    /* Allocate command queue */
    priv->cmd_queue = plat->dma_alloc(sizeof(struct iwlwifi_cmd) * IWL_CMD_QUEUE_SIZE, &priv->cmd_queue_phys);
    if (!priv->cmd_queue) {
        if (priv->firmware) plat->firmware_free(priv->firmware);
        plat->free(priv);
        return -1;
    }
    memset(priv->cmd_queue, 0, sizeof(struct iwlwifi_cmd) * IWL_CMD_QUEUE_SIZE);

    /* Allocate TX ring */
    priv->tx_ring = plat->dma_alloc(sizeof(struct iwlwifi_tx_desc) * IWL_TX_QUEUE_SIZE, &priv->tx_ring_phys);
    if (!priv->tx_ring) {
        plat->dma_free(priv->cmd_queue, priv->cmd_queue_phys, sizeof(struct iwlwifi_cmd) * IWL_CMD_QUEUE_SIZE);
        if (priv->firmware) plat->firmware_free(priv->firmware);
        plat->free(priv);
        return -1;
    }
    memset(priv->tx_ring, 0, sizeof(struct iwlwifi_tx_desc) * IWL_TX_QUEUE_SIZE);

    /* Allocate RX ring */
    priv->rx_ring = plat->dma_alloc(IWL_RX_QUEUE_SIZE * 2048, &priv->rx_ring_phys);
    if (!priv->rx_ring) {
        plat->dma_free(priv->tx_ring, priv->tx_ring_phys, sizeof(struct iwlwifi_tx_desc) * IWL_TX_QUEUE_SIZE);
        plat->dma_free(priv->cmd_queue, priv->cmd_queue_phys, sizeof(struct iwlwifi_cmd) * IWL_CMD_QUEUE_SIZE);
        if (priv->firmware) plat->firmware_free(priv->firmware);
        plat->free(priv);
        return -1;
    }
    memset(priv->rx_ring, 0, IWL_RX_QUEUE_SIZE * 2048);

    /* Read MAC address from EEPROM (simplified) */
    uint32_t mac_low = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + IWL_PRPH_SCRATCH));
    uint32_t mac_high = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + IWL_PRPH_SCRATCH + 4));
    priv->mac[0] = mac_low & 0xFF;
    priv->mac[1] = (mac_low >> 8) & 0xFF;
    priv->mac[2] = (mac_low >> 16) & 0xFF;
    priv->mac[3] = mac_low >> 24;
    priv->mac[4] = mac_high & 0xFF;
    priv->mac[5] = (mac_high >> 8) & 0xFF;

    /* Try to register interrupt handler */
    priv->irq_num = dev->irq;
    priv->use_interrupts = 0;
    if (priv->irq_num >= 0) {
        int irq_result = plat->irq_request(priv->irq_num, iwlwifi_irq_handler, dev);
        if (irq_result == 0) {
            priv->use_interrupts = 1;
            /* Enable interrupts */
            plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + IWL_CSR_INT_MASK), 
                               IWL_INT_RX | IWL_INT_TX | IWL_INT_ERROR);
            plat->log("iwlwifi: interrupt handler registered for IRQ %d\n", priv->irq_num);
        } else {
            plat->log("iwlwifi: interrupt registration failed, using polling\n");
        }
    } else {
        plat->log("iwlwifi: no IRQ available, using polling\n");
    }

    priv->initialized = 1;
    dev->driver_data = priv;

    plat->log("iwlwifi: probed device\n");
    return 0;
}

static void iwlwifi_irq_handler(void *ctx)
{
    struct nc_device *dev = (struct nc_device *)ctx;
    struct iwlwifi_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !plat) return;

    /* Read interrupt status */
    uint32_t status = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + IWL_CSR_INT));
    
    /* Acknowledge interrupt */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + IWL_CSR_INT), status);
    
    /* Handle RX interrupt */
    if (status & IWL_INT_RX) {
        /* RX processing will be done in recv() */
    }
    
    /* Handle TX interrupt */
    if (status & IWL_INT_TX) {
        /* TX completion will be handled in next send() */
    }
    
    /* Handle error interrupt */
    if (status & IWL_INT_ERROR) {
        /* Error occurred */
    }
    
    /* Handle alive interrupt (firmware ready) */
    if (status & IWL_INT_ALIVE) {
        /* Firmware is alive */
    }
}

static void iwlwifi_remove(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    struct iwlwifi_priv *priv = dev->driver_data;
    if (!plat || !priv) return;
    
    /* Disable interrupts */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + IWL_CSR_INT_MASK), 0xFFFFFFFF);
    
    /* Unregister interrupt handler if registered */
    if (priv->use_interrupts && priv->irq_num >= 0) {
        plat->irq_free(priv->irq_num);
    }
    
    if (priv->cmd_queue) plat->dma_free(priv->cmd_queue, priv->cmd_queue_phys, sizeof(struct iwlwifi_cmd) * IWL_CMD_QUEUE_SIZE);
    if (priv->tx_ring) plat->dma_free(priv->tx_ring, priv->tx_ring_phys, sizeof(struct iwlwifi_tx_desc) * IWL_TX_QUEUE_SIZE);
    if (priv->rx_ring) plat->dma_free(priv->rx_ring, priv->rx_ring_phys, IWL_RX_QUEUE_SIZE * 2048);
    if (priv->firmware) plat->firmware_free(priv->firmware);
    
    plat->free(priv);
    dev->driver_data = NULL;
}

static int iwlwifi_send(struct nc_device *dev, const uint8_t *frame, size_t len)
{
    struct iwlwifi_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !frame || len == 0 || !plat || !priv->initialized) return -1;
    
    /* Security: validate frame length */
    if (len > 2304) return -1; /* Maximum WiFi frame size */

    if (!priv->firmware_loaded) {
        plat->log("iwlwifi: cannot send without firmware\n");
        return -1;
    }

    uint16_t idx = priv->tx_tail % IWL_TX_QUEUE_SIZE;
    struct iwlwifi_tx_desc *desc = (struct iwlwifi_tx_desc *)priv->tx_ring + idx;
    
    desc->addr = (uint64_t)(uintptr_t)frame;
    desc->len = (uint32_t)len;
    desc->cmd = IWL_CMD_TX;
    desc->flags = 0x01;
    
    __sync_synchronize();
    
    priv->tx_tail++;
    
    /* Notify hardware */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + IWL_CSR_INT), 0x01);
    
    return (int)len;
}

static int iwlwifi_recv(struct nc_device *dev, uint8_t *buf, size_t buf_len)
{
    struct iwlwifi_priv *priv = dev->driver_data;
    if (!priv || !buf || !priv->initialized) return -1;
    
    /* Security: validate buffer length */
    if (buf_len == 0 || buf_len > 2304) return -1;

    if (!priv->firmware_loaded) return 0;

    __sync_synchronize();
    
    if (priv->rx_head == priv->rx_tail) return 0;
    
    uint16_t idx = priv->rx_head % IWL_RX_QUEUE_SIZE;
    uint8_t *rx_buf = (uint8_t *)priv->rx_ring + idx * 2048;
    
    uint32_t len = 2048;
    /* Security: bounds check */
    if (len > buf_len || len > 2304) return -1;
    
    memcpy(buf, rx_buf, len);
    priv->rx_head++;
    
    return (int)len;
}

static void iwlwifi_get_mac(struct nc_device *dev, uint8_t mac_out[6])
{
    struct iwlwifi_priv *priv = dev->driver_data;
    if (!priv || !mac_out) return;
    memcpy(mac_out, priv->mac, 6);
}

static int iwlwifi_scan(struct nc_device *dev, struct nc_wifi_network *networks, int *count)
{
    struct iwlwifi_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !networks || !count || !plat) return -1;
    
    /* Security: validate count */
    if (*count < 0 || *count > 256) return -1;

    if (!priv->firmware_loaded) {
        plat->log("iwlwifi: cannot scan without firmware\n");
        *count = 0;
        return -1;
    }

    /* Send scan command to firmware */
    uint16_t idx = priv->cmd_tail % IWL_CMD_QUEUE_SIZE;
    struct iwlwifi_cmd *cmd = &priv->cmd_queue[idx];
    
    cmd->cmd_id = IWL_CMD_SCAN;
    cmd->len = 0;
    memset(cmd->data, 0, sizeof(cmd->data));
    
    __sync_synchronize();
    
    priv->cmd_tail++;
    
    /* In real implementation, wait for scan results from firmware */
    plat->sleep_ms(1000);
    
    *count = 0;
    return 0;
}

static int iwlwifi_connect(struct nc_device *dev, const char *ssid, const char *passphrase)
{
    struct iwlwifi_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !ssid || !plat) return -1;
    
    /* Security: validate SSID length */
    size_t ssid_len = strlen(ssid);
    if (ssid_len == 0 || ssid_len > 32) return -1;

    if (!priv->firmware_loaded) {
        plat->log("iwlwifi: cannot connect without firmware\n");
        return -1;
    }

    /* Send associate command to firmware */
    uint16_t idx = priv->cmd_tail % IWL_CMD_QUEUE_SIZE;
    struct iwlwifi_cmd *cmd = &priv->cmd_queue[idx];
    
    cmd->cmd_id = IWL_CMD_ASSOCIATE;
    cmd->len = (uint32_t)ssid_len + 1;
    memset(cmd->data, 0, sizeof(cmd->data));
    memcpy(cmd->data, ssid, cmd->len);
    
    __sync_synchronize();
    
    priv->cmd_tail++;
    
    (void)passphrase;
    
    /* In real implementation, wait for association response */
    plat->sleep_ms(2000);
    
    return 0;
}

static int iwlwifi_disconnect(struct nc_device *dev)
{
    struct iwlwifi_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !plat) return -1;

    if (!priv->firmware_loaded) return -1;

    /* Send disassociate command to firmware */
    uint16_t idx = priv->cmd_tail % IWL_CMD_QUEUE_SIZE;
    struct iwlwifi_cmd *cmd = &priv->cmd_queue[idx];
    
    cmd->cmd_id = IWL_CMD_DISASSOCIATE;
    cmd->len = 0;
    memset(cmd->data, 0, sizeof(cmd->data));
    
    __sync_synchronize();
    
    priv->cmd_tail++;
    
    return 0;
}

static int iwlwifi_get_status(struct nc_device *dev, char *ssid_out, int *connected)
{
    struct iwlwifi_priv *priv = dev->driver_data;
    if (!priv || !ssid_out || !connected) return -1;

    if (!priv->firmware_loaded) {
        *connected = 0;
        ssid_out[0] = '\0';
        return -1;
    }

    /* In real implementation, query firmware for connection status */
    *connected = 0;
    ssid_out[0] = '\0';
    
    return 0;
}

struct nc_wifi_driver nc_iwlwifi_driver = {
    .name = "iwlwifi",
    .probe = iwlwifi_probe,
    .remove = iwlwifi_remove,
    .send = iwlwifi_send,
    .recv = iwlwifi_recv,
    .get_mac = iwlwifi_get_mac,
    .scan = iwlwifi_scan,
    .connect = iwlwifi_connect,
    .disconnect = iwlwifi_disconnect,
    .get_status = iwlwifi_get_status,
};
