/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager system settings service
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2008 Red Hat, Inc.
 */

#include <string.h>
#include <net/ethernet.h>
#include <netinet/ether.h>

#include <glib/gstdio.h>

#include <NetworkManager.h>
#include <nm-setting-connection.h>
#include <nm-setting-wired.h>
#include <nm-setting-wireless.h>
#include <nm-setting-gsm.h>
#include <nm-setting-cdma.h>
#include <nm-setting-pppoe.h>
#include <nm-setting-wireless-security.h>
#include <nm-setting-8021x.h>

#include "common.h"
#include "nm-ifcfg-connection.h"
#include "nm-system-config-hal-manager.h"
#include "reader.h"

G_DEFINE_TYPE (NMIfcfgConnection, nm_ifcfg_connection, NM_TYPE_EXPORTED_CONNECTION)

#define NM_IFCFG_CONNECTION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_IFCFG_CONNECTION, NMIfcfgConnectionPrivate))

typedef struct {
	char *filename;
	char *keyfile;

	char *udi;
	gboolean unmanaged;

	NMSystemConfigHalManager *hal_mgr;
	DBusGConnection *g_connection;
	gulong daid;
} NMIfcfgConnectionPrivate;

enum {
	PROP_0,
	PROP_FILENAME,
	PROP_UNMANAGED,
	PROP_UDI,

	LAST_PROP
};

static char *
get_ether_device_udi (DBusGConnection *g_connection, GByteArray *mac, GSList *devices)
{
	GError *error = NULL;
	GSList *iter;
	char *udi = NULL;

	if (!g_connection || !mac)
		return NULL;

	for (iter = devices; !udi && iter; iter = g_slist_next (iter)) {
		DBusGProxy *dev_proxy;
		char *address = NULL;

		dev_proxy = dbus_g_proxy_new_for_name (g_connection,
		                                       "org.freedesktop.Hal",
		                                       iter->data,
		                                       "org.freedesktop.Hal.Device");
		if (!dev_proxy)
			continue;

		if (dbus_g_proxy_call_with_timeout (dev_proxy,
		                                    "GetPropertyString", 10000, &error,
		                                    G_TYPE_STRING, "net.address", G_TYPE_INVALID,
		                                    G_TYPE_STRING, &address, G_TYPE_INVALID)) {		
			struct ether_addr *dev_mac;

			if (address && strlen (address)) {
				dev_mac = ether_aton (address);
				if (!memcmp (dev_mac->ether_addr_octet, mac->data, ETH_ALEN))
					udi = g_strdup (iter->data);
			}
		} else {
			g_error_free (error);
			error = NULL;
		}
		g_free (address);
		g_object_unref (dev_proxy);
	}

	return udi;
}

static NMDeviceType
get_device_type_for_connection (NMConnection *connection)
{
	NMDeviceType devtype = DEVICE_TYPE_UNKNOWN;
	NMSettingConnection *s_con;

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	if (!s_con)
		return DEVICE_TYPE_UNKNOWN;

	if (   !strcmp (s_con->type, NM_SETTING_WIRED_SETTING_NAME)
	    || !strcmp (s_con->type, NM_SETTING_PPPOE_SETTING_NAME)) {
		if (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRED))
			devtype = DEVICE_TYPE_802_3_ETHERNET;
	} else if (!strcmp (s_con->type, NM_SETTING_WIRELESS_SETTING_NAME)) {
		if (nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS))
			devtype = DEVICE_TYPE_802_11_WIRELESS;
	} else if (!strcmp (s_con->type, NM_SETTING_GSM_SETTING_NAME)) {
		if (nm_connection_get_setting (connection, NM_TYPE_SETTING_GSM))
			devtype = DEVICE_TYPE_GSM;
	} else if (!strcmp (s_con->type, NM_SETTING_CDMA_SETTING_NAME)) {
		if (nm_connection_get_setting (connection, NM_TYPE_SETTING_CDMA))
			devtype = DEVICE_TYPE_CDMA;
	}

	return devtype;
}

