/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
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
 * (C) Copyright 2010 - 2011 Red Hat, Inc.
 */

#include <config.h>
#include <ctype.h>
#include <string.h>
#include <NetworkManager.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "nm-secret-agent.h"
#include "nm-marshal.h"
#include "NetworkManager.h"

static void impl_secret_agent_get_secrets (NMSecretAgent *self,
                                           GHashTable *connection_hash,
                                           const char *connection_path,
                                           const char *setting_name,
                                           const char **hints,
                                           guint32 flags,
                                           DBusGMethodInvocation *context);

static void impl_secret_agent_cancel_get_secrets (NMSecretAgent *self,
                                                  const char *connection_path,
                                                  const char *setting_name,
                                                  DBusGMethodInvocation *context);

static void impl_secret_agent_save_secrets (NMSecretAgent *self,
                                            GHashTable *connection_hash,
                                            const char *connection_path,
                                            DBusGMethodInvocation *context);

static void impl_secret_agent_delete_secrets (NMSecretAgent *self,
                                              GHashTable *connection_hash,
                                              const char *connection_path,
                                              DBusGMethodInvocation *context);

#include "nm-secret-agent-glue.h"

G_DEFINE_ABSTRACT_TYPE (NMSecretAgent, nm_secret_agent, G_TYPE_OBJECT)

#define NM_SECRET_AGENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
                                        NM_TYPE_SECRET_AGENT, \
                                        NMSecretAgentPrivate))

static gboolean auto_register_cb (gpointer user_data);

typedef struct {
	gboolean registered;

	DBusGConnection *bus;
	DBusGProxy *dbus_proxy;
	DBusGProxy *manager_proxy;
	DBusGProxyCall *reg_call;

	char *nm_owner;

	char *identifier;
	gboolean auto_register;
	gboolean suppress_auto;
	gboolean auto_register_id;

	gboolean disposed;
} NMSecretAgentPrivate;

enum {
	PROP_0,
	PROP_IDENTIFIER,
	PROP_AUTO_REGISTER,

	LAST_PROP
};

enum {
	REGISTRATION_RESULT,

	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };


/********************************************************************/

GQuark
nm_secret_agent_error_quark (void)
{
	static GQuark ret = 0;

	if (G_UNLIKELY (ret == 0))
		ret = g_quark_from_static_string ("nm-secret-agent-error");
	return ret;
}

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
nm_secret_agent_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] = {
			/* Sender is not authorized to make this request */
			ENUM_ENTRY (NM_SECRET_AGENT_ERROR_NOT_AUTHORIZED, "NotAuthorized"),
			/* Given connection details do not make a valid connection */
			ENUM_ENTRY (NM_SECRET_AGENT_ERROR_INVALID_CONNECTION, "InvalidConnection"),
			/* The request was canceled explicitly by the user */
			ENUM_ENTRY (NM_SECRET_AGENT_ERROR_USER_CANCELED, "UserCanceled"),
			/* The request was canceled, but not by the user */
			ENUM_ENTRY (NM_SECRET_AGENT_ERROR_AGENT_CANCELED, "AgentCanceled"),
			/* Some internal error prevented returning secrets */
			ENUM_ENTRY (NM_SECRET_AGENT_ERROR_INTERNAL_ERROR, "InternalError"),
			/* No secrets could be found to fulfill the request */
			ENUM_ENTRY (NM_SECRET_AGENT_ERROR_NO_SECRETS, "NoSecrets"),
			{ 0, 0, 0 }
		};
		etype = g_enum_register_static ("NMSecretAgentError", values);
	}
	return etype;
}

/*************************************************************/

