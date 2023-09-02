/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2005 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2005-2009 Richard Hughes <richard@hughsie.com>
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
#  include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>
#include <ctk/ctk.h>
#include <libupower-glib/upower.h>

#include "egg-debug.h"

#include "cpm-upower.h"
#include "cpm-engine.h"
#include "cpm-common.h"
#include "cpm-icon-names.h"
#include "cpm-tray-icon.h"

static void     cpm_tray_icon_finalize   (GObject	   *object);

struct CpmTrayIconPrivate
{
	GSettings		*settings;
	CpmEngine		*engine;
	CtkStatusIcon		*status_icon;
	gboolean		 show_actions;
};

G_DEFINE_TYPE_WITH_PRIVATE (CpmTrayIcon, cpm_tray_icon, G_TYPE_OBJECT)

/**
 * cpm_tray_icon_enable_actions:
 **/
static void
cpm_tray_icon_enable_actions (CpmTrayIcon *icon, gboolean enabled)
{
	g_return_if_fail (CPM_IS_TRAY_ICON (icon));
	icon->priv->show_actions = enabled;
}

/**
 * cpm_tray_icon_show:
 * @enabled: If we should show the tray
 **/
static void
cpm_tray_icon_show (CpmTrayIcon *icon, gboolean enabled)
{
	g_return_if_fail (CPM_IS_TRAY_ICON (icon));
	ctk_status_icon_set_visible (icon->priv->status_icon, enabled);
}

/**
 * cpm_tray_icon_set_tooltip:
 * @tooltip: The tooltip text, e.g. "Batteries charged"
 **/
gboolean
cpm_tray_icon_set_tooltip (CpmTrayIcon *icon, const gchar *tooltip)
{
	g_return_val_if_fail (icon != NULL, FALSE);
	g_return_val_if_fail (CPM_IS_TRAY_ICON (icon), FALSE);
	g_return_val_if_fail (tooltip != NULL, FALSE);

	ctk_status_icon_set_tooltip_text (icon->priv->status_icon, tooltip);

	return TRUE;
}

/**
 * cpm_tray_icon_get_status_icon:
 **/
CtkStatusIcon *
cpm_tray_icon_get_status_icon (CpmTrayIcon *icon)
{
	g_return_val_if_fail (CPM_IS_TRAY_ICON (icon), NULL);
	return g_object_ref (icon->priv->status_icon);
}

/**
 * cpm_tray_icon_set_icon:
 * @icon_name: The icon name, e.g. CPM_ICON_APP_ICON, or NULL to remove.
 *
 * Loads a pixmap from disk, and sets as the tooltip icon.
 **/
gboolean
cpm_tray_icon_set_icon (CpmTrayIcon *icon, const gchar *icon_name)
{
	g_return_val_if_fail (icon != NULL, FALSE);
	g_return_val_if_fail (CPM_IS_TRAY_ICON (icon), FALSE);

	if (icon_name != NULL) {
		egg_debug ("Setting icon to %s", icon_name);
		ctk_status_icon_set_from_icon_name (icon->priv->status_icon,
		                                    icon_name);

		/* make sure that we are visible */
		cpm_tray_icon_show (icon, TRUE);
	} else {
		/* remove icon */
		egg_debug ("no icon will be displayed");

		/* make sure that we are hidden */
		cpm_tray_icon_show (icon, FALSE);
	}
	return TRUE;
}

/**
 * cpm_tray_icon_show_info_cb:
 **/
static void
cpm_tray_icon_show_info_cb (CtkMenuItem *item, gpointer data)
{
	gchar *path;
	const gchar *object_path;

	object_path = g_object_get_data (G_OBJECT (item), "object-path");
	path = g_strdup_printf ("%s/cafe-power-statistics --device %s", BINDIR, object_path);
	if (!g_spawn_command_line_async (path, NULL))
		egg_warning ("Couldn't execute command: %s", path);
	g_free (path);
}

/**
 * cpm_tray_icon_show_preferences_cb:
 * @action: A valid CtkAction
 **/
static void
cpm_tray_icon_show_preferences_cb (CtkMenuItem *item, gpointer data)
{
	const gchar *command = "cafe-power-preferences";

	if (g_spawn_command_line_async (command, NULL) == FALSE)
		egg_warning ("Couldn't execute command: %s", command);
}

#define ABOUT_GROUP "About"
#define EMAILIFY(string) (g_strdelimit ((string), "%", '@'))

/**
 * cpm_tray_icon_show_about_cb:
 * @action: A valid CtkAction
 **/
