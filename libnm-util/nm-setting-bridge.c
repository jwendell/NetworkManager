/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */

/*
 * Thomas Graf <tgraf@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2011 - 2012 Red Hat, Inc.
 */

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <dbus/dbus-glib.h>

#include "nm-setting-bridge.h"
#include "nm-param-spec-specialized.h"
#include "nm-setting-private.h"
#include "nm-utils.h"
#include "nm-utils-private.h"
#include "nm-dbus-glib-types.h"

/**
 * SECTION:nm-setting-bridge
 * @short_description: Describes connection properties for bridges
 * @include: nm-setting-bridge.h
 *
 * The #NMSettingBridge object is a #NMSetting subclass that describes properties
 * necessary for bridging connections.
 **/

/**
 * nm_setting_bridge_error_quark:
 *
 * Registers an error quark for #NMSettingBridge if necessary.
 *
 * Returns: the error quark used for #NMSettingBridge errors.
 **/
GQuark
nm_setting_bridge_error_quark (void)
{
	static GQuark quark;

	if (G_UNLIKELY (!quark))
		quark = g_quark_from_static_string ("nm-setting-bridge-error-quark");
	return quark;
}


G_DEFINE_TYPE_WITH_CODE (NMSettingBridge, nm_setting_bridge, NM_TYPE_SETTING,
                         _nm_register_setting (NM_SETTING_BRIDGE_SETTING_NAME,
                                               g_define_type_id,
                                               1,
                                               NM_SETTING_BRIDGE_ERROR))
NM_SETTING_REGISTER_TYPE (NM_TYPE_SETTING_BRIDGE)

#define NM_SETTING_BRIDGE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_SETTING_BRIDGE, NMSettingBridgePrivate))

typedef struct {
	char *   interface_name;
	gboolean stp;
	guint16  priority;
	guint16  forward_delay;
	guint16  hello_time;
	guint16  max_age;
	guint32  ageing_time;
} NMSettingBridgePrivate;

enum {
	PROP_0,
	PROP_INTERFACE_NAME,
	PROP_STP,
	PROP_PRIORITY,
	PROP_FORWARD_DELAY,
	PROP_HELLO_TIME,
	PROP_MAX_AGE,
	PROP_AGEING_TIME,
	LAST_PROP
};

/**
 * nm_setting_bridge_new:
 *
 * Creates a new #NMSettingBridge object with default values.
 *
 * Returns: (transfer full): the new empty #NMSettingBridge object
 **/
NMSetting *
nm_setting_bridge_new (void)
{
	return (NMSetting *) g_object_new (NM_TYPE_SETTING_BRIDGE, NULL);
}

/**
 * nm_setting_bridge_get_interface_name:
 * @setting: the #NMSettingBridge
 *
 * Returns: the #NMSettingBridge:interface-name property of the setting
 **/
const char *
nm_setting_bridge_get_interface_name (NMSettingBridge *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_BRIDGE (setting), 0);

	return NM_SETTING_BRIDGE_GET_PRIVATE (setting)->interface_name;
}

/**
 * nm_setting_bridge_get_stp:
 * @setting: the #NMSettingBridge
 *
 * Returns: the #NMSettingBridge:stp property of the setting
 **/
gboolean
nm_setting_bridge_get_stp (NMSettingBridge *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_BRIDGE (setting), FALSE);

	return NM_SETTING_BRIDGE_GET_PRIVATE (setting)->stp;
}

/**
 * nm_setting_bridge_get_priority:
 * @setting: the #NMSettingBridge
 *
 * Returns: the #NMSettingBridge:priority property of the setting
 **/
guint16
nm_setting_bridge_get_priority (NMSettingBridge *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_BRIDGE (setting), 0);

	return NM_SETTING_BRIDGE_GET_PRIVATE (setting)->priority;
}

/**
 * nm_setting_bridge_get_forward_delay:
 * @setting: the #NMSettingBridge
 *
 * Returns: the #NMSettingBridge:forward-delay property of the setting
 **/
guint16
nm_setting_bridge_get_forward_delay (NMSettingBridge *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_BRIDGE (setting), 0);

	return NM_SETTING_BRIDGE_GET_PRIVATE (setting)->forward_delay;
}

/**
 * nm_setting_bridge_get_hello_time:
 * @setting: the #NMSettingBridge
 *
 * Returns: the #NMSettingBridge:hello-time property of the setting
 **/
guint16
nm_setting_bridge_get_hello_time (NMSettingBridge *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_BRIDGE (setting), 0);

	return NM_SETTING_BRIDGE_GET_PRIVATE (setting)->hello_time;
}

/**
 * nm_setting_bridge_get_max_age:
 * @setting: the #NMSettingBridge
 *
 * Returns: the #NMSettingBridge:max-age property of the setting
 **/
guint16
nm_setting_bridge_get_max_age (NMSettingBridge *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_BRIDGE (setting), 0);

	return NM_SETTING_BRIDGE_GET_PRIVATE (setting)->max_age;
}

/**
 * nm_setting_bridge_get_ageing_time:
 * @setting: the #NMSettingBridge
 *
 * Returns: the #NMSettingBridge:ageing-time property of the setting
 **/