static const char *
get_nm_owner (NMSecretAgent *self)
{
	NMSecretAgentPrivate *priv = NM_SECRET_AGENT_GET_PRIVATE (self);
	GError *error = NULL;
	char *owner;

	if (!priv->nm_owner) {
		if (!dbus_g_proxy_call_with_timeout (priv->dbus_proxy,
			                                 "GetNameOwner", 2000, &error,
			                                 G_TYPE_STRING, NM_DBUS_SERVICE,
			                                 G_TYPE_INVALID,
			                                 G_TYPE_STRING, &owner,
			                                 G_TYPE_INVALID))
			return NULL;

		priv->nm_owner = g_strdup (owner);
		g_free (owner);
	}

	return priv->nm_owner;
}

static void
_internal_unregister (NMSecretAgent *self)
{
	NMSecretAgentPrivate *priv = NM_SECRET_AGENT_GET_PRIVATE (self);

	if (priv->registered) {
		dbus_g_connection_unregister_g_object (priv->bus, G_OBJECT (self));
		priv->registered = FALSE;
	}
}

static void
name_owner_changed (DBusGProxy *proxy,
                    const char *name,
                    const char *old_owner,
                    const char *new_owner,
                    gpointer user_data)
{
	NMSecretAgent *self = NM_SECRET_AGENT (user_data);
	NMSecretAgentPrivate *priv = NM_SECRET_AGENT_GET_PRIVATE (self);
	gboolean old_owner_good = (old_owner && strlen (old_owner));
	gboolean new_owner_good = (new_owner && strlen (new_owner));

	if (strcmp (name, NM_DBUS_SERVICE) == 0) {
		g_free (priv->nm_owner);
		priv->nm_owner = g_strdup (new_owner);

		if (!old_owner_good && new_owner_good) {
			/* NM appeared */
			auto_register_cb (self);
		} else if (old_owner_good && !new_owner_good) {
			/* NM disappeared */
			_internal_unregister (self);
		} else if (old_owner_good && new_owner_good && strcmp (old_owner, new_owner)) {
			/* Hmm, NM magically restarted */
			_internal_unregister (self);
			auto_register_cb (self);
		}
	}
}

static gboolean
verify_request (NMSecretAgent *self,
                DBusGMethodInvocation *context,
                GHashTable *connection_hash,
                const char *connection_path,
                NMConnection **out_connection,
                GError **error)
{
	NMSecretAgentPrivate *priv = NM_SECRET_AGENT_GET_PRIVATE (self);
	NMConnection *connection = NULL;
	DBusConnection *bus;
	char *sender;
	const char *nm_owner;
	DBusError dbus_error;
	uid_t sender_uid = G_MAXUINT;
	GError *local = NULL;

	g_return_val_if_fail (context != NULL, FALSE);

	/* Verify the sender's UID is 0, and that the sender is the same as
	 * NetworkManager's bus name owner.
	 */

	nm_owner = get_nm_owner (self);
	if (!nm_owner) {
		g_set_error_literal (error,
		                     NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_NOT_AUTHORIZED,
		                     "NetworkManager bus name owner unknown.");
		return FALSE;
	}

	bus = dbus_g_connection_get_connection (priv->bus);
	if (!bus) {
		g_set_error_literal (error,
		                     NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_NOT_AUTHORIZED,
		                     "Failed to get DBus connection.");
		return FALSE;
	}

	sender = dbus_g_method_get_sender (context);
	if (!sender) {
		g_set_error_literal (error,
		                     NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_NOT_AUTHORIZED,
		                     "Failed to get request sender.");
		return FALSE;
	}

	/* Check that the sender matches the current NM bus name owner */
	if (strcmp (sender, nm_owner) != 0) {
		g_set_error_literal (error,
		                     NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_NOT_AUTHORIZED,
		                     "Request sender does not match NetworkManager bus name owner.");
		goto out;
	}

	dbus_error_init (&dbus_error);
	sender_uid = dbus_bus_get_unix_user (bus, sender, &dbus_error);
	if (dbus_error_is_set (&dbus_error)) {
		g_set_error (error,
		             NM_SECRET_AGENT_ERROR,
		             NM_SECRET_AGENT_ERROR_NOT_AUTHORIZED,
		             "Failed to get request unix user: (%s) %s.",
		             dbus_error.name, dbus_error.message);
		dbus_error_free (&dbus_error);
		goto out;
	}

	if (0 != sender_uid) {
		g_set_error_literal (error,
		                     NM_SECRET_AGENT_ERROR,
		                     NM_SECRET_AGENT_ERROR_NOT_AUTHORIZED,
		                     "Request sender is not root.");
		goto out;
	}

	/* And make sure the connection is actually valid */
	if (connection_hash) {
		connection = nm_connection_new_from_hash (connection_hash, &local);
		if (connection && connection_path) {
			nm_connection_set_path (connection, connection_path);
		} else {
			g_set_error (error,
				         NM_SECRET_AGENT_ERROR,
				         NM_SECRET_AGENT_ERROR_INVALID_CONNECTION,
				         "Invalid connection: (%d) %s",
				         local ? local->code : -1,
				         (local && local->message) ? local->message : "(unknown)");
			g_clear_error (&local);
		}
	}

out:
	if (out_connection)
		*out_connection = connection;
	g_free (sender);

	return !!connection;
}

