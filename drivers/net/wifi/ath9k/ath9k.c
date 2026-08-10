#include "ath9k.h"
#include "nc/platform.h"
#include <string.h>

#define ATHEROS_VENDOR_ID 0x168C

/* Supported device IDs for AR9271 and related chipsets */
#define AR9271_DEVICE_ID 0x002E
#define AR9280_DEVICE_ID 0x002A
#define AR9285_DEVICE_ID 0x002B
#define AR9287_DEVICE_ID 0x002D

#define AR9271_RESET 0x4000
#define AR9271_RTC_RESET 0x7000
#define AR9271_RTC_RC 0x7010
#define AR9271_RTC_STATUS 0x7018
#define AR9271_TXDP 0x0000
#define AR9271_RXDP 0x0004
#define AR9271_CR 0x0008
#define AR9271_TXCFG 0x000C
#define AR9271_RXCFG 0x0010
#define AR9271_MIBC 0x0020
#define AR9271_INTR 0x0024
#define AR9271_INTR_MASK 0x0028

#define ATH9K_TX_QUEUE_SIZE 256
#define ATH9K_RX_QUEUE_SIZE 256
#define ATH9K_CMD_QUEUE_SIZE 32

/* Interrupt bits */
#define ATH9K_INT_RX 0x00000001
#define ATH9K_INT_TX 0x00000002
#define ATH9K_INT_RXORN 0x00000004
#define ATH9K_INT_TXURN 0x00000008
#define ATH9K_INT_MIB 0x00000010
#define ATH9K_INT_RXLP 0x00000020
#define ATH9K_INT_RXHP 0x00000040
#define ATH9K_INT_TXLP 0x00000080
#define ATH9K_INT_TXHP 0x00000100
#define ATH9K_INT_BMISS 0x00000400
#define ATH9K_INT_SWBA 0x00000800
#define ATH9K_INT_BNR 0x00001000
#define ATH9K_INT_TIM 0x00002000
#define ATH9K_INT_CST 0x00004000
#define ATH9K_INT_TSFOOR 0x00008000
#define ATH9K_INT_GPIO 0x00010000
#define ATH9K_INT_CABEND 0x00020000
#define ATH9K_INT_DTIM 0x00040000
#define ATH9K_INT_DTIMSYNC 0x00080000
#define ATH9K_INT_FATAL 0x00100000
#define ATH9K_INT_GLOBAL 0x00200000
#define ATH9K_INT_BB_WATCHDOG 0x00400000

/* Command IDs */
#define ATH9K_CMD_TX 0x01
#define ATH9K_CMD_RX 0x02
#define ATH9K_CMD_SCAN 0x03
#define ATH9K_CMD_ASSOCIATE 0x04
#define ATH9K_CMD_DISASSOCIATE 0x05

struct ath9k_cmd {
    uint32_t cmd_id;
    uint32_t len;
    uint8_t data[256];
};

struct ath9k_tx_desc {
    uint32_t addr;
    uint32_t len;
    uint8_t cmd;
    uint8_t flags;
};

