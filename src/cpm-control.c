/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006-2007 Richard Hughes <richard@hughsie.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <gio/gio.h>
#include <glib/gi18n.h>

#ifdef WITH_KEYRING
#include <gnome-keyring.h>
#endif /* WITH_KEYRING */

#include "egg-debug.h"
#include "egg-console-kit.h"

#include "cpm-screensaver.h"
#include "cpm-common.h"
#include "cpm-control.h"
#include "cpm-networkmanager.h"

struct CpmControlPrivate
{
	GSettings		*settings;
};

enum {
	RESUME,
	SLEEP,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };
static gpointer cpm_control_object = NULL;

G_DEFINE_TYPE_WITH_PRIVATE (CpmControl, cpm_control, G_TYPE_OBJECT)

/**
 * cpm_control_error_quark:
 * Return value: Our personal error quark.
 **/
GQuark
cpm_control_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("cpm_control_error");
	return quark;
}

/**
 * cpm_manager_systemd_shutdown:
 *
 * Shutdown the system using systemd-logind.
 *
 * Return value: fd, -1 on error
 **/
static gboolean
cpm_control_systemd_shutdown (void) {
	GError *error = NULL;
	GDBusProxy *proxy;
	GVariant *res = NULL;

	egg_debug ("Requesting systemd to shutdown");
	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
					       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
					       NULL,
					       "org.freedesktop.login1",
					       "/org/freedesktop/login1",
					       "org.freedesktop.login1.Manager",
					       NULL,
					       &error );
	//append all our arguments
	if (proxy == NULL) {
		egg_error("Error connecting to dbus - %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	res = g_dbus_proxy_call_sync (proxy, "PowerOff",
				      g_variant_new( "(b)", FALSE),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      NULL,
				      &error
				      );
	if (error != NULL) {
		egg_error ("Error in dbus - %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	g_variant_unref(res);
	return TRUE;
}

/**
 * cpm_control_shutdown:
 * @control: This class instance
 *
 * Shuts down the computer
 **/
gboolean
cpm_control_shutdown (CpmControl *control G_GNUC_UNUSED,
		      GError    **error)
{
	gboolean ret;
	EggConsoleKit *console;

	if (LOGIND_RUNNING()) {
		ret = cpm_control_systemd_shutdown ();
	} else {
		console = egg_console_kit_new ();
		ret = egg_console_kit_stop (console, error);
		g_object_unref (console);
	}
	return ret;
}

/**
 * cpm_control_get_lock_policy:
 * @control: This class instance
 * @policy: The policy string.
 *
 * This function finds out if we should lock the screen when we do an
 * action. It is required as we can either use the cafe-screensaver policy
 * or the custom policy. See the yelp file for more information.
 *
 * Return value: TRUE if we should lock.
 **/
gboolean
cpm_control_get_lock_policy (CpmControl *control, const gchar *policy)
{
	gboolean do_lock;
	gboolean use_ss_setting;
	gchar **schemas = NULL;
	gboolean schema_exists;
	gint i;

	/* Check if the cafe-screensaver schema exists before trying to read
	   the lock setting to prevent crashing. See GNOME bug #651225. */
	g_settings_schema_source_list_schemas (g_settings_schema_source_get_default (), TRUE, &schemas, NULL);
	schema_exists = FALSE;
	for (i = 0; schemas[i] != NULL; i++) {
		if (g_strcmp0 (schemas[i], GS_SETTINGS_SCHEMA) == 0) {
			schema_exists = TRUE;
			break;
		}
	}

	g_strfreev (schemas);

	/* This allows us to over-ride the custom lock settings set
	   with a system default set in cafe-screensaver.
	   See bug #331164 for all the juicy details. :-) */
	use_ss_setting = g_settings_get_boolean (control->priv->settings, CPM_SETTINGS_LOCK_USE_SCREENSAVER);
	if (use_ss_setting && schema_exists) {
		GSettings *settings_ss;
		settings_ss = g_settings_new (GS_SETTINGS_SCHEMA);
		do_lock = g_settings_get_boolean (settings_ss, GS_SETTINGS_PREF_LOCK_ENABLED);
		egg_debug ("Using ScreenSaver settings (%i)", do_lock);
		g_object_unref (settings_ss);
	} else {
		do_lock = g_settings_get_boolean (control->priv->settings, policy);
		egg_debug ("Using custom locking settings (%i)", do_lock);
	}
	return do_lock;
}

/**
 * cpm_control_suspend:
 **/
gboolean
cpm_control_suspend (CpmControl *control, GError **error)
{
	gboolean allowed = FALSE;
	gboolean ret = FALSE;
	gboolean do_lock;
	gboolean nm_sleep;
	EggConsoleKit *console;
	CpmScreensaver *screensaver;
	guint32 throttle_cookie = 0;
#ifdef WITH_KEYRING
	gboolean lock_gnome_keyring;
	GnomeKeyringResult keyres;
#endif /* WITH_KEYRING */

	GError *dbus_error = NULL;
	GDBusProxy *proxy;
	GVariant *res = NULL;

	screensaver = cpm_screensaver_new ();

	if (!LOGIND_RUNNING()) {
		console = egg_console_kit_new ();
		egg_console_kit_can_suspend (console, &allowed, NULL);
		g_object_unref (console);

		if (!allowed) {
			egg_debug ("cannot suspend as not allowed from policy");
			g_set_error_literal (error, CPM_CONTROL_ERROR, CPM_CONTROL_ERROR_GENERAL, "Cannot suspend");
			goto out;
		}
	}

#ifdef WITH_KEYRING
	/* we should perhaps lock keyrings when sleeping #375681 */
	lock_gnome_keyring = g_settings_get_boolean (control->priv->settings, CPM_SETTINGS_LOCK_KEYRING_SUSPEND);
	if (lock_gnome_keyring) {
		keyres = gnome_keyring_lock_all_sync ();
		if (keyres != GNOME_KEYRING_RESULT_OK)
			egg_warning ("could not lock keyring");
	}
#endif /* WITH_KEYRING */

	do_lock = cpm_control_get_lock_policy (control, CPM_SETTINGS_LOCK_ON_SUSPEND);
	if (do_lock) {
		throttle_cookie = cpm_screensaver_add_throttle (screensaver, "suspend");
		cpm_screensaver_lock (screensaver);
	}

	nm_sleep = g_settings_get_boolean (control->priv->settings, CPM_SETTINGS_NETWORKMANAGER_SLEEP);
	if (nm_sleep)
		cpm_networkmanager_sleep ();

	/* Do the suspend */
	egg_debug ("emitting sleep");
	g_signal_emit (control, signals [SLEEP], 0, CPM_CONTROL_ACTION_SUSPEND);

	if (LOGIND_RUNNING()) {
		/* sleep via logind */
		proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
						       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
						       NULL,
						       "org.freedesktop.login1",
						       "/org/freedesktop/login1",
						       "org.freedesktop.login1.Manager",
						       NULL,
						       &dbus_error );
		if (proxy == NULL) {
			egg_error("Error connecting to dbus - %s", dbus_error->message);
			g_error_free (dbus_error);
			ret = FALSE;
			goto out;
		}
		res = g_dbus_proxy_call_sync (proxy, "Suspend",
					      g_variant_new( "(b)",FALSE),
					      G_DBUS_CALL_FLAGS_NONE,
					      -1,
					      NULL,
					      &dbus_error
					      );
		if (dbus_error != NULL ) {
			egg_debug ("Error in dbus - %s", dbus_error->message);
			g_error_free (dbus_error);
			ret = TRUE;
		}
		else {
			g_variant_unref(res);
			ret = TRUE;
		}
		g_object_unref(proxy);
	}
	else {
		console = egg_console_kit_new ();
		ret = egg_console_kit_suspend (console, error);
		g_object_unref (console);
	}

	egg_debug ("emitting resume");
	g_signal_emit (control, signals [RESUME], 0, CPM_CONTROL_ACTION_SUSPEND);

	if (do_lock) {
		cpm_screensaver_poke (screensaver);
		if (throttle_cookie)
			cpm_screensaver_remove_throttle (screensaver, throttle_cookie);
	}

	nm_sleep = g_settings_get_boolean (control->priv->settings, CPM_SETTINGS_NETWORKMANAGER_SLEEP);
	if (nm_sleep)
		cpm_networkmanager_wake ();

out:
	g_object_unref (screensaver);
	return ret;
}

/**
 * cpm_control_hibernate:
 **/
gboolean
cpm_control_hibernate (CpmControl *control, GError **error)
{
	gboolean allowed = FALSE;
	gboolean ret = FALSE;
	gboolean do_lock;
	gboolean nm_sleep;
	EggConsoleKit *console;
	CpmScreensaver *screensaver;
	guint32 throttle_cookie = 0;
#ifdef WITH_KEYRING
	gboolean lock_gnome_keyring;
	GnomeKeyringResult keyres;
#endif /* WITH_KEYRING */

	GError *dbus_error = NULL;
	GDBusProxy *proxy;
	GVariant *res = NULL;

	screensaver = cpm_screensaver_new ();

	if (!LOGIND_RUNNING()) {
		console = egg_console_kit_new ();
		egg_console_kit_can_hibernate (console, &allowed, NULL);
		g_object_unref (console);

		if (!allowed) {
			egg_debug ("cannot hibernate as not allowed from policy");
			g_set_error_literal (error, CPM_CONTROL_ERROR, CPM_CONTROL_ERROR_GENERAL, "Cannot hibernate");
			goto out;
		}
	}

#ifdef WITH_KEYRING
	/* we should perhaps lock keyrings when sleeping #375681 */
	lock_gnome_keyring = g_settings_get_boolean (control->priv->settings, CPM_SETTINGS_LOCK_KEYRING_HIBERNATE);
	if (lock_gnome_keyring) {
		keyres = gnome_keyring_lock_all_sync ();
		if (keyres != GNOME_KEYRING_RESULT_OK) {
			egg_warning ("could not lock keyring");
		}
	}
#endif /* WITH_KEYRING */

	do_lock = cpm_control_get_lock_policy (control, CPM_SETTINGS_LOCK_ON_HIBERNATE);
	if (do_lock) {
		throttle_cookie = cpm_screensaver_add_throttle (screensaver, "hibernate");
		cpm_screensaver_lock (screensaver);
	}

	nm_sleep = g_settings_get_boolean (control->priv->settings, CPM_SETTINGS_NETWORKMANAGER_SLEEP);
	if (nm_sleep)
		cpm_networkmanager_sleep ();

	egg_debug ("emitting sleep");
	g_signal_emit (control, signals [SLEEP], 0, CPM_CONTROL_ACTION_HIBERNATE);

	if (LOGIND_RUNNING()) {
		/* sleep via logind */
		proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
						       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
						       NULL,
						       "org.freedesktop.login1",
						       "/org/freedesktop/login1",
						       "org.freedesktop.login1.Manager",
						       NULL,
						       &dbus_error );
		if (proxy == NULL) {
			egg_error("Error connecting to dbus - %s", dbus_error->message);
			g_error_free (dbus_error);
			ret = FALSE;
			goto out;
		}
		res = g_dbus_proxy_call_sync (proxy, "Hibernate",
					      g_variant_new( "(b)",FALSE),
					      G_DBUS_CALL_FLAGS_NONE,
					      -1,
					      NULL,
					      &dbus_error
					      );
		if (dbus_error != NULL ) {
			egg_debug ("Error in dbus - %s", dbus_error->message);
			g_error_free (dbus_error);
			ret = TRUE;
		}
		else {
			g_variant_unref(res);
			ret = TRUE;
		}
	}
	else {
		console = egg_console_kit_new ();
		ret = egg_console_kit_hibernate (console, error);
		g_object_unref (console);
	}

	egg_debug ("emitting resume");
	g_signal_emit (control, signals [RESUME], 0, CPM_CONTROL_ACTION_HIBERNATE);

	if (do_lock) {
		cpm_screensaver_poke (screensaver);
		if (throttle_cookie)
			cpm_screensaver_remove_throttle (screensaver, throttle_cookie);
	}

	nm_sleep = g_settings_get_boolean (control->priv->settings, CPM_SETTINGS_NETWORKMANAGER_SLEEP);
	if (nm_sleep)
		cpm_networkmanager_wake ();

out:
	g_object_unref (screensaver);
	return ret;
}