static void
get_secrets_cb (NMSecretAgent *self,
                NMConnection *connection,
                GHashTable *secrets,
                GError *error,
                gpointer user_data)
{
	DBusGMethodInvocation *context = user_data;

	if (error)
		dbus_g_method_return_error (context, error);
	else
		dbus_g_method_return (context, secrets);
}

static void
impl_secret_agent_get_secrets (NMSecretAgent *self,
                               GHashTable *connection_hash,
                               const char *connection_path,
                               const char *setting_name,
                               const char **hints,
                               guint32 flags,
                               DBusGMethodInvocation *context)
{
	GError *error = NULL;
	NMConnection *connection = NULL;

	/* Make sure the request comes from NetworkManager and is valid */
	if (!verify_request (self, context, connection_hash, connection_path, &connection, &error)) {
		dbus_g_method_return_error (context, error);
		g_clear_error (&error);
		return;
	}

	NM_SECRET_AGENT_GET_CLASS (self)->get_secrets (self,
	                                               connection,
	                                               connection_path,
	                                               setting_name,
	                                               hints,
	                                               flags,
	                                               get_secrets_cb,
	                                               context);
	g_object_unref (connection);
}

static void
impl_secret_agent_cancel_get_secrets (NMSecretAgent *self,
                                      const char *connection_path,
                                      const char *setting_name,
                                      DBusGMethodInvocation *context)
{
	GError *error = NULL;

	/* Make sure the request comes from NetworkManager and is valid */
	if (!verify_request (self, context, NULL, NULL, NULL, &error)) {
		dbus_g_method_return_error (context, error);
		g_clear_error (&error);
	} else {
		NM_SECRET_AGENT_GET_CLASS (self)->cancel_get_secrets (self, connection_path, setting_name);
		dbus_g_method_return (context);
	}
}

static void
save_secrets_cb (NMSecretAgent *self,
                 NMConnection *connection,
                 GError *error,
                 gpointer user_data)
{
	DBusGMethodInvocation *context = user_data;

	if (error)
		dbus_g_method_return_error (context, error);
	else
		dbus_g_method_return (context);
}

static void
impl_secret_agent_save_secrets (NMSecretAgent *self,
                                GHashTable *connection_hash,
                                const char *connection_path,
                                DBusGMethodInvocation *context)
{
	GError *error = NULL;
	NMConnection *connection = NULL;

	/* Make sure the request comes from NetworkManager and is valid */
	if (!verify_request (self, context, connection_hash, connection_path, &connection, &error)) {
		dbus_g_method_return_error (context, error);
		g_clear_error (&error);
		return;
	}

	NM_SECRET_AGENT_GET_CLASS (self)->save_secrets (self,
	                                                connection,
	                                                connection_path,
	                                                save_secrets_cb,
	                                                context);
	g_object_unref (connection);
}

