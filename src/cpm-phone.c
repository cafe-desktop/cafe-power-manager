/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <dbus/dbus-glib.h>

#include "cpm-phone.h"
#include "egg-debug.h"
#include "cpm-marshal.h"

static void     cpm_phone_finalize   (GObject	    *object);

struct CpmPhonePrivate
{
	DBusGProxy		*proxy;
	DBusGConnection		*connection;
	guint			 watch_id;
	gboolean		 present;
	guint			 percentage;
	gboolean		 onac;	 
};

enum {
	DEVICE_ADDED,
	DEVICE_REMOVED,
	DEVICE_REFRESH,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };
static gpointer cpm_phone_object = NULL;

G_DEFINE_TYPE_WITH_PRIVATE (CpmPhone, cpm_phone, G_TYPE_OBJECT)

/**
 * cpm_phone_coldplug:
 * Return value: Success value, or zero for failure
 **/
gboolean
cpm_phone_coldplug (CpmPhone *phone)
{
	GError  *error = NULL;
	gboolean ret;

	g_return_val_if_fail (phone != NULL, FALSE);
	g_return_val_if_fail (CPM_IS_PHONE (phone), FALSE);

	if (phone->priv->proxy == NULL) {
		egg_warning ("not connected");
		return FALSE;
	}

	ret = dbus_g_proxy_call (phone->priv->proxy, "Coldplug", &error,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (error != NULL) {
		egg_warning ("DEBUG: ERROR: %s", error->message);
		g_error_free (error);
	}

	return ret;
}

/**
 * cpm_phone_coldplug:
 * Return value: if present
 **/
gboolean
cpm_phone_get_present (CpmPhone *phone,
		       guint     idx G_GNUC_UNUSED)
{
	g_return_val_if_fail (phone != NULL, FALSE);
	g_return_val_if_fail (CPM_IS_PHONE (phone), FALSE);
	return phone->priv->present;
}

/**
 * cpm_phone_coldplug:
 * Return value: if present
 **/
guint
cpm_phone_get_percentage (CpmPhone *phone,
			  guint     idx G_GNUC_UNUSED)
{
	g_return_val_if_fail (phone != NULL, 0);
	g_return_val_if_fail (CPM_IS_PHONE (phone), 0);
	return phone->priv->percentage;
}

/**
 * cpm_phone_coldplug:
 * Return value: if present
 **/
gboolean
cpm_phone_get_on_ac (CpmPhone *phone,
		     guint     idx G_GNUC_UNUSED)
{
	g_return_val_if_fail (phone != NULL, FALSE);
	g_return_val_if_fail (CPM_IS_PHONE (phone), FALSE);
	return phone->priv->onac;
}

/**
 * cpm_phone_get_num_batteries:
 * Return value: number of phone batteries monitored
 **/
guint
cpm_phone_get_num_batteries (CpmPhone *phone)
{
	g_return_val_if_fail (phone != NULL, 0);
	g_return_val_if_fail (CPM_IS_PHONE (phone), 0);
	if (phone->priv->present) {
		return 1;
	}
	return 0;
}

/** Invoked when we get the BatteryStateChanged
 */
static void
cpm_phone_battery_state_changed (DBusGProxy *proxy G_GNUC_UNUSED,
				 guint       idx,
				 guint       percentage,
				 gboolean    on_ac,
				 CpmPhone   *phone)
{
	g_return_if_fail (CPM_IS_PHONE (phone));

	egg_debug ("got BatteryStateChanged %i = %i (%i)", idx, percentage, on_ac);
	phone->priv->percentage = percentage;
	phone->priv->onac = on_ac;
	phone->priv->present = TRUE;
	egg_debug ("emitting device-refresh : (%i)", idx);
	g_signal_emit (phone, signals [DEVICE_REFRESH], 0, idx);
}

/** Invoked when we get NumberBatteriesChanged
 */
static void
cpm_phone_num_batteries_changed (DBusGProxy *proxy G_GNUC_UNUSED,
				 guint       number,
				 CpmPhone   *phone)
{
	g_return_if_fail (CPM_IS_PHONE (phone));

	egg_debug ("got NumberBatteriesChanged %i", number);
	if (number > 1) {
		egg_warning ("number not 0 or 1, not valid!");
		return;
	}

	/* are we removed? */
	if (number == 0) {
		phone->priv->present = FALSE;
		phone->priv->percentage = 0;
		phone->priv->onac = FALSE;
		egg_debug ("emitting device-removed : (%i)", 0);
		g_signal_emit (phone, signals [DEVICE_REMOVED], 0, 0);
		return;
	}

	if (phone->priv->present) {
		egg_warning ("duplicate NumberBatteriesChanged with no change");
		return;
	}

	/* reset to defaults until we get BatteryStateChanged */
	phone->priv->present = TRUE;
	phone->priv->percentage = 0;
	phone->priv->onac = FALSE;
	egg_debug ("emitting device-added : (%i)", 0);
	g_signal_emit (phone, signals [DEVICE_ADDED], 0, 0);
}

/**
 * cpm_phone_class_init:
 * @klass: This class instance
 **/
static void
cpm_phone_class_init (CpmPhoneClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = cpm_phone_finalize;

	signals [DEVICE_ADDED] =
		g_signal_new ("device-added",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (CpmPhoneClass, device_added),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	signals [DEVICE_REMOVED] =
		g_signal_new ("device-removed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (CpmPhoneClass, device_removed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	signals [DEVICE_REFRESH] =
		g_signal_new ("device-refresh",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (CpmPhoneClass, device_refresh),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
}

/**
 * cpm_phone_service_appeared_cb:
 */
static void
cpm_phone_service_appeared_cb (GDBusConnection *connection G_GNUC_UNUSED,
			       const gchar     *name G_GNUC_UNUSED,
			       const gchar     *name_owner G_GNUC_UNUSED,
			       CpmPhone        *phone)
{
	GError *error = NULL;

	g_return_if_fail (CPM_IS_PHONE (phone));

	if (phone->priv->connection == NULL) {
		egg_debug ("get connection");
		g_clear_error (&error);
		phone->priv->connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
		if (error != NULL) {
			egg_warning ("Could not connect to DBUS daemon: %s", error->message);
			g_error_free (error);
			phone->priv->connection = NULL;
			return;
		}
	}
	if (phone->priv->proxy == NULL) {
		egg_debug ("get proxy");
		g_clear_error (&error);
		phone->priv->proxy = dbus_g_proxy_new_for_name_owner (phone->priv->connection,
							 CAFE_PHONE_MANAGER_DBUS_SERVICE,
							 CAFE_PHONE_MANAGER_DBUS_PATH,
							 CAFE_PHONE_MANAGER_DBUS_INTERFACE,
							 &error);
		if (error != NULL) {
			egg_warning ("Cannot connect, maybe the daemon is not running: %s", error->message);
			g_error_free (error);
			phone->priv->proxy = NULL;
			return;
		}

		/* complicated type. ick */
		dbus_g_object_register_marshaller(cpm_marshal_VOID__UINT_UINT_BOOLEAN,
						  G_TYPE_NONE, G_TYPE_UINT, G_TYPE_UINT,
						  G_TYPE_BOOLEAN, G_TYPE_INVALID);

		/* get BatteryStateChanged */
		dbus_g_proxy_add_signal (phone->priv->proxy, "BatteryStateChanged",
					 G_TYPE_UINT, G_TYPE_UINT, G_TYPE_BOOLEAN, G_TYPE_INVALID);
		dbus_g_proxy_connect_signal (phone->priv->proxy, "BatteryStateChanged",
					     G_CALLBACK (cpm_phone_battery_state_changed),
					     phone, NULL);

		/* get NumberBatteriesChanged */
		dbus_g_proxy_add_signal (phone->priv->proxy, "NumberBatteriesChanged",
					 G_TYPE_UINT, G_TYPE_INVALID);
		dbus_g_proxy_connect_signal (phone->priv->proxy, "NumberBatteriesChanged",
					     G_CALLBACK (cpm_phone_num_batteries_changed),
					     phone, NULL);

	}
}

/**
 * cpm_phone_service_vanished_cb:
 */
static void
cpm_phone_service_vanished_cb (GDBusConnection *connection G_GNUC_UNUSED,
			       const gchar     *name G_GNUC_UNUSED,
			       CpmPhone        *phone)
{
	g_return_if_fail (CPM_IS_PHONE (phone));

	if (phone->priv->proxy != NULL) {
		egg_debug ("removing proxy");
		g_object_unref (phone->priv->proxy);
		phone->priv->proxy = NULL;
		if (phone->priv->present) {
			phone->priv->present = FALSE;
			phone->priv->percentage = 0;
			egg_debug ("emitting device-removed : (%i)", 0);
			g_signal_emit (phone, signals [DEVICE_REMOVED], 0, 0);
		}
	}
	return;
}

/**
 * cpm_phone_init:
 * @phone: This class instance
 **/
static void
cpm_phone_init (CpmPhone *phone)
{
	phone->priv = cpm_phone_get_instance_private (phone);

	phone->priv->connection = NULL;
	phone->priv->proxy = NULL;
	phone->priv->present = FALSE;
	phone->priv->percentage = 0;
	phone->priv->onac = FALSE;

	phone->priv->watch_id = g_bus_watch_name (G_BUS_TYPE_SESSION,
						  CAFE_PHONE_MANAGER_DBUS_SERVICE,
						  G_BUS_NAME_WATCHER_FLAGS_NONE,
						  (GBusNameAppearedCallback) cpm_phone_service_appeared_cb,
						  (GBusNameVanishedCallback) cpm_phone_service_vanished_cb,
						  phone, NULL);
}

/**
 * cpm_phone_finalize:
 * @object: This class instance
 **/
static void
cpm_phone_finalize (GObject *object)
{
	CpmPhone *phone;
	g_return_if_fail (CPM_IS_PHONE (object));

	phone = CPM_PHONE (object);
	phone->priv = cpm_phone_get_instance_private (phone);

	if (phone->priv->proxy != NULL)
		g_object_unref (phone->priv->proxy);
	g_bus_unwatch_name (phone->priv->watch_id);

	G_OBJECT_CLASS (cpm_phone_parent_class)->finalize (object);
}

/**
 * cpm_phone_new:
 * Return value: new CpmPhone instance.
 **/
CpmPhone *
cpm_phone_new (void)
{
	if (cpm_phone_object != NULL) {
		g_object_ref (cpm_phone_object);
	} else {
		cpm_phone_object = g_object_new (CPM_TYPE_PHONE, NULL);
		g_object_add_weak_pointer (cpm_phone_object, &cpm_phone_object);
	}
	return CPM_PHONE (cpm_phone_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

static gboolean test_got_refresh = FALSE;

static void
egg_test_mainloop_wait (guint ms)
{
	GMainLoop *loop;
	loop = g_main_loop_new (NULL, FALSE);
	g_timeout_add (ms, (GSourceFunc) g_main_loop_quit, loop);
	g_main_loop_run (loop);
}

static void
phone_device_refresh_cb (CpmPhone *phone, guint idx, gpointer *data)
{
	g_debug ("idx refresh = %i", idx);
	if (idx == 0 && GPOINTER_TO_UINT (data) == 44)
		test_got_refresh = TRUE;
}

void
cpm_phone_test (gpointer data)
{
	CpmPhone *phone;
	guint value;
	gboolean ret;
	EggTest *test = (EggTest *) data;

	if (egg_test_start (test, "CpmPhone") == FALSE)
		return;

	/************************************************************/
	egg_test_title (test, "make sure we get a non null phone");
	phone = cpm_phone_new ();
	if (phone != NULL)
		egg_test_success (test, "got CpmPhone");
	else
		egg_test_failed (test, "could not get CpmPhone");

	/* connect signals */
	g_signal_connect (phone, "device-refresh",
			  G_CALLBACK (phone_device_refresh_cb), GUINT_TO_POINTER(44));

	/************************************************************/
	egg_test_title (test, "make sure we got a connection");
	if (phone->priv->proxy != NULL) {
		egg_test_success (test, "got connection");
	} else {
		/* skip this part of the test */
		egg_test_success (test, "could not get a connection!");
		goto out;
	}

	/************************************************************/
	egg_test_title (test, "coldplug the data");
	ret = cpm_phone_coldplug (phone);
	if (ret) {
		egg_test_success (test, "coldplug okay");
	} else {
		egg_test_failed (test, "could not coldplug");
	}

	egg_test_mainloop_wait (500);

	/************************************************************/
	egg_test_title (test, "got refresh");
	if (test_got_refresh) {
		egg_test_success (test, NULL);
	} else {
		egg_test_failed (test, "did not get refresh");
	}

	/************************************************************/
	egg_test_title (test, "check the connected phones");
	value = cpm_phone_get_num_batteries (phone);
	if (value == 1) {
		egg_test_success (test, "connected phone");
	} else {
		egg_test_failed (test, "not connected with %i (phone not connected?)", value);
	}

	/************************************************************/
	egg_test_title (test, "check the present value");
	ret = cpm_phone_get_present (phone, 0);
	if (ret) {
		egg_test_success (test, "we are here!");
	} else {
		egg_test_failed (test, "not here...");
	}

	/************************************************************/
	egg_test_title (test, "check the percentage");
	value = cpm_phone_get_percentage (phone, 0);
	if (value != 0) {
		egg_test_success (test, "percentage is %i", phone->priv->percentage);
	} else {
		egg_test_failed (test, "could not get value");
	}

	/************************************************************/
	egg_test_title (test, "check the ac value");
	ret = cpm_phone_get_on_ac (phone, 0);
	if (!ret) {
		egg_test_success (test, "not charging, correct");
	} else {
		egg_test_failed (test, "charging?");
	}
out:
	g_object_unref (phone);

	egg_test_end (test);
}

#endif

