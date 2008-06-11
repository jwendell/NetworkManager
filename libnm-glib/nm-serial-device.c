/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */

#include "nm-serial-device.h"
#include "nm-device-private.h"
#include "nm-object-private.h"
#include "nm-marshal.h"

G_DEFINE_ABSTRACT_TYPE (NMSerialDevice, nm_serial_device, NM_TYPE_DEVICE)

#define NM_SERIAL_DEVICE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_SERIAL_DEVICE, NMSerialDevicePrivate))

typedef struct {
	DBusGProxy *proxy;

	guint32 in_bytes;
	guint32 out_bytes;

	gboolean disposed;
} NMSerialDevicePrivate;

enum {
	PPP_STATS,

	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

guint32
nm_serial_device_get_bytes_received (NMSerialDevice *self)
{
	g_return_val_if_fail (NM_IS_SERIAL_DEVICE (self), 0);

	return NM_SERIAL_DEVICE_GET_PRIVATE (self)->in_bytes;
}

guint32
nm_serial_device_get_bytes_sent (NMSerialDevice *self)
{
	g_return_val_if_fail (NM_IS_SERIAL_DEVICE (self), 0);

	return NM_SERIAL_DEVICE_GET_PRIVATE (self)->out_bytes;
}

static void
ppp_stats (DBusGProxy *proxy,
		   guint32 in_bytes,
		   guint32 out_bytes,
		   gpointer user_data)
{
	NMSerialDevice *self = NM_SERIAL_DEVICE (user_data);
	NMSerialDevicePrivate *priv = NM_SERIAL_DEVICE_GET_PRIVATE (self);

	priv->in_bytes = in_bytes;
	priv->out_bytes = out_bytes;

	g_signal_emit (self, signals[PPP_STATS], 0, in_bytes, out_bytes);
}

static void
device_state_changed (NMDevice *device, GParamSpec *pspec, gpointer user_data)
{
	NMSerialDevicePrivate *priv;

	switch (nm_device_get_state (device)) {
	case NM_DEVICE_STATE_UNMANAGED:
	case NM_DEVICE_STATE_UNAVAILABLE:
	case NM_DEVICE_STATE_DISCONNECTED:
		priv = NM_SERIAL_DEVICE_GET_PRIVATE (device);
		priv->in_bytes = priv->out_bytes = 0;
		break;
	default:
		break;
	}
}

static void
nm_serial_device_init (NMSerialDevice *device)
{
}

static GObject*
constructor (GType type,
			 guint n_construct_params,
			 GObjectConstructParam *construct_params)
{
	GObject *object;
	NMSerialDevicePrivate *priv;

	object = G_OBJECT_CLASS (nm_serial_device_parent_class)->constructor (type,
																		  n_construct_params,
																		  construct_params);
	if (!object)
		return NULL;

	priv = NM_SERIAL_DEVICE_GET_PRIVATE (object);

	priv->proxy = dbus_g_proxy_new_for_name (nm_object_get_connection (NM_OBJECT (object)),
											 NM_DBUS_SERVICE,
											 nm_object_get_path (NM_OBJECT (object)),
											 NM_DBUS_INTERFACE_SERIAL_DEVICE);

	dbus_g_object_register_marshaller (nm_marshal_VOID__UINT_UINT,
	                                   G_TYPE_NONE,
	                                   G_TYPE_UINT, G_TYPE_UINT,
	                                   G_TYPE_INVALID);

	dbus_g_proxy_add_signal (priv->proxy, "PppStats", G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (priv->proxy, "PppStats",
								 G_CALLBACK (ppp_stats),
								 object,
								 NULL);

	/* Catch NMDevice::state changes to reset the counters */
	g_signal_connect (object, "notify::state",
					  G_CALLBACK (device_state_changed),
					  object);

	return object;
}

static void
dispose (GObject *object)
{
	NMSerialDevicePrivate *priv = NM_SERIAL_DEVICE_GET_PRIVATE (object);

	if (priv->disposed) {
		G_OBJECT_CLASS (nm_serial_device_parent_class)->dispose (object);
		return;
	}

	priv->disposed = TRUE;

	g_object_unref (priv->proxy);

	G_OBJECT_CLASS (nm_serial_device_parent_class)->dispose (object);
}

static void
nm_serial_device_class_init (NMSerialDeviceClass *device_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (device_class);

	g_type_class_add_private (device_class, sizeof (NMSerialDevicePrivate));

	/* virtual methods */
	object_class->constructor = constructor;
	object_class->dispose = dispose;

	/* Signals */
	signals[PPP_STATS] =
		g_signal_new ("ppp-stats",
					  G_OBJECT_CLASS_TYPE (object_class),
					  G_SIGNAL_RUN_FIRST,
					  G_STRUCT_OFFSET (NMSerialDeviceClass, ppp_stats),
					  NULL, NULL,
					  nm_marshal_VOID__UINT_UINT,
					  G_TYPE_NONE, 2,
					  G_TYPE_UINT, G_TYPE_UINT);
}