static void
delete_secrets_cb (NMSecretAgent *self,
                   NMConnection *connection,
                   GError *error,
                   gpointer user_data)
{
	DBusGMethodInvocation *context = user_data;

	if (error)
		dbus_g_method_return_error (context, error);
	else
		dbus_g_method_return (context);
}

static void
impl_secret_agent_delete_secrets (NMSecretAgent *self,
                                  GHashTable *connection_hash,
                                  const char *connection_path,
                                  DBusGMethodInvocation *context)
{
	GError *error = NULL;
	NMConnection *connection = NULL;

	/* Make sure the request comes from NetworkManager and is valid */
	if (!verify_request (self, context, connection_hash, connection_path, &connection, &error)) {
		dbus_g_method_return_error (context, error);
		g_clear_error (&error);
		return;
	}

	NM_SECRET_AGENT_GET_CLASS (self)->delete_secrets (self,
	                                                  connection,
	                                                  connection_path,
	                                                  delete_secrets_cb,
	                                                  context);
	g_object_unref (connection);
}

/**************************************************************/

static void
reg_request_cb (DBusGProxy *proxy,
                DBusGProxyCall *call,
                gpointer user_data)
{
	NMSecretAgent *self = NM_SECRET_AGENT (user_data);
	NMSecretAgentPrivate *priv = NM_SECRET_AGENT_GET_PRIVATE (self);
	GError *error = NULL;

	priv->reg_call = NULL;

	if (dbus_g_proxy_end_call (proxy, call, &error, G_TYPE_INVALID))
		priv->registered = TRUE;
	else {
		/* If registration failed we shouldn't expose ourselves on the bus */
		_internal_unregister (self);
	}

	g_signal_emit (self, signals[REGISTRATION_RESULT], 0, error);
	g_clear_error (&error);
}

/**
 * nm_secret_agent_register:
 *
 * Registers the #NMSecretAgent with the NetworkManager secret manager,
 * indicating to NetworkManager that the agent is able to provide and save
 * secrets for connections on behalf of its user.  Registration is an
 * asynchronous operation and its success or failure is indicated via the
 * 'registration-result' signal.
 *
 * Returns: a new %TRUE if registration was successfully requested (this does
 * not mean registration itself was successful), %FALSE if registration was not
 * successfully requested.
 **/
gboolean
nm_secret_agent_register (NMSecretAgent *self)
{
	NMSecretAgentPrivate *priv;
	NMSecretAgentClass *class;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (NM_IS_SECRET_AGENT (self), FALSE);

	priv = NM_SECRET_AGENT_GET_PRIVATE (self);

	g_return_val_if_fail (priv->registered == FALSE, FALSE);
	g_return_val_if_fail (priv->reg_call == NULL, FALSE);
	g_return_val_if_fail (priv->bus != NULL, FALSE);
	g_return_val_if_fail (priv->manager_proxy != NULL, FALSE);

	/* Also make sure the subclass can actually respond to secrets requests */
	class = NM_SECRET_AGENT_GET_CLASS (self);
	g_return_val_if_fail (class->get_secrets != NULL, FALSE);
	g_return_val_if_fail (class->save_secrets != NULL, FALSE);
	g_return_val_if_fail (class->delete_secrets != NULL, FALSE);

	priv->suppress_auto = FALSE;

	/* Export our secret agent interface before registering with the manager */
	dbus_g_connection_register_g_object (priv->bus,
	                                     NM_DBUS_PATH_SECRET_AGENT,
	                                     G_OBJECT (self));

	priv->reg_call = dbus_g_proxy_begin_call_with_timeout (priv->manager_proxy,
	                                                       "Register",
	                                                       reg_request_cb,
	                                                       self,
	                                                       NULL,
	                                                       5000,
	                                                       G_TYPE_STRING, priv->identifier,
	                                                       G_TYPE_INVALID);

	return TRUE;
}