static char *
get_udi_for_connection (NMConnection *connection,
                        DBusGConnection *g_connection,
                        NMSystemConfigHalManager *hal_mgr,
                        NMDeviceType devtype)
{
	NMSettingWired *s_wired;
	NMSettingWireless *s_wireless;
	char *udi = NULL;
	GSList *devices = NULL;

	if (devtype == DEVICE_TYPE_UNKNOWN)
		devtype = get_device_type_for_connection (connection);

	switch (devtype) {
	case DEVICE_TYPE_802_3_ETHERNET:
		s_wired = (NMSettingWired *) nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRED);
		if (s_wired) {
			devices = nm_system_config_hal_manager_get_devices_of_type (hal_mgr, DEVICE_TYPE_802_3_ETHERNET);
			udi = get_ether_device_udi (g_connection, s_wired->mac_address, devices);
		}
		break;

	case DEVICE_TYPE_802_11_WIRELESS:
		s_wireless = (NMSettingWireless *) nm_connection_get_setting (connection, NM_TYPE_SETTING_WIRELESS);
		if (s_wireless) {
			devices = nm_system_config_hal_manager_get_devices_of_type (hal_mgr, DEVICE_TYPE_802_11_WIRELESS);
			udi = get_ether_device_udi (g_connection, s_wireless->mac_address, devices);
		}
		break;

	default:
		break;
	}

	g_slist_foreach (devices, (GFunc) g_free, NULL);
	g_slist_free (devices);

	return udi;
}

static void
device_added_cb (NMSystemConfigHalManager *hal_mgr,
                 const char *udi,
                 NMDeviceType devtype,
                 gpointer user_data)
{
	NMIfcfgConnection *connection = NM_IFCFG_CONNECTION (user_data);
	NMIfcfgConnectionPrivate *priv = NM_IFCFG_CONNECTION_GET_PRIVATE (connection);
	NMConnection *wrapped;

	/* Should only be called when udi is NULL */
	g_return_if_fail (priv->udi == NULL);

	wrapped = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (connection));
	if (devtype != get_device_type_for_connection (wrapped))
		return;

	priv->udi = get_udi_for_connection (wrapped, priv->g_connection, priv->hal_mgr, devtype);
	if (!priv->udi)
		return;

	/* If the connection is unmanaged we have to tell the plugin */
	if (priv->unmanaged)
		g_object_notify (G_OBJECT (connection), NM_IFCFG_CONNECTION_UNMANAGED);

	g_signal_handler_disconnect (G_OBJECT (hal_mgr), priv->daid);
	priv->daid = 0;
}

NMIfcfgConnection *
nm_ifcfg_connection_new (const char *filename,
                         DBusGConnection *g_connection,
                         NMSystemConfigHalManager *hal_mgr,
                         GError **error)
{
	GObject *object;
	NMConnection *wrapped;
	gboolean unmanaged = FALSE;
	char *udi;

	g_return_val_if_fail (filename != NULL, NULL);

	wrapped = connection_from_file (filename, &unmanaged, error);
	if (!wrapped)
		return NULL;

	udi = get_udi_for_connection (wrapped, g_connection, hal_mgr, DEVICE_TYPE_UNKNOWN);

	object = (GObject *) g_object_new (NM_TYPE_IFCFG_CONNECTION,
	                                   NM_IFCFG_CONNECTION_FILENAME, filename,
	                                   NM_IFCFG_CONNECTION_UNMANAGED, unmanaged,
	                                   NM_IFCFG_CONNECTION_UDI, udi,
	                                   NM_EXPORTED_CONNECTION_CONNECTION, wrapped,
	                                   NULL);
	if (object && !udi) {
		NMIfcfgConnectionPrivate *priv = NM_IFCFG_CONNECTION_GET_PRIVATE (object);

		priv->hal_mgr = g_object_ref (hal_mgr);
		priv->g_connection = dbus_g_connection_ref (g_connection);
		priv->daid = g_signal_connect (priv->hal_mgr, "device-added", G_CALLBACK (device_added_cb), object);
	}

	g_object_unref (wrapped);
	g_free (udi);
	return (NMIfcfgConnection *) object;
}

const char *
nm_ifcfg_connection_get_filename (NMIfcfgConnection *self)
{
	g_return_val_if_fail (NM_IS_IFCFG_CONNECTION (self), NULL);

	return NM_IFCFG_CONNECTION_GET_PRIVATE (self)->filename;
}

const char *
nm_ifcfg_connection_get_udi (NMIfcfgConnection *self)
{
	g_return_val_if_fail (NM_IS_IFCFG_CONNECTION (self), NULL);

	return NM_IFCFG_CONNECTION_GET_PRIVATE (self)->udi;
}

