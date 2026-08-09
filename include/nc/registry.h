#ifndef NC_REGISTRY_H
#define NC_REGISTRY_H

#include "nc/device.h"

/* Platform registration */
int nc_platform_register(struct nc_platform *plat);
struct nc_platform *nc_platform_get(void);

/* Network driver registration and query */
int nc_register_net_driver(struct nc_net_driver *drv);
struct nc_net_driver *nc_get_net_driver(int index);
int nc_net_driver_count(void);

/* WiFi driver registration and query */
int nc_register_wifi_driver(struct nc_wifi_driver *drv);
struct nc_wifi_driver *nc_get_wifi_driver(int index);
int nc_wifi_driver_count(void);

/* Storage driver registration and query */
int nc_register_storage_driver(struct nc_storage_driver *drv);
struct nc_storage_driver *nc_get_storage_driver(int index);
int nc_storage_driver_count(void);

/* Initialize all built-in drivers */
void nc_drivers_init(void);

#endif