struct ath9k_priv {
    uint8_t mac[6];
    volatile void *mmio_base;
    void *firmware_data;
    size_t firmware_len;
    struct ath9k_cmd *cmd_queue;
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

static void ath9k_irq_handler(void *ctx);

static int ath9k_probe(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    if (!plat) return -1;

    if (dev->vendor_id != ATHEROS_VENDOR_ID) return -1;
    
    /* Check for supported device IDs */
    switch (dev->device_id) {
        case AR9271_DEVICE_ID:
        case AR9280_DEVICE_ID:
        case AR9285_DEVICE_ID:
        case AR9287_DEVICE_ID:
            break;
        default:
            return -1;
    }

    struct ath9k_priv *priv = plat->alloc(sizeof(struct ath9k_priv));
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
    const char *fw_name = "ath9k_htc_9271.fw";
    int fw_result = plat->firmware_load(fw_name, &priv->firmware_data, &priv->firmware_len);
    if (fw_result != 0) {
        plat->log("ath9k: firmware load failed, continuing without firmware\n");
    } else {
        priv->firmware_loaded = 1;
        plat->log("ath9k: firmware loaded (%zu bytes)\n", priv->firmware_len);
    }

    /* Allocate command queue */
    priv->cmd_queue = plat->dma_alloc(sizeof(struct ath9k_cmd) * ATH9K_CMD_QUEUE_SIZE, &priv->cmd_queue_phys);
    if (!priv->cmd_queue) {
        if (priv->firmware_data) plat->firmware_free(priv->firmware_data);
        plat->free(priv);
        return -1;
    }
    memset(priv->cmd_queue, 0, sizeof(struct ath9k_cmd) * ATH9K_CMD_QUEUE_SIZE);

    /* Allocate TX ring */
    priv->tx_ring = plat->dma_alloc(sizeof(struct ath9k_tx_desc) * ATH9K_TX_QUEUE_SIZE, &priv->tx_ring_phys);
    if (!priv->tx_ring) {
        plat->dma_free(priv->cmd_queue, priv->cmd_queue_phys, sizeof(struct ath9k_cmd) * ATH9K_CMD_QUEUE_SIZE);
        if (priv->firmware_data) plat->firmware_free(priv->firmware_data);
        plat->free(priv);
        return -1;
    }
    memset(priv->tx_ring, 0, sizeof(struct ath9k_tx_desc) * ATH9K_TX_QUEUE_SIZE);

    /* Allocate RX ring */
    priv->rx_ring = plat->dma_alloc(ATH9K_RX_QUEUE_SIZE * 2048, &priv->rx_ring_phys);
    if (!priv->rx_ring) {
        plat->dma_free(priv->tx_ring, priv->tx_ring_phys, sizeof(struct ath9k_tx_desc) * ATH9K_TX_QUEUE_SIZE);
        plat->dma_free(priv->cmd_queue, priv->cmd_queue_phys, sizeof(struct ath9k_cmd) * ATH9K_CMD_QUEUE_SIZE);
        if (priv->firmware_data) plat->firmware_free(priv->firmware_data);
        plat->free(priv);
        return -1;
    }
    memset(priv->rx_ring, 0, ATH9K_RX_QUEUE_SIZE * 2048);

    /* Read MAC address from EEPROM */
    uint32_t mac_low = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + 0x5020));
    uint32_t mac_high = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + 0x5024));
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
        int irq_result = plat->irq_request(priv->irq_num, ath9k_irq_handler, dev);
        if (irq_result == 0) {
            priv->use_interrupts = 1;
            /* Enable interrupts */
            plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + AR9271_INTR_MASK), 
                               ATH9K_INT_RX | ATH9K_INT_TX | ATH9K_INT_FATAL);
            plat->log("ath9k: interrupt handler registered for IRQ %d\n", priv->irq_num);
        } else {
            plat->log("ath9k: interrupt registration failed, using polling\n");
        }
    } else {
        plat->log("ath9k: no IRQ available, using polling\n");
    }

    priv->initialized = 1;
    dev->driver_data = priv;

    plat->log("ath9k: probed device\n");
    return 0;
}

static void ath9k_irq_handler(void *ctx)
{
    struct nc_device *dev = (struct nc_device *)ctx;
    struct ath9k_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !plat) return;

    /* Read interrupt status */
    uint32_t status = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + AR9271_INTR));
    
    /* Acknowledge interrupt */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + AR9271_INTR), status);
    
    /* Handle RX interrupt */
    if (status & ATH9K_INT_RX) {
        /* RX processing will be done in recv() */
    }
    
    /* Handle TX interrupt */
    if (status & ATH9K_INT_TX) {
        /* TX completion will be handled in next send() */
    }
    
    /* Handle fatal interrupt */
    if (status & ATH9K_INT_FATAL) {
        /* Fatal error occurred */
    }
    
    /* Handle beacon interrupt */
    if (status & ATH9K_INT_SWBA) {
        /* SWBA - beacon ready */
    }
}

static void ath9k_remove(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    struct ath9k_priv *priv = dev->driver_data;
    if (!plat || !priv) return;
    
    /* Disable interrupts */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + AR9271_INTR_MASK), 0xFFFFFFFF);
    
    /* Unregister interrupt handler if registered */
    if (priv->use_interrupts && priv->irq_num >= 0) {
        plat->irq_free(priv->irq_num);
    }
    
    if (priv->cmd_queue) plat->dma_free(priv->cmd_queue, priv->cmd_queue_phys, sizeof(struct ath9k_cmd) * ATH9K_CMD_QUEUE_SIZE);
    if (priv->tx_ring) plat->dma_free(priv->tx_ring, priv->tx_ring_phys, sizeof(struct ath9k_tx_desc) * ATH9K_TX_QUEUE_SIZE);
    if (priv->rx_ring) plat->dma_free(priv->rx_ring, priv->rx_ring_phys, ATH9K_RX_QUEUE_SIZE * 2048);
    if (priv->firmware_data) plat->firmware_free(priv->firmware_data);
    
    plat->free(priv);
    dev->driver_data = NULL;
}

static int ath9k_send(struct nc_device *dev, const uint8_t *frame, size_t len)
{
    struct ath9k_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !frame || len == 0 || !plat || !priv->initialized) return -1;
    
    /* Security: validate frame length */
    if (len > 2304) return -1; /* Maximum WiFi frame size */

    if (!priv->firmware_loaded) {
        plat->log("ath9k: cannot send without firmware\n");
        return -1;
    }

    uint16_t idx = priv->tx_tail % ATH9K_TX_QUEUE_SIZE;
    struct ath9k_tx_desc *desc = (struct ath9k_tx_desc *)priv->tx_ring + idx;
    
    desc->addr = (uint64_t)(uintptr_t)frame;
    desc->len = (uint32_t)len;
    desc->cmd = ATH9K_CMD_TX;
    desc->flags = 0x01;
    
    __sync_synchronize();
    
    priv->tx_tail++;
    
    /* Notify hardware */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + AR9271_TXDP), priv->tx_ring_phys);
    
    return (int)len;
}

