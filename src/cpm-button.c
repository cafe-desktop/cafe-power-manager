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

#include <X11/X.h>
#include <cdk/cdkx.h>
#include <ctk/ctk.h>
#include <X11/XF86keysym.h>
#include <libupower-glib/upower.h>

#include "cpm-common.h"
#include "cpm-button.h"

#include "egg-debug.h"

static void     cpm_button_finalize   (GObject	      *object);

struct CpmButtonPrivate
{
	CdkScreen		*screen;
	CdkWindow		*window;
	GHashTable		*keysym_to_name_hash;
	gchar			*last_button;
	GTimer			*timer;
	gboolean		 lid_is_closed;
	UpClient		*client;
};

enum {
	BUTTON_PRESSED,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };
static gpointer cpm_button_object = NULL;

G_DEFINE_TYPE_WITH_PRIVATE (CpmButton, cpm_button, G_TYPE_OBJECT)

#define CPM_BUTTON_DUPLICATE_TIMEOUT	0.125f

/**
 * cpm_button_emit_type:
 **/
static gboolean
cpm_button_emit_type (CpmButton *button, const gchar *type)
{
	g_return_val_if_fail (CPM_IS_BUTTON (button), FALSE);

	/* did we just have this button before the timeout? */
	if (g_strcmp0 (type, button->priv->last_button) == 0 &&
	    g_timer_elapsed (button->priv->timer, NULL) < CPM_BUTTON_DUPLICATE_TIMEOUT) {
		egg_debug ("ignoring duplicate button %s", type);
		return FALSE;
	}

	egg_debug ("emitting button-pressed : %s", type);
	g_signal_emit (button, signals [BUTTON_PRESSED], 0, type);

	/* save type and last size */
	g_free (button->priv->last_button);
	button->priv->last_button = g_strdup (type);
	g_timer_reset (button->priv->timer);

	return TRUE;
}

/**
 * cpm_button_filter_x_events:
 **/
static CdkFilterReturn
cpm_button_filter_x_events (CdkXEvent *xevent, CdkEvent *event, gpointer data)
{
	CpmButton *button = (CpmButton *) data;
	XEvent *xev = (XEvent *) xevent;
	guint keycode;
	const gchar *key;
	gchar *keycode_str;

	if (xev->type != KeyPress)
		return CDK_FILTER_CONTINUE;

	keycode = xev->xkey.keycode;

	/* is the key string already in our DB? */
	keycode_str = g_strdup_printf ("0x%x", keycode);
	key = g_hash_table_lookup (button->priv->keysym_to_name_hash, (gpointer) keycode_str);
	g_free (keycode_str);

	/* found anything? */
	if (key == NULL) {
		egg_debug ("Key %i not found in hash", keycode);
		/* pass normal keypresses on, which might help with accessibility access */
		return CDK_FILTER_CONTINUE;
	}

	egg_debug ("Key %i mapped to key %s", keycode, key);
	cpm_button_emit_type (button, key);

	return CDK_FILTER_REMOVE;
}

/**
 * cpm_button_grab_keystring:
 * @button: This button class instance
 * @keystr: The key string, e.g. "<Control><Alt>F11"
 * @hashkey: A unique key made up from the modmask and keycode suitable for
 *           referencing in a hashtable.
 *           You must free this string, or specify NULL to ignore.
 *
 * Grab the key specified in the key string.
 *
 * Return value: TRUE if we parsed and grabbed okay
 **/
static gboolean
cpm_button_grab_keystring (CpmButton *button, guint64 keycode)
{
	guint modmask = AnyModifier;
	Display *display;
	CdkDisplay *cdkdisplay;
	gint ret;

	/* get the current X Display */
	display = CDK_DISPLAY_XDISPLAY (cdk_display_get_default());

	/* don't abort on error */
	cdkdisplay = cdk_display_get_default ();
	cdk_x11_display_error_trap_push (cdkdisplay);

	/* grab the key if possible */
	ret = XGrabKey (display, keycode, modmask,
			CDK_WINDOW_XID (button->priv->window), True,
			GrabModeAsync, GrabModeAsync);
	if (ret == BadAccess) {
		egg_warning ("Failed to grab modmask=%u, keycode=%li",
			     modmask, (long int) keycode);
		return FALSE;
	}

	/* grab the lock key if possible */
	ret = XGrabKey (display, keycode, LockMask | modmask,
			CDK_WINDOW_XID (button->priv->window), True,
			GrabModeAsync, GrabModeAsync);
	if (ret == BadAccess) {
		egg_warning ("Failed to grab modmask=%u, keycode=%li",
			     LockMask | modmask, (long int) keycode);
		return FALSE;
	}

	/* we are not processing the error */
	cdk_display_flush (cdkdisplay);
	cdk_x11_display_error_trap_pop_ignored (cdkdisplay);

	egg_debug ("Grabbed modmask=%x, keycode=%li", modmask, (long int) keycode);
	return TRUE;
}