/**
 * nm_secret_agent_unregister:
 *
 * Unregisters the #NMSecretAgent with the NetworkManager secret manager,
 * indicating to NetworkManager that the agent is will no longer provide or
 * store secrets on behalf of this user.
 *
 * Returns: a new %TRUE if unregistration was successful, %FALSE if it was not.
 **/
gboolean
nm_secret_agent_unregister (NMSecretAgent *self)
{
	NMSecretAgentPrivate *priv;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (NM_IS_SECRET_AGENT (self), FALSE);

	priv = NM_SECRET_AGENT_GET_PRIVATE (self);

	g_return_val_if_fail (priv->registered == TRUE, FALSE);
	g_return_val_if_fail (priv->bus != NULL, FALSE);
	g_return_val_if_fail (priv->manager_proxy != NULL, FALSE);

	dbus_g_proxy_call_no_reply (priv->manager_proxy, "Unregister", G_TYPE_INVALID);

	_internal_unregister (self);
	priv->suppress_auto = TRUE;

	return TRUE;
}

static gboolean
auto_register_cb (gpointer user_data)
{
	NMSecretAgent *self = NM_SECRET_AGENT (user_data);
	NMSecretAgentPrivate *priv = NM_SECRET_AGENT_GET_PRIVATE (self);

	priv->auto_register_id = 0;
	if (priv->auto_register && !priv->suppress_auto && (priv->reg_call == NULL))
		nm_secret_agent_register (self);
	return FALSE;
}

/**************************************************************/

/**
 * nm_secret_agent_get_secrets:
 * @self: a #NMSecretAgent
 * @connection: the #NMConnection for which we're asked secrets
 * @setting_name: the name of the secret setting
 * @hints: (array zero-terminated=1): hints to the agent
 * @flags:
 * @callback: (scope async): a callback, invoked when the operation is done
 * @callback_data: (closure):
 *
 * Asyncronously retrieve secrets belonging to @connection for the
 * setting @setting_name.
 *
 * VFunc: get_secrets
 */
void
nm_secret_agent_get_secrets (NMSecretAgent *self,
                             NMConnection *connection,
                             const char *setting_name,
                             const char **hints,
                             guint32 flags,
                             NMSecretAgentGetSecretsFunc callback,
                             gpointer callback_data)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (NM_IS_SECRET_AGENT (self));
	g_return_if_fail (connection != NULL);
	g_return_if_fail (NM_IS_CONNECTION (connection));
	g_return_if_fail (nm_connection_get_path (connection));
	g_return_if_fail (setting_name != NULL);
	g_return_if_fail (strlen (setting_name) > 0);
	g_return_if_fail (callback != NULL);

	NM_SECRET_AGENT_GET_CLASS (self)->get_secrets (self,
	                                               connection,
	                                               nm_connection_get_path (connection),
	                                               setting_name,
	                                               hints,
	                                               flags,
	                                               callback,
	                                               callback_data);
}

/**
 * nm_secret_agent_save_secrets:
 * @self: a #NMSecretAgent
 * @connection: a #NMConnection
 * @callback: (scope async): a callback, invoked when the operation is done
 * @callback_data: (closure):
 *
 * Asyncronously ensure that all secrets inside @connection
 * are stored to disk.
 *
 * VFunc: save_secrets
 */
void
nm_secret_agent_save_secrets (NMSecretAgent *self,
                              NMConnection *connection,
                              NMSecretAgentSaveSecretsFunc callback,
                              gpointer callback_data)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (NM_IS_SECRET_AGENT (self));
	g_return_if_fail (connection != NULL);
	g_return_if_fail (NM_IS_CONNECTION (connection));
	g_return_if_fail (nm_connection_get_path (connection));

	NM_SECRET_AGENT_GET_CLASS (self)->save_secrets (self,
	                                                connection,
	                                                nm_connection_get_path (connection),
	                                                callback,
	                                                callback_data);
}

