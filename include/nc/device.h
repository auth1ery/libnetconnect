#ifndef NC_DEVICE_H
#define NC_DEVICE_H

#include <stdint.h>
#include <stddef.h>

#define NC_MAX_BARS 6

struct nc_bar {
    uint64_t phys_addr;
    volatile void *virt_addr;
    size_t size;
};

struct nc_device {
    uint16_t vendor_id;
    uint16_t device_id;
    struct nc_bar bars[NC_MAX_BARS];
    uint32_t irq;
    void *driver_data;
};

struct nc_net_driver {
    const char *name;
    int (*probe)(struct nc_device *dev);
    void (*remove)(struct nc_device *dev);
    int (*send)(struct nc_device *dev, const uint8_t *frame, size_t len);
    int (*recv)(struct nc_device *dev, uint8_t *buf, size_t buf_len);
    void (*get_mac)(struct nc_device *dev, uint8_t mac_out[6]);
};

#define NC_MAX_SSID_LEN 32
#define NC_MAX_BSSID_LEN 6
#define NC_MAX_NETWORKS 64

enum nc_wifi_security {
    NC_WIFI_SECURITY_OPEN = 0,
    NC_WIFI_SECURITY_WEP = 1,
    NC_WIFI_SECURITY_WPA_PSK = 2,
    NC_WIFI_SECURITY_WPA2_PSK = 3,
    NC_WIFI_SECURITY_WPA3_SAE = 4,
};

struct nc_wifi_network {
    uint8_t bssid[NC_MAX_BSSID_LEN];
    char ssid[NC_MAX_SSID_LEN + 1];
    int8_t signal_dbm;
    enum nc_wifi_security security;
    uint16_t channel;
};

struct nc_wifi_driver {
    const char *name;
    int (*probe)(struct nc_device *dev);
    void (*remove)(struct nc_device *dev);
    int (*send)(struct nc_device *dev, const uint8_t *frame, size_t len);
    int (*recv)(struct nc_device *dev, uint8_t *buf, size_t buf_len);
    void (*get_mac)(struct nc_device *dev, uint8_t mac_out[6]);
    int (*scan)(struct nc_device *dev, struct nc_wifi_network *networks, int *count);
    int (*connect)(struct nc_device *dev, const char *ssid, const char *passphrase);
    int (*disconnect)(struct nc_device *dev);
    int (*get_status)(struct nc_device *dev, char *ssid_out, int *connected);
};

int nc_register_net_driver(struct nc_net_driver *drv);
int nc_register_wifi_driver(struct nc_wifi_driver *drv);

#define NC_MAX_SECTOR_SIZE 512
#define NC_STORAGE_MAX_BLOCKS 0xFFFFFFFF

struct nc_storage_driver {
    const char *name;
    int (*probe)(struct nc_device *dev);
    void (*remove)(struct nc_device *dev);
    int (*read)(struct nc_device *dev, uint64_t lba, void *buffer, uint32_t sectors);
    int (*write)(struct nc_device *dev, uint64_t lba, const void *buffer, uint32_t sectors);
    int (*flush)(struct nc_device *dev);
    uint64_t (*get_capacity)(struct nc_device *dev);
};

int nc_register_storage_driver(struct nc_storage_driver *drv);

#endif