static int ath9k_recv(struct nc_device *dev, uint8_t *buf, size_t buf_len)
{
    struct ath9k_priv *priv = dev->driver_data;
    if (!priv || !buf || !priv->initialized) return -1;
    
    /* Security: validate buffer length */
    if (buf_len == 0 || buf_len > 2304) return -1;

    if (!priv->firmware_loaded) return 0;

    __sync_synchronize();
    
    if (priv->rx_head == priv->rx_tail) return 0;
    
    uint16_t idx = priv->rx_head % ATH9K_RX_QUEUE_SIZE;
    uint8_t *rx_buf = (uint8_t *)priv->rx_ring + idx * 2048;
    
    uint32_t len = 2048;
    /* Security: bounds check */
    if (len > buf_len || len > 2304) return -1;
    
    memcpy(buf, rx_buf, len);
    priv->rx_head++;
    
    return (int)len;
}

static void ath9k_get_mac(struct nc_device *dev, uint8_t mac_out[6])
{
    struct ath9k_priv *priv = dev->driver_data;
    if (!priv || !mac_out) return;
    memcpy(mac_out, priv->mac, 6);
}

static int ath9k_scan(struct nc_device *dev, struct nc_wifi_network *networks, int *count)
{
    struct ath9k_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !networks || !count || !plat) return -1;
    
    /* Security: validate count */
    if (*count < 0 || *count > 256) return -1;

    if (!priv->firmware_loaded) {
        plat->log("ath9k: cannot scan without firmware\n");
        *count = 0;
        return -1;
    }

    /* Send scan command to firmware */
    uint16_t idx = priv->cmd_tail % ATH9K_CMD_QUEUE_SIZE;
    struct ath9k_cmd *cmd = &priv->cmd_queue[idx];
    
    cmd->cmd_id = ATH9K_CMD_SCAN;
    cmd->len = 0;
    memset(cmd->data, 0, sizeof(cmd->data));
    
    __sync_synchronize();
    
    priv->cmd_tail++;
    
    plat->sleep_ms(1000);
    
    *count = 0;
    return 0;
}

static int ath9k_connect(struct nc_device *dev, const char *ssid, const char *passphrase)
{
    struct ath9k_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !ssid || !plat) return -1;
    
    /* Security: validate SSID length */
    size_t ssid_len = strlen(ssid);
    if (ssid_len == 0 || ssid_len > 32) return -1;

    if (!priv->firmware_loaded) {
        plat->log("ath9k: cannot connect without firmware\n");
        return -1;
    }

    /* Send associate command to firmware */
    uint16_t idx = priv->cmd_tail % ATH9K_CMD_QUEUE_SIZE;
    struct ath9k_cmd *cmd = &priv->cmd_queue[idx];
    
    cmd->cmd_id = ATH9K_CMD_ASSOCIATE;
    cmd->len = (uint32_t)ssid_len + 1;
    memset(cmd->data, 0, sizeof(cmd->data));
    memcpy(cmd->data, ssid, cmd->len);
    
    __sync_synchronize();
    
    priv->cmd_tail++;
    
    (void)passphrase;
    
    plat->sleep_ms(2000);
    
    return 0;
}

static int ath9k_disconnect(struct nc_device *dev)
{
    struct ath9k_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !plat) return -1;

    if (!priv->firmware_loaded) return -1;

    /* Send disassociate command to firmware */
    uint16_t idx = priv->cmd_tail % ATH9K_CMD_QUEUE_SIZE;
    struct ath9k_cmd *cmd = &priv->cmd_queue[idx];
    
    cmd->cmd_id = ATH9K_CMD_DISASSOCIATE;
    cmd->len = 0;
    memset(cmd->data, 0, sizeof(cmd->data));
    
    __sync_synchronize();
    
    priv->cmd_tail++;
    
    return 0;
}

static int ath9k_get_status(struct nc_device *dev, char *ssid_out, int *connected)
{
    struct ath9k_priv *priv = dev->driver_data;
    if (!priv || !ssid_out || !connected) return -1;

    if (!priv->firmware_loaded) {
        *connected = 0;
        ssid_out[0] = '\0';
        return -1;
    }

    *connected = 0;
    ssid_out[0] = '\0';
    
    return 0;
}

struct nc_wifi_driver nc_ath9k_driver = {
    .name = "ath9k",
    .probe = ath9k_probe,
    .remove = ath9k_remove,
    .send = ath9k_send,
    .recv = ath9k_recv,
    .get_mac = ath9k_get_mac,
    .scan = ath9k_scan,
    .connect = ath9k_connect,
    .disconnect = ath9k_disconnect,
    .get_status = ath9k_get_status,
};