guint
nm_setting_bridge_get_ageing_time (NMSettingBridge *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_BRIDGE (setting), 0);

	return NM_SETTING_BRIDGE_GET_PRIVATE (setting)->ageing_time;
}

/* IEEE 802.1D-1998 timer values */
#define BR_MIN_HELLO_TIME    1
#define BR_MAX_HELLO_TIME    10

#define BR_MIN_FORWARD_DELAY 2
#define BR_MAX_FORWARD_DELAY 30

#define BR_MIN_MAX_AGE       6
#define BR_MAX_MAX_AGE       40

/* IEEE 802.1D-1998 Table 7.4 */
#define BR_MIN_AGEING_TIME   0
#define BR_MAX_AGEING_TIME   1000000

static inline gboolean
check_range (guint32 val,
             guint32 min,
             guint32 max,
             const char *prop,
             GError **error)
{
	if ((val != 0) && (val < min || val > max)) {
		g_set_error_literal (error,
		                     NM_SETTING_BRIDGE_ERROR,
		                     NM_SETTING_BRIDGE_ERROR_INVALID_PROPERTY,
		                     prop);
		return FALSE;
	}
	return TRUE;
}

static gboolean
verify (NMSetting *setting, GSList *all_settings, GError **error)
{
	NMSettingBridgePrivate *priv = NM_SETTING_BRIDGE_GET_PRIVATE (setting);

	if (!priv->interface_name || !strlen(priv->interface_name)) {
		g_set_error (error,
		             NM_SETTING_BRIDGE_ERROR,
		             NM_SETTING_BRIDGE_ERROR_MISSING_PROPERTY,
		             NM_SETTING_BRIDGE_INTERFACE_NAME);
		return FALSE;
	}

	if (!nm_utils_iface_valid_name (priv->interface_name)) {
		g_set_error (error,
		             NM_SETTING_BRIDGE_ERROR,
		             NM_SETTING_BRIDGE_ERROR_INVALID_PROPERTY,
		             NM_SETTING_BRIDGE_INTERFACE_NAME);
		return FALSE;
	}

	if (!check_range (priv->forward_delay,
	                  BR_MIN_FORWARD_DELAY,
	                  BR_MAX_FORWARD_DELAY,
	                  NM_SETTING_BRIDGE_FORWARD_DELAY,
	                  error))
		return FALSE;

	if (!check_range (priv->hello_time,
	                  BR_MIN_HELLO_TIME,
	                  BR_MAX_HELLO_TIME,
	                  NM_SETTING_BRIDGE_HELLO_TIME,
	                  error))
		return FALSE;

	if (!check_range (priv->max_age,
	                  BR_MIN_MAX_AGE,
	                  BR_MAX_MAX_AGE,
	                  NM_SETTING_BRIDGE_MAX_AGE,
	                  error))
		return FALSE;

	if (!check_range (priv->ageing_time,
	                  BR_MIN_AGEING_TIME,
	                  BR_MAX_AGEING_TIME,
	                  NM_SETTING_BRIDGE_AGEING_TIME,
	                  error))
		return FALSE;

	return TRUE;
}

static const char *
get_virtual_iface_name (NMSetting *setting)
{
	NMSettingBridge *self = NM_SETTING_BRIDGE (setting);

	return nm_setting_bridge_get_interface_name (self);
}

static void
nm_setting_bridge_init (NMSettingBridge *setting)
{
	g_object_set (setting, NM_SETTING_NAME, NM_SETTING_BRIDGE_SETTING_NAME, NULL);
}

