#include "nc/platform.h"
#include "nc/device.h"
#include <stddef.h>

#define NC_MAX_DRIVERS 32

static struct nc_platform *g_platform = NULL;
static struct nc_net_driver *g_net_drivers[NC_MAX_DRIVERS];
static int g_net_driver_count = 0;

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
