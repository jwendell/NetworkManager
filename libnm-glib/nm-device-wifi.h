#ifndef NM_DEVICE_WIFI_H
#define NM_DEVICE_WIFI_H

#include "nm-device.h"
#include "nm-access-point.h"

G_BEGIN_DECLS

#define NM_TYPE_DEVICE_WIFI            (nm_device_wifi_get_type ())
#define NM_DEVICE_WIFI(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_DEVICE_WIFI, NMDeviceWifi))
#define NM_DEVICE_WIFI_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_DEVICE_WIFI, NMDeviceWifiClass))
#define NM_IS_DEVICE_WIFI(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_DEVICE_WIFI))
#define NM_IS_DEVICE_WIFI_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), NM_TYPE_DEVICE_WIFI))
#define NM_DEVICE_WIFI_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_DEVICE_WIFI, NMDeviceWifiClass))

#define NM_DEVICE_WIFI_HW_ADDRESS          "hw-address"
#define NM_DEVICE_WIFI_MODE                "mode"
#define NM_DEVICE_WIFI_BITRATE             "bitrate"
#define NM_DEVICE_WIFI_ACTIVE_ACCESS_POINT "active-access-point"
#define NM_DEVICE_WIFI_CAPABILITIES        "wireless-capabilities"

typedef struct {
	NMDevice parent;
} NMDeviceWifi;

typedef struct {
	NMDeviceClass parent;

	/* Signals */
	void (*access_point_added) (NMDeviceWifi *device, NMAccessPoint *ap);
	void (*access_point_removed) (NMDeviceWifi *device, NMAccessPoint *ap);
} NMDeviceWifiClass;

GType nm_device_wifi_get_type (void);

GObject *nm_device_wifi_new (DBusGConnection *connection, const char *path);

const char *    nm_device_wifi_get_hw_address           (NMDeviceWifi *device);
NM80211Mode     nm_device_wifi_get_mode                 (NMDeviceWifi *device);
guint32         nm_device_wifi_get_bitrate              (NMDeviceWifi *device);
guint32         nm_device_wifi_get_capabilities         (NMDeviceWifi *device);
NMAccessPoint * nm_device_wifi_get_active_access_point  (NMDeviceWifi *device);

NMAccessPoint * nm_device_wifi_get_access_point_by_path (NMDeviceWifi *device,
                                                         const char *path);

const GPtrArray *nm_device_wifi_get_access_points       (NMDeviceWifi *device);

G_END_DECLS

#endif /* NM_DEVICE_WIFI_H */