static void
finalize (GObject *object)
{
	NMSettingBridgePrivate *priv = NM_SETTING_BRIDGE_GET_PRIVATE (object);

	g_free (priv->interface_name);

	G_OBJECT_CLASS (nm_setting_bridge_parent_class)->finalize (object);
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
	NMSettingBridgePrivate *priv = NM_SETTING_BRIDGE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_INTERFACE_NAME:
		g_free (priv->interface_name);
		priv->interface_name = g_value_dup_string (value);
		break;
	case PROP_STP:
		priv->stp = g_value_get_boolean (value);
		break;
	case PROP_PRIORITY:
		priv->priority = (guint16) g_value_get_uint (value);
		break;
	case PROP_FORWARD_DELAY:
		priv->forward_delay = (guint16) g_value_get_uint (value);
		break;
	case PROP_HELLO_TIME:
		priv->hello_time = (guint16) g_value_get_uint (value);
		break;
	case PROP_MAX_AGE:
		priv->max_age = (guint16) g_value_get_uint (value);
		break;
	case PROP_AGEING_TIME:
		priv->ageing_time = g_value_get_uint (value);
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
	NMSettingBridgePrivate *priv = NM_SETTING_BRIDGE_GET_PRIVATE (object);
	NMSettingBridge *setting = NM_SETTING_BRIDGE (object);

	switch (prop_id) {
	case PROP_INTERFACE_NAME:
		g_value_set_string (value, nm_setting_bridge_get_interface_name (setting));
		break;
	case PROP_STP:
		g_value_set_boolean (value, priv->stp);
		break;
	case PROP_PRIORITY:
		g_value_set_uint (value, priv->priority);
		break;
	case PROP_FORWARD_DELAY:
		g_value_set_uint (value, priv->forward_delay);
		break;
	case PROP_HELLO_TIME:
		g_value_set_uint (value, priv->hello_time);
		break;
	case PROP_MAX_AGE:
		g_value_set_uint (value, priv->max_age);
		break;
	case PROP_AGEING_TIME:
		g_value_set_uint (value, priv->ageing_time);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_setting_bridge_class_init (NMSettingBridgeClass *setting_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (setting_class);
	NMSettingClass *parent_class = NM_SETTING_CLASS (setting_class);

	g_type_class_add_private (setting_class, sizeof (NMSettingBridgePrivate));

	/* virtual methods */
	object_class->set_property = set_property;
	object_class->get_property = get_property;
	object_class->finalize     = finalize;
	parent_class->verify       = verify;
	parent_class->get_virtual_iface_name = get_virtual_iface_name;

	/* Properties */
	/**
	 * NMSettingBridge:interface-name:
	 *
	 * The name of the virtual in-kernel briding network interface
	 **/
	g_object_class_install_property
		(object_class, PROP_INTERFACE_NAME,
		 g_param_spec_string (NM_SETTING_BRIDGE_INTERFACE_NAME,
		                      "InterfaceName",
		                      "The name of the virtual in-kernel bridging network interface",
		                      NULL,
		                      G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE));

	/**
	 * NMSettingBridge:stp:
	 *
	 * Controls whether Spanning Tree Protocol (STP) is enabled for this bridge.
	 **/
	 g_object_class_install_property
		 (object_class, PROP_STP,
		  g_param_spec_boolean (NM_SETTING_BRIDGE_STP,
		                        "STP",
		                        "Controls whether Spanning Tree Protocol (STP) "
		                        "is enabled for this bridge.",
		                        TRUE,
		                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | NM_SETTING_PARAM_SERIALIZE));

	/**
	 * NMSettingBridge:priority:
	 *
	 * Sets the Spanning Tree Protocol (STP) priority for this bridge.  Lower
	 * values are "better"; the lowest priority bridge will be elected the root
	 * bridge.
	 **/
	 g_object_class_install_property
		 (object_class, PROP_PRIORITY,
		  g_param_spec_uint (NM_SETTING_BRIDGE_PRIORITY,
		                     "Priority",
		                     "Sets the Spanning Tree Protocol (STP) priority "
		                     "for this bridge.  Lower values are 'better'; the "
		                     "lowest priority bridge will be elected the root "
		                     "bridge.",
		                     0, G_MAXUINT16, 0x80,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT | NM_SETTING_PARAM_SERIALIZE));

	/**
	 * NMSettingBridge:forward-delay:
	 *
	 * The Spanning Tree Protocol (STP) forwarding delay, in seconds.
	 **/
	 g_object_class_install_property
		 (object_class, PROP_FORWARD_DELAY,
		  g_param_spec_uint (NM_SETTING_BRIDGE_FORWARD_DELAY,
		                     "ForwardDelay",
		                     "The Spanning Tree Protocol (STP) forwarding "
		                     "delay, in seconds.",
		                     0, BR_MAX_FORWARD_DELAY, 15,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT | NM_SETTING_PARAM_SERIALIZE));

	/**
	 * NMSettingBridge:hello-time:
	 *
	 * The Spanning Tree Protocol (STP) hello time, in seconds.
	 **/
	 g_object_class_install_property
		 (object_class, PROP_HELLO_TIME,
		  g_param_spec_uint (NM_SETTING_BRIDGE_HELLO_TIME,
		                     "HelloTime",
		                     "The Spanning Tree Protocol (STP) hello time, in "
		                     "seconds.",
		                     0, BR_MAX_HELLO_TIME, 2,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT | NM_SETTING_PARAM_SERIALIZE));

	/**
	 * NMSettingBridge:max-age:
	 *
	 * The Spanning Tree Protocol (STP) maximum message age, in seconds.
	 **/
	 g_object_class_install_property
		 (object_class, PROP_MAX_AGE,
		  g_param_spec_uint (NM_SETTING_BRIDGE_MAX_AGE,
		                     "MaxAge",
		                     "The Spanning Tree Protocol (STP) maximum message "
		                     "age, in seconds.",
		                     0, BR_MAX_MAX_AGE, 20,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT | NM_SETTING_PARAM_SERIALIZE));

	/**
	 * NMSettingBridge:ageing-time:
	 *
	 * The ethernet MAC address aging time, in seconds.
	 **/
	 g_object_class_install_property
		 (object_class, PROP_AGEING_TIME,
		  g_param_spec_uint (NM_SETTING_BRIDGE_AGEING_TIME,
		                     "AgeingTime",
		                     "The ethernet MAC address aging time, in seconds.",
		                     0, BR_MAX_AGEING_TIME, 300,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT | NM_SETTING_PARAM_SERIALIZE));
}