static void
cpm_tray_icon_show_about_cb (CtkMenuItem *item, gpointer data)
{
	GKeyFile *key_file;
	GBytes *bytes;
	const guint8 *data_resource;
	gsize data_resource_len;
	GError *error = NULL;
	char **authors;
	gsize n_authors = 0, i;

	bytes = g_resources_lookup_data ("/org/cafe/powermanager/manager/cafe-power-manager.about",
	                                 G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
	g_assert_no_error (error);

	data_resource = g_bytes_get_data (bytes, &data_resource_len);
	key_file = g_key_file_new ();
	g_key_file_load_from_data (key_file, (const char *) data_resource, data_resource_len, 0, &error);
	g_assert_no_error (error);

	authors = g_key_file_get_string_list (key_file, ABOUT_GROUP, "Authors", &n_authors, NULL);

	g_key_file_free (key_file);
	g_bytes_unref (bytes);

	for (i = 0; i < n_authors; ++i)
		authors[i] = EMAILIFY (authors[i]);

	ctk_show_about_dialog (NULL,
				"program-name", _("Power Manager"),
				"version", VERSION,
				"comments", _("Power management daemon"),
				"copyright", _("Copyright \xC2\xA9 2011-2020 MATE developers\n"
					       "Copyright \xC2\xA9 2023 Pablo Barciela"),
				"authors", authors,
				/* Translators should localize the following string
				* which will be displayed at the bottom of the about
				* box to give credit to the translator(s).
				*/
				"translator-credits", _("translator-credits"),
				"icon-name", "cafe-power-manager",
				"logo-icon-name", "cafe-power-manager",
				"website", "https://cafe-desktop.org",
				NULL);

	g_strfreev (authors);
}

/**
 * cpm_tray_icon_class_init:
 **/
static void
cpm_tray_icon_class_init (CpmTrayIconClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = cpm_tray_icon_finalize;
}

/**
 * cpm_tray_icon_add_device:
 **/
static guint
cpm_tray_icon_add_device (CpmTrayIcon *icon, CtkMenu *menu, const GPtrArray *array, UpDeviceKind kind)
{
	guint i;
	guint added = 0;
	gchar *icon_name;
	gchar *label, *vendor, *model;
	CtkWidget *item;
	CtkWidget *image;
	const gchar *object_path;
	UpDevice *device;
	UpDeviceKind kind_tmp;
	gdouble percentage;

	/* find type */
	for (i=0;i<array->len;i++) {
		device = g_ptr_array_index (array, i);

		/* get device properties */
		g_object_get (device,
			      "kind", &kind_tmp,
			      "percentage", &percentage,
			      "vendor", &vendor,
			      "model", &model,
			      NULL);

		if (kind != kind_tmp)
			continue;

		object_path = up_device_get_object_path (device);
		egg_debug ("adding device %s", object_path);
		added++;

		/* generate the label */
		if ((vendor != NULL && strlen(vendor) != 0) && (model != NULL && strlen(model) != 0)) {
			label = g_strdup_printf ("%s %s (%.1f%%)", vendor, model, percentage);
		}
		else {
			label = g_strdup_printf ("%s (%.1f%%)", cpm_device_kind_to_localised_string (kind, 1), percentage);
		}
		item = ctk_image_menu_item_new_with_label (label);

		/* generate the image */
		icon_name = cpm_upower_get_device_icon (device);
		image = ctk_image_new_from_icon_name (icon_name, CTK_ICON_SIZE_MENU);
		ctk_image_menu_item_set_image (CTK_IMAGE_MENU_ITEM (item), image);
		ctk_image_menu_item_set_always_show_image (CTK_IMAGE_MENU_ITEM (item), TRUE);

		/* set callback and add the menu */
		g_signal_connect (G_OBJECT (item), "activate", G_CALLBACK (cpm_tray_icon_show_info_cb), icon);
		g_object_set_data (G_OBJECT (item), "object-path", (gpointer) object_path);
		ctk_menu_shell_append (CTK_MENU_SHELL (menu), item);

		g_free (icon_name);
		g_free (label);
		g_free (vendor);
		g_free (model);
	}
	return added;
}

/**
 * cpm_tray_icon_add_primary_device:
 **/
static void
cpm_tray_icon_add_primary_device (CpmTrayIcon *icon, CtkMenu *menu, UpDevice *device)
{
	CtkWidget *item;
	gchar *time_str;
	gchar *string;
	gint64 time_to_empty = 0;

	/* get details */
	g_object_get (device,
		      "time-to-empty", &time_to_empty,
		      NULL);

	/* convert time to string */
	time_str = cpm_get_timestring (time_to_empty);

	/* TRANSLATORS: % is a timestring, e.g. "6 hours 10 minutes" */
	string = g_strdup_printf (_("%s remaining"), time_str);
	item = ctk_image_menu_item_new_with_label (string);
	ctk_menu_shell_append (CTK_MENU_SHELL (menu), item);
	g_free (time_str);
	g_free (string);
}

/**
 * cpm_tray_icon_create_menu:
 *
 * Create the popup menu.
 **/
static CtkMenu *
cpm_tray_icon_create_menu (CpmTrayIcon *icon)
{
	CtkMenu *menu = (CtkMenu*) ctk_menu_new ();
	CtkWidget *item;
	CtkWidget *image;
	guint dev_cnt = 0;
	GPtrArray *array;
	UpDevice *device = NULL;

	/* show the primary device time remaining */
	device = cpm_engine_get_primary_device (icon->priv->engine);
	if (device != NULL) {
		cpm_tray_icon_add_primary_device (icon, menu, device);
		item = ctk_separator_menu_item_new ();
		ctk_menu_shell_append (CTK_MENU_SHELL (menu), item);
	}

	/* add all device types to the drop down menu */
	array = cpm_engine_get_devices (icon->priv->engine);
	dev_cnt += cpm_tray_icon_add_device (icon, menu, array, UP_DEVICE_KIND_BATTERY);
	dev_cnt += cpm_tray_icon_add_device (icon, menu, array, UP_DEVICE_KIND_UPS);
	dev_cnt += cpm_tray_icon_add_device (icon, menu, array, UP_DEVICE_KIND_MOUSE);
	dev_cnt += cpm_tray_icon_add_device (icon, menu, array, UP_DEVICE_KIND_KEYBOARD);
	dev_cnt += cpm_tray_icon_add_device (icon, menu, array, UP_DEVICE_KIND_PDA);
	dev_cnt += cpm_tray_icon_add_device (icon, menu, array, UP_DEVICE_KIND_PHONE);
	dev_cnt += cpm_tray_icon_add_device (icon, menu, array, UP_DEVICE_KIND_MEDIA_PLAYER);
	dev_cnt += cpm_tray_icon_add_device (icon, menu, array, UP_DEVICE_KIND_TABLET);
	dev_cnt += cpm_tray_icon_add_device (icon, menu, array, UP_DEVICE_KIND_COMPUTER);
	g_ptr_array_unref (array);

	/* skip for things like live-cd's and GDM */
	if (!icon->priv->show_actions)
		goto skip_prefs;

	/* only do the separator if we have at least one device */
	if (dev_cnt != 0) {
		item = ctk_separator_menu_item_new ();
		ctk_menu_shell_append (CTK_MENU_SHELL (menu), item);
	}

	/* preferences */
	item = ctk_image_menu_item_new_with_mnemonic (_("_Preferences"));
	image = ctk_image_new_from_icon_name ("preferences-system", CTK_ICON_SIZE_MENU);
	ctk_image_menu_item_set_image (CTK_IMAGE_MENU_ITEM (item), image);
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (cpm_tray_icon_show_preferences_cb), icon);
	ctk_menu_shell_append (CTK_MENU_SHELL (menu), item);
	
	/*Set up custom panel menu theme support-ctk3 only */
	CtkWidget *toplevel = ctk_widget_get_toplevel (CTK_WIDGET (menu));
	/* Fix any failures of compiz/other wm's to communicate with ctk for transparency in menu theme */
	CdkScreen *screen = ctk_widget_get_screen(CTK_WIDGET(toplevel));
	CdkVisual *visual = cdk_screen_get_rgba_visual(screen);
	ctk_widget_set_visual(CTK_WIDGET(toplevel), visual);
	/* Set menu and its toplevel window to follow panel theme */
	CtkStyleContext *context;
	context = ctk_widget_get_style_context (CTK_WIDGET(toplevel));
	ctk_style_context_add_class(context,"gnome-panel-menu-bar");
	ctk_style_context_add_class(context,"cafe-panel-menu-bar");

	/* about */
	item = ctk_image_menu_item_new_from_stock ("ctk-about", NULL);
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (cpm_tray_icon_show_about_cb), icon);
	ctk_menu_shell_append (CTK_MENU_SHELL (menu), item);

