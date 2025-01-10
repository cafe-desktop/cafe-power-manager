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

#include "config.h"

#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#include "cpm-screensaver.h"
#include "cpm-common.h"
#include "egg-debug.h"

static void     cpm_screensaver_finalize   (GObject		*object);

#define GS_LISTENER_SERVICE	"org.freedesktop.ScreenSaver"
#define GS_LISTENER_PATH	"/org/freedesktop/ScreenSaver"
#define GS_LISTENER_INTERFACE	"org.freedesktop.ScreenSaver"

struct CpmScreensaverPrivate
{
	DBusGProxy		*proxy;
};


static gpointer cpm_screensaver_object = NULL;

G_DEFINE_TYPE_WITH_PRIVATE (CpmScreensaver, cpm_screensaver, G_TYPE_OBJECT)

/**
 * cpm_screensaver_lock
 * @screensaver: This class instance
 * Return value: Success value
 **/
gboolean
cpm_screensaver_lock (CpmScreensaver *screensaver)
{
	guint sleepcount = 0;

	g_return_val_if_fail (CPM_IS_SCREENSAVER (screensaver), FALSE);

	if (screensaver->priv->proxy == NULL) {
		egg_warning ("not connected");
		return FALSE;
	}

	egg_debug ("doing cafe-screensaver lock");
	dbus_g_proxy_call_no_reply (screensaver->priv->proxy,
				    "Lock", G_TYPE_INVALID);

	/* When we send the Lock signal to g-ss it takes maybe a second
	   or so to fade the screen and lock. If we suspend mid fade then on
	   resume the X display is still present for a split second
	   (since fade is gamma) and as such it can leak information.
	   Instead we wait until g-ss reports running and thus blanked
	   solidly before we continue from the screensaver_lock action.
	   The interior of g-ss is async, so we cannot get the dbus method
	   to block until lock is complete. */
	while (! cpm_screensaver_check_running (screensaver)) {
		/* Sleep for 1/10s */
		g_usleep (1000 * 100);
		if (sleepcount++ > 50) {
			egg_debug ("timeout waiting for cafe-screensaver");
			break;
		}
	}

	return TRUE;
}

/**
 * cpm_screensaver_add_throttle:
 * @screensaver: This class instance
 * @reason:      The reason for throttling
 * Return value: Success value, or zero for failure
 **/
