/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * CAFE Power Manager Inhibit Applet
 * Copyright (C) 2006 Benjamin Canou <bookeldor@gmail.com>
 * Copyright (C) 2006-2009 Richard Hughes <richard@hughsie.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cafe-panel-applet.h>
#include <ctk/ctk.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#include "egg-debug.h"
#include "cpm-common.h"

#define CPM_TYPE_INHIBIT_APPLET		(cpm_inhibit_applet_get_type ())
#define CPM_INHIBIT_APPLET(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), CPM_TYPE_INHIBIT_APPLET, GpmInhibitApplet))
#define CPM_INHIBIT_APPLET_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), CPM_TYPE_INHIBIT_APPLET, GpmInhibitAppletClass))
#define CPM_IS_INHIBIT_APPLET(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), CPM_TYPE_INHIBIT_APPLET))
#define CPM_IS_INHIBIT_APPLET_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), CPM_TYPE_INHIBIT_APPLET))
#define CPM_INHIBIT_APPLET_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), CPM_TYPE_INHIBIT_APPLET, GpmInhibitAppletClass))

typedef struct{
	CafePanelApplet parent;
	/* applet state */
	guint cookie;
	/* the icon */
	CtkWidget *image;
	/* connection to g-p-m */
	DBusGProxy *proxy;
	DBusGConnection *connection;
	guint bus_watch_id;
	guint level;
	/* a cache for panel size */
	gint size;
} GpmInhibitApplet;

typedef struct{
	CafePanelAppletClass	parent_class;
} GpmInhibitAppletClass;

GType                cpm_inhibit_applet_get_type  (void);

#define GS_DBUS_SERVICE		"org.gnome.SessionManager"
#define GS_DBUS_PATH		"/org/gnome/SessionManager"
#define GS_DBUS_INTERFACE	"org.gnome.SessionManager"

G_DEFINE_TYPE (GpmInhibitApplet, cpm_inhibit_applet, PANEL_TYPE_APPLET)

static void	cpm_applet_update_icon		(GpmInhibitApplet *applet);
static void	cpm_applet_size_allocate_cb     (CtkWidget *widget, CdkRectangle *allocation);;
static void	cpm_applet_update_tooltip	(GpmInhibitApplet *applet);
static gboolean	cpm_applet_click_cb		(GpmInhibitApplet *applet, CdkEventButton *event);
static void	cpm_applet_dialog_about_cb	(CtkAction *action, gpointer data);
static gboolean	cpm_applet_cb		        (CafePanelApplet *_applet, const gchar *iid, gpointer data);
static void	cpm_applet_destroy_cb		(CtkWidget *widget);

#define CPM_INHIBIT_APPLET_ID		        "InhibitApplet"
#define CPM_INHIBIT_APPLET_FACTORY_ID	        "InhibitAppletFactory"
#define CPM_INHIBIT_APPLET_ICON_INHIBIT		"cpm-inhibit"
#define CPM_INHIBIT_APPLET_ICON_INVALID		"cpm-inhibit-invalid"
#define CPM_INHIBIT_APPLET_ICON_UNINHIBIT	"cpm-hibernate"
#define CPM_INHIBIT_APPLET_NAME			_("Power Manager Inhibit Applet")
#define CPM_INHIBIT_APPLET_DESC			_("Allows user to inhibit automatic power saving.")
#define CAFE_PANEL_APPLET_VERTICAL(p)					\
	 (((p) == CAFE_PANEL_APPLET_ORIENT_LEFT) || ((p) == CAFE_PANEL_APPLET_ORIENT_RIGHT))


/** cookie is returned as an unsigned integer */
static gboolean
cpm_applet_inhibit (GpmInhibitApplet *applet,
		    const gchar     *appname,
		    const gchar     *reason,
		    guint           *cookie)
{
	GError  *error = NULL;
	gboolean ret;

	g_return_val_if_fail (cookie != NULL, FALSE);

	if (applet->proxy == NULL) {
		egg_warning ("not connected\n");
		return FALSE;
	}

	ret = dbus_g_proxy_call (applet->proxy, "Inhibit", &error,
				 G_TYPE_STRING, appname,
				 G_TYPE_UINT, 0, /* xid */
				 G_TYPE_STRING, reason,
				 G_TYPE_UINT, 1+2+4+8, /* logoff, switch, suspend, and idle */
				 G_TYPE_INVALID,
				 G_TYPE_UINT, cookie,
				 G_TYPE_INVALID);
	if (error) {
		g_debug ("ERROR: %s", error->message);
		g_error_free (error);
		*cookie = 0;
	}
	if (!ret) {
		/* abort as the DBUS method failed */
		g_warning ("Inhibit failed!");
	}

	return ret;
}

