#include "rtl8188.h"
#include "nc/platform.h"
#include <string.h>

#define REALTEK_VENDOR_ID 0x10EC

/* Supported device IDs for RTL8188 series */
#define RTL8188CE_DEVICE_ID 0x8176
#define RTL8188CUS_DEVICE_ID 0x8179
#define RTL8188ETV_DEVICE_ID 0x8188
#define RTL8188EU_DEVICE_ID 0x8189

#define RTL8188_SYS_CTRL 0x0020
#define RTL8188_TXPAUSE 0x0022
#define RTL8188_INT_MIG 0x0024
#define RTL8188_BCN_CTRL 0x0030
#define RTL8188_CMD 0x0037
#define RTL8188_TX_DESC_ADDR 0x0028
#define RTL8188_RX_DESC_ADDR 0x002C
#define RTL8188_EEPROM 0x0050
#define RTL8188_IMR 0x005C
#define RTL8188_ISR 0x005E

#define RTL8188_TX_QUEUE_SIZE 256
#define RTL8188_RX_QUEUE_SIZE 256
#define RTL8188_CMD_QUEUE_SIZE 32

/* Interrupt bits */
#define RTL8188_INT_TX_OK 0x0001
#define RTL8188_INT_RX_OK 0x0002
#define RTL8188_INT_TX_ERR 0x0004
#define RTL8188_INT_RX_ERR 0x0008
#define RTL8188_INT_RX_OVF 0x0010
#define RTL8188_INT_TX_DESC_UNAVAIL 0x0020
#define RTL8188_INT_RX_DESC_UNAVAIL 0x0040
#define RTL8188_INT_RXTX_OK 0x0080
#define RTL8188_INT_CPWM 0x0100
#define RTL8188_INT_TXFOVW 0x0200
#define RTL8188_INT_RXFOVW 0x0400

/* Command IDs */
#define RTL8188_CMD_TX 0x01
#define RTL8188_CMD_RX 0x02
#define RTL8188_CMD_SCAN 0x03
#define RTL8188_CMD_ASSOCIATE 0x04
#define RTL8188_CMD_DISASSOCIATE 0x05

struct rtl8188_cmd {
    uint32_t cmd_id;
    uint32_t len;
    uint8_t data[256];
};

struct rtl8188_tx_desc {
    uint32_t addr;
    uint32_t len;
    uint8_t cmd;
    uint8_t flags;
};

struct rtl8188_priv {
    uint8_t mac[6];
    volatile void *mmio_base;
    void *firmware_data;
    size_t firmware_len;
    struct rtl8188_cmd *cmd_queue;
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

static void rtl8188_irq_handler(void *ctx);

static int rtl8188_probe(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    if (!plat) return -1;

    if (dev->vendor_id != REALTEK_VENDOR_ID) return -1;
    
    /* Check for supported device IDs */
    switch (dev->device_id) {
        case RTL8188CE_DEVICE_ID:
        case RTL8188CUS_DEVICE_ID:
        case RTL8188ETV_DEVICE_ID:
        case RTL8188EU_DEVICE_ID:
            break;
        default:
            return -1;
    }

    struct rtl8188_priv *priv = plat->alloc(sizeof(struct rtl8188_priv));
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
    const char *fw_name = "rtl8188/rtl8188eu.bin";
    int fw_result = plat->firmware_load(fw_name, &priv->firmware_data, &priv->firmware_len);
    if (fw_result != 0) {
        plat->log("rtl8188: firmware load failed, continuing without firmware\n");
    } else {
        priv->firmware_loaded = 1;
        plat->log("rtl8188: firmware loaded (%zu bytes)\n", priv->firmware_len);
    }

    /* Allocate command queue */
    priv->cmd_queue = plat->dma_alloc(sizeof(struct rtl8188_cmd) * RTL8188_CMD_QUEUE_SIZE, &priv->cmd_queue_phys);
    if (!priv->cmd_queue) {
        if (priv->firmware_data) plat->firmware_free(priv->firmware_data);
        plat->free(priv);
        return -1;
    }
    memset(priv->cmd_queue, 0, sizeof(struct rtl8188_cmd) * RTL8188_CMD_QUEUE_SIZE);

    /* Allocate TX ring */
    priv->tx_ring = plat->dma_alloc(sizeof(struct rtl8188_tx_desc) * RTL8188_TX_QUEUE_SIZE, &priv->tx_ring_phys);
    if (!priv->tx_ring) {
        plat->dma_free(priv->cmd_queue, priv->cmd_queue_phys, sizeof(struct rtl8188_cmd) * RTL8188_CMD_QUEUE_SIZE);
        if (priv->firmware_data) plat->firmware_free(priv->firmware_data);
        plat->free(priv);
        return -1;
    }
    memset(priv->tx_ring, 0, sizeof(struct rtl8188_tx_desc) * RTL8188_TX_QUEUE_SIZE);

    /* Allocate RX ring */
    priv->rx_ring = plat->dma_alloc(RTL8188_RX_QUEUE_SIZE * 2048, &priv->rx_ring_phys);
    if (!priv->rx_ring) {
        plat->dma_free(priv->tx_ring, priv->tx_ring_phys, sizeof(struct rtl8188_tx_desc) * RTL8188_TX_QUEUE_SIZE);
        plat->dma_free(priv->cmd_queue, priv->cmd_queue_phys, sizeof(struct rtl8188_cmd) * RTL8188_CMD_QUEUE_SIZE);
        if (priv->firmware_data) plat->firmware_free(priv->firmware_data);
        plat->free(priv);
        return -1;
    }
    memset(priv->rx_ring, 0, RTL8188_RX_QUEUE_SIZE * 2048);

    /* Read MAC address from EEPROM */
    uint32_t mac_low = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + RTL8188_EEPROM));
    uint32_t mac_high = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + RTL8188_EEPROM + 4));
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
        int irq_result = plat->irq_request(priv->irq_num, rtl8188_irq_handler, dev);
        if (irq_result == 0) {
            priv->use_interrupts = 1;
            /* Enable interrupts */
            uint32_t imr_val = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + RTL8188_IMR));
            imr_val = (imr_val & 0xFFFF0000) | (RTL8188_INT_TX_OK | RTL8188_INT_RX_OK | RTL8188_INT_TX_ERR | RTL8188_INT_RX_ERR);
            plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + RTL8188_IMR), imr_val);
            plat->log("rtl8188: interrupt handler registered for IRQ %d\n", priv->irq_num);
        } else {
            plat->log("rtl8188: interrupt registration failed, using polling\n");
        }
    } else {
        plat->log("rtl8188: no IRQ available, using polling\n");
    }

    priv->initialized = 1;
    dev->driver_data = priv;

    plat->log("rtl8188: probed device\n");
    return 0;
}