/**
 * nm_secret_agent_delete_secrets:
 * @self: a #NMSecretAgent
 * @connection: a #NMConnection
 * @callback: (scope async): a callback, invoked when the operation is done
 * @callback_data: (closure):
 *
 * Asynchronously ask the agent to delete all saved secrets belonging to
 * @connection.
 *
 * VFunc: delete_secrets
 */
void
nm_secret_agent_delete_secrets (NMSecretAgent *self,
                                NMConnection *connection,
                                NMSecretAgentDeleteSecretsFunc callback,
                                gpointer callback_data)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (NM_IS_SECRET_AGENT (self));
	g_return_if_fail (connection != NULL);
	g_return_if_fail (NM_IS_CONNECTION (connection));
	g_return_if_fail (nm_connection_get_path (connection));

	NM_SECRET_AGENT_GET_CLASS (self)->delete_secrets (self,
	                                                  connection,
	                                                  nm_connection_get_path (connection),
	                                                  callback,
	                                                  callback_data);
}

/**************************************************************/

static gboolean
validate_identifier (const char *identifier)
{
	const char *p = identifier;
	size_t id_len;

	/* Length between 3 and 255 characters inclusive */
	id_len = strlen (identifier);
	if (id_len < 3 || id_len > 255)
		return FALSE;

	if ((identifier[0] == '.') || (identifier[id_len - 1] == '.'))
		return FALSE;

	/* FIXME: do complete validation here */
	while (p && *p) {
		if (!isalnum (*p) && (*p != '_') && (*p != '-') && (*p != '.'))
			return FALSE;
		if ((*p == '.') && (*(p + 1) == '.'))
			return FALSE;
		p++;
	}

	return TRUE;
}

