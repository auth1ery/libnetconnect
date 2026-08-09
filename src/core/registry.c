#include "nc/platform.h"
#include "nc/device.h"
#include <stddef.h>

#define NC_MAX_DRIVERS 32

static struct nc_platform *g_platform = NULL;
static struct nc_net_driver *g_net_drivers[NC_MAX_DRIVERS];
static int g_net_driver_count = 0;
static struct nc_wifi_driver *g_wifi_drivers[NC_MAX_DRIVERS];
static int g_wifi_driver_count = 0;
static struct nc_storage_driver *g_storage_drivers[NC_MAX_DRIVERS];
static int g_storage_driver_count = 0;

/* External driver declarations */
extern struct nc_net_driver nc_virtio_net_driver;
extern struct nc_net_driver nc_e1000_driver;
extern struct nc_net_driver nc_r8169_driver;
extern struct nc_wifi_driver nc_iwlwifi_driver;
extern struct nc_wifi_driver nc_ath9k_driver;
extern struct nc_wifi_driver nc_rtl8188_driver;

int nc_platform_register(struct nc_platform *plat)
{
    if (!plat) return -1;
    g_platform = plat;
    return 0;
}

struct nc_platform *nc_platform_get(void)
{
    return g_platform;
}

int nc_register_net_driver(struct nc_net_driver *drv)
{
    if (!drv || g_net_driver_count >= NC_MAX_DRIVERS) return -1;
    g_net_drivers[g_net_driver_count++] = drv;
    return 0;
}

struct nc_net_driver *nc_get_net_driver(int index)
{
    if (index < 0 || index >= g_net_driver_count) return NULL;
    return g_net_drivers[index];
}

int nc_net_driver_count(void)
{
    return g_net_driver_count;
}

int nc_register_wifi_driver(struct nc_wifi_driver *drv)
{
    if (!drv || g_wifi_driver_count >= NC_MAX_DRIVERS) return -1;
    g_wifi_drivers[g_wifi_driver_count++] = drv;
    return 0;
}

struct nc_wifi_driver *nc_get_wifi_driver(int index)
{
    if (index < 0 || index >= g_wifi_driver_count) return NULL;
    return g_wifi_drivers[index];
}

int nc_wifi_driver_count(void)
{
    return g_wifi_driver_count;
}

int nc_register_storage_driver(struct nc_storage_driver *drv)
{
    if (!drv || g_storage_driver_count >= NC_MAX_DRIVERS) return -1;
    g_storage_drivers[g_storage_driver_count++] = drv;
    return 0;
}

struct nc_storage_driver *nc_get_storage_driver(int index)
{
    if (index < 0 || index >= g_storage_driver_count) return NULL;
    return g_storage_drivers[index];
}

int nc_storage_driver_count(void)
{
    return g_storage_driver_count;
}

void nc_drivers_init(void)
{
    nc_register_net_driver(&nc_virtio_net_driver);
    nc_register_net_driver(&nc_e1000_driver);
    nc_register_net_driver(&nc_r8169_driver);
    nc_register_wifi_driver(&nc_iwlwifi_driver);
    nc_register_wifi_driver(&nc_ath9k_driver);
    nc_register_wifi_driver(&nc_rtl8188_driver);
}