gboolean
nm_ifcfg_connection_get_unmanaged (NMIfcfgConnection *self)
{
	g_return_val_if_fail (NM_IS_IFCFG_CONNECTION (self), FALSE);

	return NM_IFCFG_CONNECTION_GET_PRIVATE (self)->unmanaged;
}

static GHashTable *
get_settings (NMExportedConnection *exported)
{
	return nm_connection_to_hash (nm_exported_connection_get_connection (exported));
}

static const char *
get_id (NMExportedConnection *exported)
{
	return NM_IFCFG_CONNECTION_GET_PRIVATE (exported)->filename;
}

static gboolean
update (NMExportedConnection *exported, GHashTable *new_settings, GError **error)
{
//	write_connection (NM_IFCFG_CONNECTION (exported));
	return TRUE;
}

static gboolean
delete (NMExportedConnection *exported, GError **error)
{
	NMIfcfgConnectionPrivate *priv = NM_IFCFG_CONNECTION_GET_PRIVATE (exported);

	g_unlink (priv->filename);
	if (priv->keyfile)
		g_unlink (priv->keyfile);

	return TRUE;
}

/* GObject */

static void
nm_ifcfg_connection_init (NMIfcfgConnection *connection)
{
}

static void
finalize (GObject *object)
{
	NMIfcfgConnectionPrivate *priv = NM_IFCFG_CONNECTION_GET_PRIVATE (object);
	NMConnection *wrapped;

	wrapped = nm_exported_connection_get_connection (NM_EXPORTED_CONNECTION (object));
	if (wrapped)
		nm_connection_clear_secrets (wrapped);

	g_free (priv->filename);
	g_free (priv->udi);

	if (priv->hal_mgr) {
		if (priv->daid)
			g_signal_handler_disconnect (G_OBJECT (priv->hal_mgr), priv->daid);

		g_object_unref (priv->hal_mgr);
	}

	if (priv->g_connection)
		dbus_g_connection_unref (priv->g_connection);

	G_OBJECT_CLASS (nm_ifcfg_connection_parent_class)->finalize (object);
}

static void
set_property (GObject *object, guint prop_id,
		    const GValue *value, GParamSpec *pspec)
{
	NMIfcfgConnectionPrivate *priv = NM_IFCFG_CONNECTION_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_FILENAME:
		/* Construct only */
		priv->filename = g_value_dup_string (value);
		break;
	case PROP_UNMANAGED:
		priv->unmanaged = g_value_get_boolean (value);
		break;
	case PROP_UDI:
		/* Construct only */
		priv->udi = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
get_property (GObject *object, guint prop_id,
		    GValue *value, GParamSpec *pspec)
{
	NMIfcfgConnectionPrivate *priv = NM_IFCFG_CONNECTION_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_FILENAME:
		g_value_set_string (value, priv->filename);
		break;
	case PROP_UNMANAGED:
		g_value_set_boolean (value, priv->unmanaged);
		break;
	case PROP_UDI:
		g_value_set_string (value, priv->udi);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_ifcfg_connection_class_init (NMIfcfgConnectionClass *ifcfg_connection_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (ifcfg_connection_class);
	NMExportedConnectionClass *connection_class = NM_EXPORTED_CONNECTION_CLASS (ifcfg_connection_class);

	g_type_class_add_private (ifcfg_connection_class, sizeof (NMIfcfgConnectionPrivate));

	/* Virtual methods */
	object_class->set_property = set_property;
	object_class->get_property = get_property;
	object_class->finalize     = finalize;

	connection_class->get_settings = get_settings;
	connection_class->get_id       = get_id;
	connection_class->update       = update;
	connection_class->delete       = delete;

	/* Properties */
	g_object_class_install_property
		(object_class, PROP_FILENAME,
		 g_param_spec_string (NM_IFCFG_CONNECTION_FILENAME,
						  "FileName",
						  "File name",
						  NULL,
						  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property
		(object_class, PROP_UNMANAGED,
		 g_param_spec_boolean (NM_IFCFG_CONNECTION_UNMANAGED,
						  "Unmanaged",
						  "Unmanaged",
						  FALSE,
						  G_PARAM_READWRITE));

	g_object_class_install_property
		(object_class, PROP_UDI,
		 g_param_spec_string (NM_IFCFG_CONNECTION_UDI,
						  "UDI",
						  "UDI",
						  NULL,
						  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}