static gboolean
cpm_applet_uninhibit (GpmInhibitApplet *applet,
		      guint            cookie)
{
	GError *error = NULL;
	gboolean ret;

	if (applet->proxy == NULL) {
		egg_warning ("not connected");
		return FALSE;
	}

	ret = dbus_g_proxy_call (applet->proxy, "Uninhibit", &error,
				 G_TYPE_UINT, cookie,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (error) {
		g_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (!ret) {
		/* abort as the DBUS method failed */
		g_warning ("Uninhibit failed!");
	}

	return ret;
}

/**
 * cpm_applet_update_icon:
 * @applet: Inhibit applet instance
 *
 * sets an icon from stock
 **/
static void
cpm_applet_update_icon (GpmInhibitApplet *applet)
{
	const gchar *icon;

	/* get icon */
	if (applet->proxy == NULL) {
		icon = CPM_INHIBIT_APPLET_ICON_INVALID;
	} else if (applet->cookie > 0) {
		icon = CPM_INHIBIT_APPLET_ICON_INHIBIT;
	} else {
		icon = CPM_INHIBIT_APPLET_ICON_UNINHIBIT;
	}
	ctk_image_set_from_icon_name (CTK_IMAGE(applet->image),
				      icon,
				      CTK_ICON_SIZE_BUTTON);
}

/**
 * cpm_applet_size_allocate_cb:
 * @applet: Inhibit applet instance
 *
 * resize icon when panel size changed
 **/
static void
cpm_applet_size_allocate_cb (CtkWidget    *widget,
                             CdkRectangle *allocation)
{
	GpmInhibitApplet *applet = CPM_INHIBIT_APPLET (widget);
	int               size = 0;

	switch (cafe_panel_applet_get_orient (CAFE_PANEL_APPLET (applet))) {
		case CAFE_PANEL_APPLET_ORIENT_LEFT:
		case CAFE_PANEL_APPLET_ORIENT_RIGHT:
			size = allocation->width;
			break;

		case CAFE_PANEL_APPLET_ORIENT_UP:
		case CAFE_PANEL_APPLET_ORIENT_DOWN:
			size = allocation->height;
			break;
	}

	/* Scale to the actual size of the applet, don't quantize to original icon size */
	/* CtkImage already contains a check to do nothing if it's the same */
	ctk_image_set_pixel_size (CTK_IMAGE(applet->image), size);
}


/**
 * cpm_applet_update_tooltip:
 * @applet: Inhibit applet instance
 *
 * sets tooltip's content (percentage or disabled)
 **/
static void
cpm_applet_update_tooltip (GpmInhibitApplet *applet)
{
	const gchar *buf;
	if (applet->proxy == NULL) {
		buf = _("Cannot connect to cafe-power-manager");
	} else {
		if (applet->cookie > 0) {
			buf = _("Automatic sleep inhibited");
		} else {
			buf = _("Automatic sleep enabled");
		}
	}
	ctk_widget_set_tooltip_text (CTK_WIDGET(applet), buf);
}

/**
 * cpm_applet_click_cb:
 * @applet: Inhibit applet instance
 *
 * pops and unpops
 **/
static gboolean
cpm_applet_click_cb (GpmInhibitApplet *applet, CdkEventButton *event)
{
	/* react only to left mouse button */
	if (event->button != 1) {
		return FALSE;
	}

	if (applet->cookie > 0) {
		g_debug ("uninhibiting %u", applet->cookie);
		cpm_applet_uninhibit (applet, applet->cookie);
		applet->cookie = 0;
	} else {
		g_debug ("inhibiting");
		cpm_applet_inhibit (applet,
					  CPM_INHIBIT_APPLET_NAME,
					  _("Manual inhibit"),
					  &(applet->cookie));
	}
	/* update icon */
	cpm_applet_update_icon (applet);
	cpm_applet_update_tooltip (applet);

	return TRUE;
}

/**
 * cpm_applet_dialog_about_cb:
 *
 * displays about dialog
 **/
static void
cpm_applet_dialog_about_cb (CtkAction *action, gpointer data)
{
	static const gchar *authors[] = {
		"Benjamin Canou <bookeldor@gmail.com>",
		"Richard Hughes <richard@hughsie.com>",
		NULL
	};

	const char *documenters [] = {
		NULL
	};

	const char *license[] = {
		 N_("Power Manager is free software; you can redistribute it and/or "
		   "modify it under the terms of the GNU General Public License "
		   "as published by the Free Software Foundation; either version 2 "
		   "of the License, or (at your option) any later version."),

		 N_("Power Manager is distributed in the hope that it will be useful, "
		   "but WITHOUT ANY WARRANTY; without even the implied warranty of "
		   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
		   "GNU General Public License for more details.") ,

		 N_("You should have received a copy of the GNU General Public License "
		   "along with this program; if not, write to the Free Software "
		   "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA "
		   "02110-1301, USA.")
	};

	char *license_trans;

	license_trans = g_strjoin("\n\n", _(license[0]), _(license[1]), _(license[2]), NULL);

	ctk_show_about_dialog (NULL,
	                       "program-name", CPM_INHIBIT_APPLET_NAME,
	                       "version", VERSION,
	                       "title", _("About Power Manager Inhibit Applet"),
	                       "comments", CPM_INHIBIT_APPLET_DESC,
	                       "copyright", _("Copyright \xC2\xA9 2006-2007 Richard Hughes\n"
	                                      "Copyright \xC2\xA9 2011-2020 CAFE developers"),
	                       "icon-name", CPM_INHIBIT_APPLET_ICON_INHIBIT,
	                       "logo-icon-name", CPM_INHIBIT_APPLET_ICON_INHIBIT,
	                       "license", license_trans,
	                       "authors", authors,
	                       "documenters", documenters,
	                       "translator-credits", _("translator-credits"),
	                       "wrap-license", TRUE,
	                       "website", "https://cafe-desktop.org",
	                       NULL);

	g_free (license_trans);
}

/**
 * cpm_applet_help_cb:
 *
 * open cpm help
 **/
static void
cpm_applet_help_cb (CtkAction *action, gpointer data)
{
	cpm_help_display ("applets-general#applets-inhibit");
}

/**
 * cpm_applet_destroy_cb:
 * @widget: Class instance to destroy
 **/
static void
cpm_applet_destroy_cb (CtkWidget *widget)
{
	GpmInhibitApplet *applet = CPM_INHIBIT_APPLET(widget);

	g_bus_unwatch_name (applet->bus_watch_id);
}

/**
 * cpm_inhibit_applet_class_init:
 * @klass: Class instance
 **/
static void
cpm_inhibit_applet_class_init (GpmInhibitAppletClass *class)
{
	/* nothing to do here */
}


/**
 * cpm_inhibit_applet_dbus_connect:
 **/
gboolean
cpm_inhibit_applet_dbus_connect (GpmInhibitApplet *applet)
{
	GError *error = NULL;

	if (applet->connection == NULL) {
		egg_debug ("get connection\n");
		g_clear_error (&error);
		applet->connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
		if (error != NULL) {
			egg_warning ("Could not connect to DBUS daemon: %s", error->message);
			g_error_free (error);
			applet->connection = NULL;
			return FALSE;
		}
	}
	if (applet->proxy == NULL) {
		egg_debug ("get proxy\n");
		g_clear_error (&error);
		applet->proxy = dbus_g_proxy_new_for_name_owner (applet->connection,
							 GS_DBUS_SERVICE,
							 GS_DBUS_PATH,
							 GS_DBUS_INTERFACE,
							 &error);
		if (error != NULL) {
			egg_warning ("Cannot connect, maybe the daemon is not running: %s\n", error->message);
			g_error_free (error);
			applet->proxy = NULL;
			return FALSE;
		}
	}
	return TRUE;
}

/**
 * cpm_inhibit_applet_dbus_disconnect:
 **/
gboolean
cpm_inhibit_applet_dbus_disconnect (GpmInhibitApplet *applet)
{
	if (applet->proxy != NULL) {
		egg_debug ("removing proxy\n");
		g_object_unref (applet->proxy);
		applet->proxy = NULL;
		/* we have no inhibit, these are not persistent across reboots */
		applet->cookie = 0;
	}
	return TRUE;
}

/**
 * cpm_inhibit_applet_name_appeared_cb:
 **/
static void
cpm_inhibit_applet_name_appeared_cb (GDBusConnection *connection,
				     const gchar *name,
				     const gchar *name_owner,
				     GpmInhibitApplet *applet)
{
	cpm_inhibit_applet_dbus_connect (applet);
	cpm_applet_update_tooltip (applet);
	cpm_applet_update_icon (applet);;
}

/**
 * cpm_inhibit_applet_name_vanished_cb:
 **/
void
cpm_inhibit_applet_name_vanished_cb (GDBusConnection *connection,
				     const gchar *name,
				     GpmInhibitApplet *applet)
{
	cpm_inhibit_applet_dbus_disconnect (applet);
	cpm_applet_update_tooltip (applet);
	cpm_applet_update_icon (applet);
}

/**
 * cpm_inhibit_applet_init:
 * @applet: Inhibit applet instance
 **/
static void
cpm_inhibit_applet_init (GpmInhibitApplet *applet)
{
	DBusGConnection *connection;

	/* initialize fields */
	applet->image = NULL;
	applet->cookie = 0;
	applet->connection = NULL;
	applet->proxy = NULL;

	/* Add application specific icons to search path */
	ctk_icon_theme_append_search_path (ctk_icon_theme_get_default (),
                                           CPM_ICONS_DATA);

	/* monitor the daemon */
	applet->bus_watch_id =
		g_bus_watch_name (G_BUS_TYPE_SESSION,
				  GS_DBUS_SERVICE,
				  G_BUS_NAME_WATCHER_FLAGS_NONE,
				  (GBusNameAppearedCallback) cpm_inhibit_applet_name_appeared_cb,
				  (GBusNameVanishedCallback) cpm_inhibit_applet_name_vanished_cb,
				  applet, NULL);

	/* prepare */
	cafe_panel_applet_set_flags (CAFE_PANEL_APPLET (applet), CAFE_PANEL_APPLET_EXPAND_MINOR);
	applet->image = ctk_image_new();
	ctk_container_add (CTK_CONTAINER (applet), applet->image);

	/* set appropriate size and load icon accordingly */
	ctk_widget_queue_draw (CTK_WIDGET (applet));

	/* show */
	ctk_widget_show_all (CTK_WIDGET(applet));

	/* connect */
	g_signal_connect (G_OBJECT(applet), "button-press-event",
			  G_CALLBACK(cpm_applet_click_cb), NULL);

	g_signal_connect (G_OBJECT(applet), "size-allocate",
			  G_CALLBACK(cpm_applet_size_allocate_cb), NULL);

	g_signal_connect (G_OBJECT(applet), "destroy",
			  G_CALLBACK(cpm_applet_destroy_cb), NULL);
}

/**
 * cpm_applet_cb:
 * @_applet: GpmInhibitApplet instance created by the applet factory
 * @iid: Applet id
 *
 * the function called by libcafe-panel-applet factory after creation
 **/
static gboolean
cpm_applet_cb (CafePanelApplet *_applet, const gchar *iid, gpointer data)
{
	GpmInhibitApplet *applet = CPM_INHIBIT_APPLET(_applet);
	CtkActionGroup *action_group;
	gchar *ui_path;

	static const CtkActionEntry menu_actions [] = {
		{ "About", "help-about", N_("_About"),
		  NULL, NULL,
		  G_CALLBACK (cpm_applet_dialog_about_cb) },
		{ "Help", "help-browser", N_("_Help"),
		  NULL, NULL,
		  G_CALLBACK (cpm_applet_help_cb) }
	};

	if (strcmp (iid, CPM_INHIBIT_APPLET_ID) != 0) {
		return FALSE;
	}

	action_group = ctk_action_group_new ("Inhibit Applet Actions");
	ctk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);
	ctk_action_group_add_actions (action_group,
				      menu_actions,
				      G_N_ELEMENTS (menu_actions),
				      applet);
	ui_path = g_build_filename (INHIBIT_MENU_UI_DIR, "inhibit-applet-menu.xml", NULL);
	cafe_panel_applet_setup_menu_from_file (CAFE_PANEL_APPLET (applet), ui_path, action_group);
	g_free (ui_path);
	g_object_unref (action_group);

	return TRUE;
}

/**
 * this generates a main with a applet factory
 **/
CAFE_PANEL_APPLET_OUT_PROCESS_FACTORY
 (/* the factory iid */
 CPM_INHIBIT_APPLET_FACTORY_ID,
 /* generates brighness applets instead of regular cafe applets  */
 CPM_TYPE_INHIBIT_APPLET,
 /* the applet name */
 "InhibitApplet",
 /* our callback (with no user data) */
 cpm_applet_cb, NULL)