static void
nm_secret_agent_init (NMSecretAgent *self)
{
	NMSecretAgentPrivate *priv = NM_SECRET_AGENT_GET_PRIVATE (self);
	GError *error = NULL;

	priv->bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (!priv->bus) {
		g_warning ("Couldn't connect to system bus: %s", error->message);
		g_error_free (error);
		return;
	}

	priv->dbus_proxy = dbus_g_proxy_new_for_name (priv->bus,
	                                              DBUS_SERVICE_DBUS,
	                                              DBUS_PATH_DBUS,
	                                              DBUS_INTERFACE_DBUS);
	if (!priv->dbus_proxy) {
		g_warning ("Couldn't create messagebus proxy.");
		return;
	}

	dbus_g_object_register_marshaller (_nm_marshal_VOID__STRING_STRING_STRING,
	                                   G_TYPE_NONE,
	                                   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
	                                   G_TYPE_INVALID);
	dbus_g_proxy_add_signal (priv->dbus_proxy, "NameOwnerChanged",
	                         G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
	                         G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (priv->dbus_proxy,
	                             "NameOwnerChanged",
	                             G_CALLBACK (name_owner_changed),
	                             self, NULL);

	priv->manager_proxy = dbus_g_proxy_new_for_name (priv->bus,
	                                                 NM_DBUS_SERVICE,
	                                                 NM_DBUS_PATH_AGENT_MANAGER,
	                                                 NM_DBUS_INTERFACE_AGENT_MANAGER);
	if (!priv->manager_proxy) {
		g_warning ("Couldn't create NM agent manager proxy.");
		return;
	}

	priv->auto_register_id = g_idle_add (auto_register_cb, self);
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
	NMSecretAgentPrivate *priv = NM_SECRET_AGENT_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_IDENTIFIER:
		g_value_set_string (value, priv->identifier);
		break;
	case PROP_AUTO_REGISTER:
		g_value_set_boolean (value, priv->auto_register);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
	NMSecretAgentPrivate *priv = NM_SECRET_AGENT_GET_PRIVATE (object);
	const char *identifier;

	switch (prop_id) {
	case PROP_IDENTIFIER:
		identifier = g_value_get_string (value);

		g_return_if_fail (validate_identifier (identifier));

		g_free (priv->identifier);
		priv->identifier = g_strdup (identifier);
		break;
	case PROP_AUTO_REGISTER:
		priv->auto_register = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
dispose (GObject *object)
{
	NMSecretAgent *self = NM_SECRET_AGENT (object);
	NMSecretAgentPrivate *priv = NM_SECRET_AGENT_GET_PRIVATE (self);

	if (!priv->disposed) {
		priv->disposed = TRUE;

		if (priv->registered)
			nm_secret_agent_unregister (self);

		if (priv->auto_register_id)
			g_source_remove (priv->auto_register_id);

		g_free (priv->identifier);
		g_free (priv->nm_owner);

		if (priv->dbus_proxy)
			g_object_unref (priv->dbus_proxy);

		if (priv->manager_proxy)
			g_object_unref (priv->manager_proxy);

		if (priv->bus)
			dbus_g_connection_unref (priv->bus);
	}

	G_OBJECT_CLASS (nm_secret_agent_parent_class)->dispose (object);
}

static void
nm_secret_agent_class_init (NMSecretAgentClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	g_type_class_add_private (class, sizeof (NMSecretAgentPrivate));

	/* Virtual methods */
	object_class->dispose = dispose;
	object_class->get_property = get_property;
	object_class->set_property = set_property;

	/**
	 * NMSecretAgent:identifier:
	 *
	 * Identifies this agent; only one agent in each user session may use the
	 * same identifier.  Identifier formatting follows the same rules as
	 * D-Bus bus names with the exception that the ':' character is not
	 * allowed.  The valid set of characters is "[A-Z][a-z][0-9]_-." and the
	 * identifier is limited in length to 255 characters with a minimum
	 * of 3 characters.  An example valid identifier is 'org.gnome.nm-applet'
	 * (without quotes).
	 **/
	g_object_class_install_property
		(object_class, PROP_IDENTIFIER,
		 g_param_spec_string (NM_SECRET_AGENT_IDENTIFIER,
						      "Identifier",
						      "Identifier",
						      NULL,
						      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * NMSecretAgent:auto-register:
	 *
	 * If TRUE, the agent will attempt to automatically register itself after
	 * it is created (via an idle handler) and to re-register itself if
	 * NetworkManager restarts.  If FALSE, the agent does not automatically
	 * register with NetworkManager, and nm_secret_agent_register() must be
	 * called.  If 'auto-register' is TRUE, calling nm_secret_agent_unregister()
	 * will suppress auto-registration until nm_secret_agent_register() is
	 * called, which re-enables auto-registration.
	 **/
	g_object_class_install_property
		(object_class, PROP_AUTO_REGISTER,
		 g_param_spec_boolean (NM_SECRET_AGENT_AUTO_REGISTER,
						       "Auto Register",
						       "Auto Register",
						       TRUE,
						       G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	/**
	 * NMSecretAgent::registration-result:
	 * @agent: the agent that received the signal
	 * @error: the error, if any, that occured while registering
	 *
	 * Indicates the result of a registration request; if @error is NULL the
	 * request was successful.
	 **/
	signals[REGISTRATION_RESULT] =
		g_signal_new (NM_SECRET_AGENT_REGISTRATION_RESULT,
					  G_OBJECT_CLASS_TYPE (object_class),
					  G_SIGNAL_RUN_FIRST,
					  0, NULL, NULL,
					  g_cclosure_marshal_VOID__POINTER,
					  G_TYPE_NONE, 1, G_TYPE_POINTER);

	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (class),
	                                 &dbus_glib_nm_secret_agent_object_info);

	dbus_g_error_domain_register (NM_SECRET_AGENT_ERROR,
	                              NM_DBUS_INTERFACE_SECRET_AGENT,
	                              NM_TYPE_SECRET_AGENT_ERROR);
}
