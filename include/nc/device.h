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

int nc_register_net_driver(struct nc_net_driver *drv);

#endif