/**
 * cpm_control_finalize:
 **/
static void
cpm_control_finalize (GObject *object)
{
	CpmControl *control;

	g_return_if_fail (object != NULL);
	g_return_if_fail (CPM_IS_CONTROL (object));
	control = CPM_CONTROL (object);

	g_object_unref (control->priv->settings);

	g_return_if_fail (control->priv != NULL);
	G_OBJECT_CLASS (cpm_control_parent_class)->finalize (object);
}

/**
 * cpm_control_class_init:
 **/
static void
cpm_control_class_init (CpmControlClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = cpm_control_finalize;

	signals [RESUME] =
		g_signal_new ("resume",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (CpmControlClass, resume),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE, 1, G_TYPE_INT);
	signals [SLEEP] =
		g_signal_new ("sleep",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (CpmControlClass, sleep),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE, 1, G_TYPE_INT);
}

/**
 * cpm_control_init:
 * @control: This control class instance
 **/
static void
cpm_control_init (CpmControl *control)
{
	control->priv = cpm_control_get_instance_private (control);

	control->priv->settings = g_settings_new (CPM_SETTINGS_SCHEMA);
}

/**
 * cpm_control_new:
 * Return value: A new control class instance.
 **/
CpmControl *
cpm_control_new (void)
{
	if (cpm_control_object != NULL) {
		g_object_ref (cpm_control_object);
	} else {
		cpm_control_object = g_object_new (CPM_TYPE_CONTROL, NULL);
		g_object_add_weak_pointer (cpm_control_object, &cpm_control_object);
	}
	return CPM_CONTROL (cpm_control_object);
}