guint
cpm_screensaver_add_throttle (CpmScreensaver *screensaver,
			      const char     *reason)
{
	GError  *error = NULL;
	gboolean ret;
	guint32  cookie;

	g_return_val_if_fail (CPM_IS_SCREENSAVER (screensaver), 0);
	g_return_val_if_fail (reason != NULL, 0);

	if (screensaver->priv->proxy == NULL) {
		egg_warning ("not connected");
		return 0;
	}

	ret = dbus_g_proxy_call (screensaver->priv->proxy,
				 "Throttle", &error,
				 G_TYPE_STRING, "Power screensaver",
				 G_TYPE_STRING, reason,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &cookie,
				 G_TYPE_INVALID);
	if (error) {
		egg_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (!ret) {
		/* abort as the DBUS method failed */
		egg_warning ("Throttle failed!");
		return 0;
	}

	egg_debug ("adding throttle reason: '%s': id %u", reason, cookie);
	return cookie;
}

/**
 * cpm_screensaver_remove_throttle:
 **/
gboolean
cpm_screensaver_remove_throttle (CpmScreensaver *screensaver, guint cookie)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (CPM_IS_SCREENSAVER (screensaver), FALSE);

	if (screensaver->priv->proxy == NULL) {
		egg_warning ("not connected");
		return FALSE;
	}

	egg_debug ("removing throttle: id %u", cookie);
	ret = dbus_g_proxy_call (screensaver->priv->proxy,
				 "UnThrottle", &error,
				 G_TYPE_UINT, cookie,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (error) {
		egg_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (!ret) {
		/* abort as the DBUS method failed */
		egg_warning ("UnThrottle failed!");
		return FALSE;
	}

	return TRUE;
}

/**
 * cpm_screensaver_check_running:
 * @screensaver: This class instance
 * Return value: TRUE if cafe-screensaver is running
 **/
gboolean
cpm_screensaver_check_running (CpmScreensaver *screensaver)
{
	gboolean ret;
	gboolean temp = TRUE;
	GError *error = NULL;

	g_return_val_if_fail (CPM_IS_SCREENSAVER (screensaver), FALSE);

	if (screensaver->priv->proxy == NULL) {
		egg_warning ("not connected");
		return FALSE;
	}

	ret = dbus_g_proxy_call (screensaver->priv->proxy,
				 "GetActive", &error,
				 G_TYPE_INVALID,
				 G_TYPE_BOOLEAN, &temp,
				 G_TYPE_INVALID);
	if (error) {
		egg_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}

	return ret;
}

/**
 * cpm_screensaver_poke:
 * @screensaver: This class instance
 *
 * Pokes CAFE Screensaver simulating hardware events. This displays the unlock
 * dialogue when we resume, so the user doesn't have to move the mouse or press
 * any key before the window comes up.
 **/
gboolean
cpm_screensaver_poke (CpmScreensaver *screensaver)
{
	g_return_val_if_fail (CPM_IS_SCREENSAVER (screensaver), FALSE);

	if (screensaver->priv->proxy == NULL) {
		egg_warning ("not connected");
		return FALSE;
	}

	egg_debug ("poke");
	dbus_g_proxy_call_no_reply (screensaver->priv->proxy,
				    "SimulateUserActivity",
				    G_TYPE_INVALID);
	return TRUE;
}

/**
 * cpm_screensaver_class_init:
 * @klass: This class instance
 **/
static void
cpm_screensaver_class_init (CpmScreensaverClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = cpm_screensaver_finalize;
}

/**
 * cpm_screensaver_init:
 * @screensaver: This class instance
 **/
static void
cpm_screensaver_init (CpmScreensaver *screensaver)
{
	DBusGConnection *connection;

	screensaver->priv = cpm_screensaver_get_instance_private (screensaver);

	connection = dbus_g_bus_get (DBUS_BUS_SESSION, NULL);
	screensaver->priv->proxy = dbus_g_proxy_new_for_name (connection,
							      GS_LISTENER_SERVICE,
							      GS_LISTENER_PATH,
							      GS_LISTENER_INTERFACE);
}

/**
 * cpm_screensaver_finalize:
 * @object: This class instance
 **/
static void
cpm_screensaver_finalize (GObject *object)
{
	CpmScreensaver *screensaver;
	g_return_if_fail (object != NULL);
	g_return_if_fail (CPM_IS_SCREENSAVER (object));

	screensaver = CPM_SCREENSAVER (object);
	screensaver->priv = cpm_screensaver_get_instance_private (screensaver);

	g_object_unref (screensaver->priv->proxy);

	G_OBJECT_CLASS (cpm_screensaver_parent_class)->finalize (object);
}

/**
 * cpm_screensaver_new:
 * Return value: new CpmScreensaver instance.
 **/
CpmScreensaver *
cpm_screensaver_new (void)
{
	if (cpm_screensaver_object != NULL) {
		g_object_ref (cpm_screensaver_object);
	} else {
		cpm_screensaver_object = g_object_new (CPM_TYPE_SCREENSAVER, NULL);
		g_object_add_weak_pointer (cpm_screensaver_object, &cpm_screensaver_object);
	}
	return CPM_SCREENSAVER (cpm_screensaver_object);
}
/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
cpm_screensaver_test (gpointer data)
{
	CpmScreensaver *screensaver;
//	guint value;
	gboolean ret;
	EggTest *test = (EggTest *) data;

	if (egg_test_start (test, "CpmScreensaver") == FALSE)
		return;

	/************************************************************/
	egg_test_title (test, "make sure we get a non null screensaver");
	screensaver = cpm_screensaver_new ();
	egg_test_assert (test, (screensaver != NULL));

	/************************************************************/
	egg_test_title (test, "lock screensaver");
	ret = cpm_screensaver_lock (screensaver);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "poke screensaver");
	ret = cpm_screensaver_poke (screensaver);
	egg_test_assert (test, ret);

	g_object_unref (screensaver);

	egg_test_end (test);
}

#endif