skip_prefs:
	if (device != NULL)
		g_object_unref (device);
	return menu;
}

/**
 * cpm_tray_icon_popup_cleared_cd:
 * @widget: The popup Ctkwidget
 *
 * We have to re-enable the tooltip when the popup is removed
 **/
static void
cpm_tray_icon_popup_cleared_cd (CtkWidget *widget, CpmTrayIcon *icon)
{
	g_return_if_fail (CPM_IS_TRAY_ICON (icon));
	egg_debug ("clear tray");
	g_object_ref_sink (widget);
	g_object_unref (widget);
}

/**
 * cpm_tray_icon_popup_menu:
 *
 * Display the popup menu.
 **/
static void
cpm_tray_icon_popup_menu (CpmTrayIcon *icon, guint32 timestamp)
{
	CtkMenu *menu;

	menu = cpm_tray_icon_create_menu (icon);

	/* show the menu */
	ctk_widget_show_all (CTK_WIDGET (menu));
	ctk_menu_popup (CTK_MENU (menu), NULL, NULL,
			ctk_status_icon_position_menu, icon->priv->status_icon,
			1, timestamp);

	g_signal_connect (CTK_WIDGET (menu), "hide",
			  G_CALLBACK (cpm_tray_icon_popup_cleared_cd), icon);
}