static void rtl8188_irq_handler(void *ctx)
{
    struct nc_device *dev = (struct nc_device *)ctx;
    struct rtl8188_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !plat) return;

    /* Read interrupt status */
    uint32_t isr = plat->mmio_read32((volatile void *)((uint8_t *)priv->mmio_base + RTL8188_ISR));
    isr &= 0xFFFF;
    
    /* Acknowledge interrupt */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + RTL8188_ISR), isr);
    
    /* Handle RX interrupt */
    if (isr & RTL8188_INT_RX_OK) {
        /* RX processing will be done in recv() */
    }
    
    /* Handle TX interrupt */
    if (isr & RTL8188_INT_TX_OK) {
        /* TX completion will be handled in next send() */
    }
    
    /* Handle error interrupts */
    if (isr & (RTL8188_INT_TX_ERR | RTL8188_INT_RX_ERR)) {
        /* Error occurred */
    }
    
    /* Handle overflow interrupts */
    if (isr & (RTL8188_INT_RX_OVF | RTL8188_INT_TXFOVW | RTL8188_INT_RXFOVW)) {
        /* Overflow occurred */
    }
}

static void rtl8188_remove(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    struct rtl8188_priv *priv = dev->driver_data;
    if (!plat || !priv) return;
    
    /* Disable interrupts */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + RTL8188_IMR), 0xFFFFFFFF);
    
    /* Unregister interrupt handler if registered */
    if (priv->use_interrupts && priv->irq_num >= 0) {
        plat->irq_free(priv->irq_num);
    }
    
    if (priv->cmd_queue) plat->dma_free(priv->cmd_queue, priv->cmd_queue_phys, sizeof(struct rtl8188_cmd) * RTL8188_CMD_QUEUE_SIZE);
    if (priv->tx_ring) plat->dma_free(priv->tx_ring, priv->tx_ring_phys, sizeof(struct rtl8188_tx_desc) * RTL8188_TX_QUEUE_SIZE);
    if (priv->rx_ring) plat->dma_free(priv->rx_ring, priv->rx_ring_phys, RTL8188_RX_QUEUE_SIZE * 2048);
    if (priv->firmware_data) plat->firmware_free(priv->firmware_data);
    
    plat->free(priv);
    dev->driver_data = NULL;
}

static int rtl8188_send(struct nc_device *dev, const uint8_t *frame, size_t len)
{
    struct rtl8188_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !frame || len == 0 || !plat || !priv->initialized) return -1;
    
    /* Security: validate frame length */
    if (len > 2304) return -1; /* Maximum WiFi frame size */

    if (!priv->firmware_loaded) {
        plat->log("rtl8188: cannot send without firmware\n");
        return -1;
    }

    uint16_t idx = priv->tx_tail % RTL8188_TX_QUEUE_SIZE;
    struct rtl8188_tx_desc *desc = (struct rtl8188_tx_desc *)priv->tx_ring + idx;
    
    desc->addr = (uint32_t)(uintptr_t)frame;
    desc->len = (uint32_t)len;
    desc->cmd = RTL8188_CMD_TX;
    desc->flags = 0x01;
    
    __sync_synchronize();
    
    priv->tx_tail++;
    
    /* Notify hardware */
    plat->mmio_write32((volatile void *)((uint8_t *)priv->mmio_base + RTL8188_TX_DESC_ADDR), priv->tx_ring_phys);
    
    return (int)len;
}