/**
 * cpm_button_grab_keystring:
 * @button: This button class instance
 * @keystr: The key string, e.g. "<Control><Alt>F11"
 * @hashkey: A unique key made up from the modmask and keycode suitable for
 *           referencing in a hashtable.
 *           You must free this string, or specify NULL to ignore.
 *
 * Grab the key specified in the key string.
 *
 * Return value: TRUE if we parsed and grabbed okay
 **/
static gboolean
cpm_button_xevent_key (CpmButton *button, guint keysym, const gchar *key_name)
{
	gchar *key = NULL;
	gboolean ret;
	gchar *keycode_str;
	guint keycode;

	/* convert from keysym to keycode */
	keycode = XKeysymToKeycode (CDK_DISPLAY_XDISPLAY (cdk_display_get_default()), keysym);
	if (keycode == 0) {
		egg_warning ("could not map keysym %x to keycode", keysym);
		return FALSE;
	}

	/* is the key string already in our DB? */
	keycode_str = g_strdup_printf ("0x%x", keycode);
	key = g_hash_table_lookup (button->priv->keysym_to_name_hash, (gpointer) keycode_str);
	if (key != NULL) {
		egg_warning ("found in hash %i", keycode);
		g_free (keycode_str);
		return FALSE;
	}

	/* try to register X event */
	ret = cpm_button_grab_keystring (button, keycode);
	if (!ret) {
		egg_warning ("Failed to grab %i", keycode);
		g_free (keycode_str);
		return FALSE;
	}

	/* add to hash table */
	g_hash_table_insert (button->priv->keysym_to_name_hash, (gpointer) keycode_str, (gpointer) g_strdup (key_name));

	/* the key is freed in the hash function unref */
	return TRUE;
}

/**
 * cpm_button_class_init:
 * @button: This class instance
 **/
static void
cpm_button_class_init (CpmButtonClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = cpm_button_finalize;

	signals [BUTTON_PRESSED] =
		g_signal_new ("button-pressed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (CpmButtonClass, button_pressed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);
}

/**
 * cpm_button_is_lid_closed:
 **/
gboolean
cpm_button_is_lid_closed (CpmButton *button)
{
	GDBusProxy *proxy;
	GVariant *res, *inner;
	gboolean lid;
	GError *error = NULL;

	g_return_val_if_fail (CPM_IS_BUTTON (button), FALSE);

	if (LOGIND_RUNNING()) {
		proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
						       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
						       NULL,
						       "org.freedesktop.UPower",
						       "/org/freedesktop/UPower",
						       "org.freedesktop.DBus.Properties",
						       NULL,
						       &error );
		if (proxy == NULL) {
			egg_error("Error connecting to dbus - %s", error->message);
			g_error_free (error);
			return -1;
		}

		res = g_dbus_proxy_call_sync (proxy, "Get", 
					      g_variant_new( "(ss)", 
							     "org.freedesktop.UPower",
							     "LidIsClosed"),
					      G_DBUS_CALL_FLAGS_NONE,
					      -1,
					      NULL,
					      &error
					      );
		if (error == NULL && res != NULL) {
			g_variant_get(res, "(v)", &inner );
			lid = g_variant_get_boolean(inner);
			g_variant_unref (inner);
			g_variant_unref (res);
			return lid;
		} else if (error != NULL ) {
			egg_error ("Error in dbus - %s", error->message);
			g_error_free (error);
		}
		g_object_unref(proxy);

		return FALSE;
	}
	else {
		return up_client_get_lid_is_closed (button->priv->client);
	}
}


/**
 * cpm_button_reset_time:
 *
 * We have to refresh the event time on resume to handle duplicate buttons
 * properly when the time is significant when we suspend.
 **/