/**
 * cpm_tray_icon_popup_menu_cb:
 *
 * Display the popup menu.
 **/
static void
cpm_tray_icon_popup_menu_cb (CtkStatusIcon *status_icon, guint button, guint32 timestamp, CpmTrayIcon *icon)
{
	egg_debug ("icon right clicked");
	cpm_tray_icon_popup_menu (icon, timestamp);
}


/**
 * cpm_tray_icon_activate_cb:
 * @button: Which buttons are pressed
 *
 * Callback when the icon is clicked
 **/
static void
cpm_tray_icon_activate_cb (CtkStatusIcon *status_icon, CpmTrayIcon *icon)
{
	egg_debug ("icon left clicked");
	cpm_tray_icon_popup_menu (icon, ctk_get_current_event_time());
}

/**
 * cpm_tray_icon_settings_changed_cb:
 *
 * We might have to do things when the settings change; do them here.
 **/
static void
cpm_tray_icon_settings_changed_cb (GSettings *settings, const gchar *key, CpmTrayIcon *icon)
{
	gboolean allowed_in_menu;

	if (g_strcmp0 (key, CPM_SETTINGS_SHOW_ACTIONS) == 0) {
		allowed_in_menu = g_settings_get_boolean (settings, key);
		cpm_tray_icon_enable_actions (icon, allowed_in_menu);
	}
}

/**
 * cpm_tray_icon_init:
 *
 * Initialise the tray object
 **/
static void
cpm_tray_icon_init (CpmTrayIcon *icon)
{
	gboolean allowed_in_menu;

	icon->priv = cpm_tray_icon_get_instance_private (icon);

	icon->priv->engine = cpm_engine_new ();

	icon->priv->settings = g_settings_new (CPM_SETTINGS_SCHEMA);
	g_signal_connect (icon->priv->settings, "changed",
			  G_CALLBACK (cpm_tray_icon_settings_changed_cb), icon);

	icon->priv->status_icon = ctk_status_icon_new ();
	cpm_tray_icon_show (icon, FALSE);
	g_signal_connect_object (G_OBJECT (icon->priv->status_icon),
				 "popup_menu",
				 G_CALLBACK (cpm_tray_icon_popup_menu_cb),
				 icon, 0);
	g_signal_connect_object (G_OBJECT (icon->priv->status_icon),
				 "activate",
				 G_CALLBACK (cpm_tray_icon_activate_cb),
				 icon, 0);

	allowed_in_menu = g_settings_get_boolean (icon->priv->settings, CPM_SETTINGS_SHOW_ACTIONS);
	cpm_tray_icon_enable_actions (icon, allowed_in_menu);
}

/**
 * cpm_tray_icon_finalize:
 * @object: This TrayIcon class instance
 **/
static void
cpm_tray_icon_finalize (GObject *object)
{
	CpmTrayIcon *tray_icon;

	g_return_if_fail (object != NULL);
	g_return_if_fail (CPM_IS_TRAY_ICON (object));

	tray_icon = CPM_TRAY_ICON (object);

	g_object_unref (tray_icon->priv->status_icon);
	g_object_unref (tray_icon->priv->engine);
	g_return_if_fail (tray_icon->priv != NULL);

	G_OBJECT_CLASS (cpm_tray_icon_parent_class)->finalize (object);
}

/**
 * cpm_tray_icon_new:
 * Return value: A new TrayIcon object.
 **/
CpmTrayIcon *
cpm_tray_icon_new (void)
{
	CpmTrayIcon *tray_icon;
	tray_icon = g_object_new (CPM_TYPE_TRAY_ICON, NULL);
	return CPM_TRAY_ICON (tray_icon);
}