static int rtl8188_recv(struct nc_device *dev, uint8_t *buf, size_t buf_len)
{
    struct rtl8188_priv *priv = dev->driver_data;
    if (!priv || !buf || !priv->initialized) return -1;
    
    /* Security: validate buffer length */
    if (buf_len == 0 || buf_len > 2304) return -1;

    if (!priv->firmware_loaded) return 0;

    __sync_synchronize();
    
    if (priv->rx_head == priv->rx_tail) return 0;
    
    uint16_t idx = priv->rx_head % RTL8188_RX_QUEUE_SIZE;
    uint8_t *rx_buf = (uint8_t *)priv->rx_ring + idx * 2048;
    
    uint32_t len = 2048;
    /* Security: bounds check */
    if (len > buf_len || len > 2304) return -1;
    
    memcpy(buf, rx_buf, len);
    priv->rx_head++;
    
    return (int)len;
}

static void rtl8188_get_mac(struct nc_device *dev, uint8_t mac_out[6])
{
    struct rtl8188_priv *priv = dev->driver_data;
    if (!priv || !mac_out) return;
    memcpy(mac_out, priv->mac, 6);
}

static int rtl8188_scan(struct nc_device *dev, struct nc_wifi_network *networks, int *count)
{
    struct rtl8188_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !networks || !count || !plat) return -1;
    
    /* Security: validate count */
    if (*count < 0 || *count > 256) return -1;

    if (!priv->firmware_loaded) {
        plat->log("rtl8188: cannot scan without firmware\n");
        *count = 0;
        return -1;
    }

    /* Send scan command to firmware */
    uint16_t idx = priv->cmd_tail % RTL8188_CMD_QUEUE_SIZE;
    struct rtl8188_cmd *cmd = &priv->cmd_queue[idx];
    
    cmd->cmd_id = RTL8188_CMD_SCAN;
    cmd->len = 0;
    memset(cmd->data, 0, sizeof(cmd->data));
    
    __sync_synchronize();
    
    priv->cmd_tail++;
    
    plat->sleep_ms(1000);
    
    *count = 0;
    return 0;
}

static int rtl8188_connect(struct nc_device *dev, const char *ssid, const char *passphrase)
{
    struct rtl8188_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !ssid || !plat) return -1;
    
    /* Security: validate SSID length */
    size_t ssid_len = strlen(ssid);
    if (ssid_len == 0 || ssid_len > 32) return -1;

    if (!priv->firmware_loaded) {
        plat->log("rtl8188: cannot connect without firmware\n");
        return -1;
    }

    /* Send associate command to firmware */
    uint16_t idx = priv->cmd_tail % RTL8188_CMD_QUEUE_SIZE;
    struct rtl8188_cmd *cmd = &priv->cmd_queue[idx];
    
    cmd->cmd_id = RTL8188_CMD_ASSOCIATE;
    cmd->len = (uint32_t)ssid_len + 1;
    memset(cmd->data, 0, sizeof(cmd->data));
    memcpy(cmd->data, ssid, cmd->len);
    
    __sync_synchronize();
    
    priv->cmd_tail++;
    
    (void)passphrase;
    
    plat->sleep_ms(2000);
    
    return 0;
}

static int rtl8188_disconnect(struct nc_device *dev)
{
    struct rtl8188_priv *priv = dev->driver_data;
    struct nc_platform *plat = nc_platform_get();
    if (!priv || !plat) return -1;

    if (!priv->firmware_loaded) return -1;

    /* Send disassociate command to firmware */
    uint16_t idx = priv->cmd_tail % RTL8188_CMD_QUEUE_SIZE;
    struct rtl8188_cmd *cmd = &priv->cmd_queue[idx];
    
    cmd->cmd_id = RTL8188_CMD_DISASSOCIATE;
    cmd->len = 0;
    memset(cmd->data, 0, sizeof(cmd->data));
    
    __sync_synchronize();
    
    priv->cmd_tail++;
    
    return 0;
}

static int rtl8188_get_status(struct nc_device *dev, char *ssid_out, int *connected)
{
    struct rtl8188_priv *priv = dev->driver_data;
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

struct nc_wifi_driver nc_rtl8188_driver = {
    .name = "rtl8188",
    .probe = rtl8188_probe,
    .remove = rtl8188_remove,
    .send = rtl8188_send,
    .recv = rtl8188_recv,
    .get_mac = rtl8188_get_mac,
    .scan = rtl8188_scan,
    .connect = rtl8188_connect,
    .disconnect = rtl8188_disconnect,
    .get_status = rtl8188_get_status,
};