gboolean
cpm_button_reset_time (CpmButton *button)
{
	g_return_val_if_fail (CPM_IS_BUTTON (button), FALSE);
	g_timer_reset (button->priv->timer);
	return TRUE;
}

/**
 * cpm_button_client_changed_cb
 **/
static void
cpm_button_client_changed_cb (UpClient *client, GParamSpec *pspec, CpmButton *button)
{
	gboolean lid_is_closed;

	/* get new state */
	lid_is_closed = cpm_button_is_lid_closed(button);

	/* same state */
	if (button->priv->lid_is_closed == lid_is_closed)
		return;

	/* save state */
	button->priv->lid_is_closed = lid_is_closed;

	/* sent correct event */
	if (lid_is_closed)
		cpm_button_emit_type (button, CPM_BUTTON_LID_CLOSED);
	else
		cpm_button_emit_type (button, CPM_BUTTON_LID_OPEN);
}

/**
 * cpm_button_init:
 * @button: This class instance
 **/
static void
cpm_button_init (CpmButton *button)
{
	button->priv = cpm_button_get_instance_private (button);

	button->priv->screen = cdk_screen_get_default ();
	button->priv->window = cdk_screen_get_root_window (button->priv->screen);

	button->priv->keysym_to_name_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	button->priv->last_button = NULL;
	button->priv->timer = g_timer_new ();

	button->priv->client = up_client_new ();
	button->priv->lid_is_closed = up_client_get_lid_is_closed (button->priv->client);
	g_signal_connect (button->priv->client, "notify",
			  G_CALLBACK (cpm_button_client_changed_cb), button);
	/* register the brightness keys */
	cpm_button_xevent_key (button, XF86XK_PowerOff, CPM_BUTTON_POWER);

	/* The kernel messes up suspend/hibernate in some places. One of
	 * them is the key names. Unfortunately, they refuse to see the
	 * errors of their way in the name of 'compatibility'. Meh
	 */
	cpm_button_xevent_key (button, XF86XK_Suspend, CPM_BUTTON_HIBERNATE);
	cpm_button_xevent_key (button, XF86XK_Sleep, CPM_BUTTON_SUSPEND); /* should be configurable */
	cpm_button_xevent_key (button, XF86XK_Hibernate, CPM_BUTTON_HIBERNATE);
	cpm_button_xevent_key (button, XF86XK_MonBrightnessUp, CPM_BUTTON_BRIGHT_UP);
	cpm_button_xevent_key (button, XF86XK_MonBrightnessDown, CPM_BUTTON_BRIGHT_DOWN);
	cpm_button_xevent_key (button, XF86XK_ScreenSaver, CPM_BUTTON_LOCK);
	cpm_button_xevent_key (button, XF86XK_Battery, CPM_BUTTON_BATTERY);
	cpm_button_xevent_key (button, XF86XK_KbdBrightnessUp, CPM_BUTTON_KBD_BRIGHT_UP);
	cpm_button_xevent_key (button, XF86XK_KbdBrightnessDown, CPM_BUTTON_KBD_BRIGHT_DOWN);
	cpm_button_xevent_key (button, XF86XK_KbdLightOnOff, CPM_BUTTON_KBD_BRIGHT_TOGGLE);

	/* use global filter */
	cdk_window_add_filter (button->priv->window,
			       cpm_button_filter_x_events, (gpointer) button);
}

/**
 * cpm_button_finalize:
 * @object: This class instance
 **/
static void
cpm_button_finalize (GObject *object)
{
	CpmButton *button;
	g_return_if_fail (object != NULL);
	g_return_if_fail (CPM_IS_BUTTON (object));

	button = CPM_BUTTON (object);
	button->priv = cpm_button_get_instance_private (button);

	g_object_unref (button->priv->client);
	g_free (button->priv->last_button);
	g_timer_destroy (button->priv->timer);

	g_hash_table_unref (button->priv->keysym_to_name_hash);

	G_OBJECT_CLASS (cpm_button_parent_class)->finalize (object);
}

/**
 * cpm_button_new:
 * Return value: new class instance.
 **/
CpmButton *
cpm_button_new (void)
{
	if (cpm_button_object != NULL) {
		g_object_ref (cpm_button_object);
	} else {
		cpm_button_object = g_object_new (CPM_TYPE_BUTTON, NULL);
		g_object_add_weak_pointer (cpm_button_object, &cpm_button_object);
	}
	return CPM_BUTTON (cpm_button_object);
}
