#include "platform_linux.h"
#include "nc/platform.h"
#include "nc/device.h"
#include "virtio_net.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    if (nc_platform_register(&g_linux_platform) != 0) {
        fprintf(stderr, "failed to register platform\n");
        return 1;
    }

    struct nc_platform *plat = nc_platform_get();
    plat->log("linux userspace shim up, time_ns=%llu\n",
              (unsigned long long)plat->time_ns());

    if (nc_register_net_driver(&nc_virtio_net_driver) != 0) {
        fprintf(stderr, "failed to register virtio-net driver\n");
        return 1;
    }

    struct nc_device dev;
    memset(&dev, 0, sizeof(dev));
    dev.vendor_id = 0x1AF4;
    dev.device_id = 0x1000;
    dev.irq = 11;

    if (nc_virtio_net_driver.probe(&dev) != 0) {
        fprintf(stderr, "probe failed\n");
        return 1;
    }

    plat->log("virtio-net probed ok against fake device, driver_data=%p\n",
              dev.driver_data);

    uint8_t frame[64];
    memset(frame, 0xAA, sizeof(frame));
    int sent = nc_virtio_net_driver.send(&dev, frame, sizeof(frame));
    plat->log("send returned %d\n", sent);

    nc_virtio_net_driver.remove(&dev);
    plat->log("driver removed cleanly\n");

    return 0;
}
