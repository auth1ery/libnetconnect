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

struct iwlwifi_priv {
    uint8_t mac[6];
    volatile void *mmio_base;
    int initialized;
};

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
    priv->initialized = 0;

    dev->driver_data = priv;

    plat->log("iwlwifi: probed device\n");
    return 0;
}

static void iwlwifi_remove(struct nc_device *dev)
{
    struct nc_platform *plat = nc_platform_get();
    struct iwlwifi_priv *priv = dev->driver_data;
    if (!plat || !priv) return;
    plat->free(priv);
    dev->driver_data = NULL;
}

static int iwlwifi_send(struct nc_device *dev, const uint8_t *frame, size_t len)
{
    struct iwlwifi_priv *priv = dev->driver_data;
    if (!priv || !frame || len == 0) return -1;
    /* TODO: implement tx path with firmware commands */
    (void)len;
    return -1;
}

static int iwlwifi_recv(struct nc_device *dev, uint8_t *buf, size_t buf_len)
{
    struct iwlwifi_priv *priv = dev->driver_data;
    if (!priv || !buf) return -1;
    /* TODO: implement rx path with firmware notifications */
    (void)buf_len;
    return 0;
}

static void iwlwifi_get_mac(struct nc_device *dev, uint8_t mac_out[6])
{
    struct iwlwifi_priv *priv = dev->driver_data;
    if (!priv) return;
    memcpy(mac_out, priv->mac, 6);
}

static int iwlwifi_scan(struct nc_device *dev, struct nc_wifi_network *networks, int *count)
{
    struct iwlwifi_priv *priv = dev->driver_data;
    if (!priv || !networks || !count) return -1;
    /* TODO: implement scan via firmware scan command */
    *count = 0;
    return -1;
}

static int iwlwifi_connect(struct nc_device *dev, const char *ssid, const char *passphrase)
{
    struct iwlwifi_priv *priv = dev->driver_data;
    if (!priv || !ssid) return -1;
    /* TODO: implement connect via firmware authentication command */
    (void)passphrase;
    return -1;
}

static int iwlwifi_disconnect(struct nc_device *dev)
{
    struct iwlwifi_priv *priv = dev->driver_data;
    if (!priv) return -1;
    /* TODO: implement disconnect via firmware command */
    return -1;
}

static int iwlwifi_get_status(struct nc_device *dev, char *ssid_out, int *connected)
{
    struct iwlwifi_priv *priv = dev->driver_data;
    if (!priv || !ssid_out || !connected) return -1;
    /* TODO: query firmware for connection status */
    *connected = 0;
    ssid_out[0] = '\0';
    return -1;
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
