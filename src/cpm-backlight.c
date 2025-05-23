/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2005-2009 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2005 William Jon McCann <mccann@jhu.edu>
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
#include <math.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>
#include <ctk/ctk.h>
#include <libupower-glib/upower.h>

#include "cpm-button.h"
#include "cpm-backlight.h"
#include "cpm-brightness.h"
#include "cpm-control.h"
#include "cpm-common.h"
#include "egg-debug.h"
#include "csd_media-keys-window.h"
#include "cpm-dpms.h"
#include "cpm-idle.h"
#include "cpm-marshal.h"
#include "cpm-icon-names.h"
#include "egg-console-kit.h"

struct CpmBacklightPrivate
{
	UpClient		*client;
	CpmBrightness		*brightness;
	CpmButton		*button;
	GSettings		*settings;
	CtkWidget		*popup;
	CpmControl		*control;
	CpmDpms			*dpms;
	CpmIdle			*idle;
	EggConsoleKit		*console;
	gboolean		 can_dim;
	gboolean		 system_is_idle;
	GTimer			*idle_timer;
	guint			 idle_dim_timeout;
	guint			 master_percentage;
};

enum {
	BRIGHTNESS_CHANGED,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (CpmBacklight, cpm_backlight, G_TYPE_OBJECT)

/**
 * cpm_backlight_error_quark:
 * Return value: Our personal error quark.
 **/
GQuark
cpm_backlight_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("cpm_backlight_error");
	return quark;
}

/**
 * cpm_backlight_get_brightness:
 **/
gboolean
cpm_backlight_get_brightness (CpmBacklight *backlight, guint *brightness, GError **error)
{
	guint level;
	gboolean ret;
	g_return_val_if_fail (backlight != NULL, FALSE);
	g_return_val_if_fail (CPM_IS_BACKLIGHT (backlight), FALSE);
	g_return_val_if_fail (brightness != NULL, FALSE);

	/* check if we have the hw */
	if (backlight->priv->can_dim == FALSE) {
		g_set_error_literal (error, cpm_backlight_error_quark (),
				      CPM_BACKLIGHT_ERROR_HARDWARE_NOT_PRESENT,
				      "Dim capable hardware not present");
		return FALSE;
	}

	/* gets the current brightness */
	ret = cpm_brightness_get (backlight->priv->brightness, &level);
	if (ret) {
		*brightness = level;
	} else {
		g_set_error_literal (error, cpm_backlight_error_quark (),
				      CPM_BACKLIGHT_ERROR_DATA_NOT_AVAILABLE,
				      "Data not available");
	}
	return ret;
}

/**
 * cpm_backlight_set_brightness:
 **/
gboolean
cpm_backlight_set_brightness (CpmBacklight *backlight, guint percentage, GError **error)
{
	gboolean ret;
	gboolean hw_changed;

	g_return_val_if_fail (backlight != NULL, FALSE);
	g_return_val_if_fail (CPM_IS_BACKLIGHT (backlight), FALSE);

	/* check if we have the hw */
	if (backlight->priv->can_dim == FALSE) {
		g_set_error_literal (error, cpm_backlight_error_quark (),
				      CPM_BACKLIGHT_ERROR_HARDWARE_NOT_PRESENT,
				      "Dim capable hardware not present");
		return FALSE;
	}

	/* just set the master percentage for now, don't try to be clever */
	backlight->priv->master_percentage = percentage;

	/* sets the current policy brightness */
	ret = cpm_brightness_set (backlight->priv->brightness, percentage, &hw_changed);
	if (!ret) {
		g_set_error_literal (error, cpm_backlight_error_quark (),
				      CPM_BACKLIGHT_ERROR_GENERAL,
				      "Cannot set policy brightness");
	}
	/* we emit a signal for the brightness applet */
	if (ret && hw_changed) {
		egg_debug ("emitting brightness-changed : %i", percentage);
		g_signal_emit (backlight, signals [BRIGHTNESS_CHANGED], 0, percentage);
	}
	return ret;
}

/**
 * cpm_backlight_dialog_init:
 *
 * Initialises the popup, and makes sure that it matches the compositing of the screen.
 **/
static void
cpm_backlight_dialog_init (CpmBacklight *backlight)
{
	if (backlight->priv->popup != NULL
	    && !csd_osd_window_is_valid (CSD_OSD_WINDOW (backlight->priv->popup))) {
		ctk_widget_destroy (backlight->priv->popup);
		backlight->priv->popup = NULL;
	}

	if (backlight->priv->popup == NULL) {
		backlight->priv->popup= csd_media_keys_window_new ();
		csd_media_keys_window_set_action_custom (CSD_MEDIA_KEYS_WINDOW (backlight->priv->popup),
							 "cpm-brightness-lcd",
							 TRUE);
		ctk_window_set_position (CTK_WINDOW (backlight->priv->popup), CTK_WIN_POS_NONE);
	}
}

/**
 * cpm_backlight_dialog_show:
 *
 * Show the brightness popup, and place it nicely on the screen.
 **/
static void
cpm_backlight_dialog_show (CpmBacklight *backlight)
{
	int            orig_w;
	int            orig_h;
	int            screen_w;
	int            screen_h;
	int            x;
	int            y;
	int            pointer_x;
	int            pointer_y;
	CtkRequisition win_req;
	CdkScreen     *pointer_screen;
	CdkRectangle   geometry;
	CdkMonitor    *monitor;
	CdkDisplay    *display;
	CdkSeat       *seat;
	CdkDevice     *device;

	/*
	 * get the window size
	 * if the window hasn't been mapped, it doesn't necessarily
	 * know its true size, yet, so we need to jump through hoops
	 */
	ctk_window_get_default_size (CTK_WINDOW (backlight->priv->popup), &orig_w, &orig_h);
	ctk_widget_get_preferred_size (backlight->priv->popup, NULL, &win_req);

	if (win_req.width > orig_w) {
		orig_w = win_req.width;
	}
	if (win_req.height > orig_h) {
		orig_h = win_req.height;
	}

	pointer_screen = NULL;
	display = ctk_widget_get_display (backlight->priv->popup);
	seat = cdk_display_get_default_seat (display);
	device = cdk_seat_get_pointer (seat);
	cdk_device_get_position (device,
	                         &pointer_screen,
	                         &pointer_x,
	                         &pointer_y);

	monitor = cdk_display_get_monitor_at_point (cdk_screen_get_display (pointer_screen),
						    pointer_x,
						    pointer_y);

	cdk_monitor_get_geometry (monitor, &geometry);

	screen_w = geometry.width;
	screen_h = geometry.height;

	x = ((screen_w - orig_w) / 2) + geometry.x;
	y = geometry.y + (screen_h / 2) + (screen_h / 2 - orig_h) / 2;

	ctk_window_move (CTK_WINDOW (backlight->priv->popup), x, y);

	ctk_widget_show (backlight->priv->popup);

	cdk_display_sync (ctk_widget_get_display (backlight->priv->popup));
}

/**
 * cpm_common_sum_scale:
 *
 * Finds the average between value1 and value2 set on a scale factor
 **/
inline static gfloat
cpm_common_sum_scale (gfloat value1, gfloat value2, gfloat factor)
{
	gfloat diff;
	diff = value1 - value2;
	return value2 + (diff * factor);
}

/**
 * cpm_backlight_brightness_evaluate_and_set:
 **/
static gboolean
cpm_backlight_brightness_evaluate_and_set (CpmBacklight *backlight, gboolean interactive, gboolean use_initial)
{
	gfloat brightness;
	gfloat scale;
	gboolean ret;
	gboolean on_battery;
	gboolean do_laptop_lcd;
	gboolean enable_action;
	gboolean battery_reduce;
	gboolean hw_changed;
	guint value;
	guint old_value;

	if (backlight->priv->can_dim == FALSE) {
		egg_warning ("no dimming hardware");
		return FALSE;
	}

	do_laptop_lcd = g_settings_get_boolean (backlight->priv->settings, CPM_SETTINGS_BACKLIGHT_ENABLE);
	if (do_laptop_lcd == FALSE) {
		egg_warning ("policy is no dimming");
		return FALSE;
	}

	/* get the last set brightness */
	brightness = backlight->priv->master_percentage / 100.0f;
	egg_debug ("1. main brightness %f", brightness);

	/* get battery status */
	g_object_get (backlight->priv->client,
		      "on-battery", &on_battery,
		      NULL);

	/* reduce if on battery power if we should */
	if (use_initial) {
		egg_debug ("Setting initial brightness level");
		battery_reduce = g_settings_get_boolean (backlight->priv->settings, CPM_SETTINGS_BACKLIGHT_BATTERY_REDUCE);
		if (on_battery && battery_reduce) {
			value = g_settings_get_int (backlight->priv->settings, CPM_SETTINGS_BRIGHTNESS_DIM_BATT);
			if (value > 100) {
				egg_warning ("cannot use battery brightness value %i, correcting to 50", value);
				value = 50;
			}
			scale = (100 - value) / 100.0f;
			brightness *= scale;
		} else {
			scale = 1.0f;
		}
		egg_debug ("2. battery scale %f, brightness %f", scale, brightness);
	}

	/* reduce if system is momentarily idle */
	if (!on_battery)
		enable_action = g_settings_get_boolean (backlight->priv->settings, CPM_SETTINGS_IDLE_DIM_AC);
	else
		enable_action = g_settings_get_boolean (backlight->priv->settings, CPM_SETTINGS_IDLE_DIM_BATT);
	if (enable_action && backlight->priv->system_is_idle) {
		value = g_settings_get_int (backlight->priv->settings, CPM_SETTINGS_IDLE_BRIGHTNESS);
		if (value > 100) {
			egg_warning ("cannot use idle brightness value %i, correcting to 50", value);
			value = 50;
		}
		scale = value / 100.0f;
		brightness *= scale;
	} else {
		scale = 1.0f;
	}
	egg_debug ("3. idle scale %f, brightness %f", scale, brightness);

	/* convert to percentage */
	value = (guint) ((brightness * 100.0f) + 0.5);

	/* only do stuff if the brightness is different */
	cpm_brightness_get (backlight->priv->brightness, &old_value);
	if (old_value == value) {
		egg_debug ("values are the same, no action");
		return FALSE;
	}

	/* only show dialog if interactive */
	if (interactive) {
		cpm_backlight_dialog_init (backlight);
		csd_media_keys_window_set_volume_level (CSD_MEDIA_KEYS_WINDOW (backlight->priv->popup),
							round (brightness));
		cpm_backlight_dialog_show (backlight);
	}

	ret = cpm_brightness_set (backlight->priv->brightness, value, &hw_changed);
	/* we emit a signal for the brightness applet */
	if (ret && hw_changed) {
		egg_debug ("emitting brightness-changed : %i", value);
		g_signal_emit (backlight, signals [BRIGHTNESS_CHANGED], 0, value);
	}
	return TRUE;
}

/**
 * cpm_settings_key_changed_cb:
 *
 * We might have to do things when the keys change; do them here.
 **/
static void
cpm_settings_key_changed_cb (GSettings *settings, const gchar *key, CpmBacklight *backlight)
{
	gboolean on_battery;

	/* get battery status */
	g_object_get (backlight->priv->client,
		      "on-battery", &on_battery,
		      NULL);

	if (g_strcmp0 (key, CPM_SETTINGS_BRIGHTNESS_AC) == 0) {
		backlight->priv->master_percentage = g_settings_get_double (settings, key);
		cpm_backlight_brightness_evaluate_and_set (backlight, FALSE, TRUE);

	} else if (on_battery && g_strcmp0 (key, CPM_SETTINGS_BRIGHTNESS_DIM_BATT) == 0) {
		cpm_backlight_brightness_evaluate_and_set (backlight, FALSE, TRUE);

	} else if (g_strcmp0 (key, CPM_SETTINGS_IDLE_DIM_AC) == 0 ||
	           g_strcmp0 (key, CPM_SETTINGS_BACKLIGHT_ENABLE) == 0 ||
	           g_strcmp0 (key, CPM_SETTINGS_SLEEP_DISPLAY_BATT) == 0 ||
	           g_strcmp0 (key, CPM_SETTINGS_BACKLIGHT_BATTERY_REDUCE) == 0 ||
	           g_strcmp0 (key, CPM_SETTINGS_IDLE_BRIGHTNESS) == 0) {
		cpm_backlight_brightness_evaluate_and_set (backlight, FALSE, TRUE);

	} else if (g_strcmp0 (key, CPM_SETTINGS_IDLE_DIM_TIME) == 0) {
		backlight->priv->idle_dim_timeout = g_settings_get_int (settings, key);
		cpm_idle_set_timeout_dim (backlight->priv->idle, backlight->priv->idle_dim_timeout);
	} else {
		egg_debug ("unknown key %s", key);
	}
}

/**
 * cpm_backlight_client_changed_cb:
 * @client: The up_client class instance
 * @backlight: This class instance
 *
 * Does the actions when the ac power source is inserted/removed.
 **/
static void
cpm_backlight_client_changed_cb (UpClient     *client G_GNUC_UNUSED,
				 GParamSpec   *pspec G_GNUC_UNUSED,
				 CpmBacklight *backlight)
{
	cpm_backlight_brightness_evaluate_and_set (backlight, FALSE, TRUE);
}

/**
 * cpm_backlight_button_pressed_cb:
 * @power: The power class instance
 * @type: The button type, e.g. "power"
 * @state: The state, where TRUE is depressed or closed
 * @brightness: This class instance
 **/
static void
cpm_backlight_button_pressed_cb (CpmButton    *button G_GNUC_UNUSED,
				 const gchar  *type,
				 CpmBacklight *backlight)
{
	gboolean ret;
	GError *error = NULL;
	guint percentage;
	gboolean hw_changed;
	gboolean on_battery;
	egg_debug ("Button press event type=%s", type);

	if (g_strcmp0 (type, CPM_BUTTON_BRIGHT_UP) == 0) {
		/* go up one step */
		ret = cpm_brightness_up (backlight->priv->brightness, &hw_changed);

		/* show the new value */
		if (ret) {
			cpm_brightness_get (backlight->priv->brightness, &percentage);
			cpm_backlight_dialog_init (backlight);
			csd_media_keys_window_set_volume_level (CSD_MEDIA_KEYS_WINDOW (backlight->priv->popup),
								percentage);
			cpm_backlight_dialog_show (backlight);
			/* save the new percentage */
			backlight->priv->master_percentage = percentage;
			/* if using AC power supply, save the new brightness settings */
			g_object_get (backlight->priv->client, "on-battery", &on_battery, NULL);
			if (!on_battery) {
				egg_debug ("saving brightness for ac supply: %i", percentage);
				g_settings_set_double (backlight->priv->settings, CPM_SETTINGS_BRIGHTNESS_AC,
						       percentage*1.0);
			}
		}
		/* we emit a signal for the brightness applet */
		if (ret && hw_changed) {
			egg_debug ("emitting brightness-changed : %i", percentage);
			g_signal_emit (backlight, signals [BRIGHTNESS_CHANGED], 0, percentage);
		}
	} else if (g_strcmp0 (type, CPM_BUTTON_BRIGHT_DOWN) == 0) {
		/* go up down step */
		ret = cpm_brightness_down (backlight->priv->brightness, &hw_changed);

		/* show the new value */
		if (ret) {
			cpm_brightness_get (backlight->priv->brightness, &percentage);
			cpm_backlight_dialog_init (backlight);
			csd_media_keys_window_set_volume_level (CSD_MEDIA_KEYS_WINDOW (backlight->priv->popup),
								percentage);
			cpm_backlight_dialog_show (backlight);
			/* save the new percentage */
			backlight->priv->master_percentage = percentage;
			/* if using AC power supply, save the new brightness settings */
			g_object_get (backlight->priv->client, "on-battery", &on_battery, NULL);
			if (!on_battery) {
				egg_debug ("saving brightness for ac supply: %i", percentage);
				g_settings_set_double (backlight->priv->settings, CPM_SETTINGS_BRIGHTNESS_AC,
						       percentage*1.0);
			}
		}
		/* we emit a signal for the brightness applet */
		if (ret && hw_changed) {
			egg_debug ("emitting brightness-changed : %i", percentage);
			g_signal_emit (backlight, signals [BRIGHTNESS_CHANGED], 0, percentage);
		}
	} else if (g_strcmp0 (type, CPM_BUTTON_LID_OPEN) == 0) {
		/* make sure we undim when we lift the lid */
		cpm_backlight_brightness_evaluate_and_set (backlight, FALSE, TRUE);

		/* ensure backlight is on */
		ret = cpm_dpms_set_mode (backlight->priv->dpms, CPM_DPMS_MODE_ON, &error);
		if (!ret) {
			egg_warning ("failed to turn on DPMS: %s", error->message);
			g_error_free (error);
		}
	}
}

/**
 * cpm_backlight_notify_system_idle_changed:
 **/
static gboolean
cpm_backlight_notify_system_idle_changed (CpmBacklight *backlight, gboolean is_idle)
{
	gdouble elapsed;

	/* no point continuing */
	if (backlight->priv->system_is_idle == is_idle) {
		egg_debug ("state not changed");
		return FALSE;
	}

	/* get elapsed time and reset timer */
	elapsed = g_timer_elapsed (backlight->priv->idle_timer, NULL);
	g_timer_reset (backlight->priv->idle_timer);

	if (is_idle == FALSE) {
		egg_debug ("we have just been idle for %lfs", elapsed);

		/* The user immediatly undimmed the screen!
		 * We should double the timeout to avoid this happening again */
		if (elapsed < 10) {
			/* double the event time */
			backlight->priv->idle_dim_timeout *= 2.0;
			egg_debug ("increasing idle dim time to %is", backlight->priv->idle_dim_timeout);
			cpm_idle_set_timeout_dim (backlight->priv->idle, backlight->priv->idle_dim_timeout);
		}

		/* We reset the dimming after 2 minutes of idle,
		 * as the user will have changed tasks */
		if (elapsed > 2*60) {
			/* reset back to our default dimming */
			backlight->priv->idle_dim_timeout =
				g_settings_get_int (backlight->priv->settings,
					   CPM_SETTINGS_IDLE_DIM_TIME);
			egg_debug ("resetting idle dim time to %is", backlight->priv->idle_dim_timeout);
			cpm_idle_set_timeout_dim (backlight->priv->idle, backlight->priv->idle_dim_timeout);
		}
	} else {
		egg_debug ("we were active for %lfs", elapsed);
	}

	egg_debug ("changing powersave idle status to %i", is_idle);
	backlight->priv->system_is_idle = is_idle;
	return TRUE;
}

/**
 * idle_changed_cb:
 * @idle: The idle class instance
 * @mode: The idle mode, e.g. CPM_IDLE_MODE_BLANK
 * @manager: This class instance
 *
 * This callback is called when cafe-screensaver detects that the idle state
 * has changed. CPM_IDLE_MODE_BLANK is when the session has become inactive,
 * and CPM_IDLE_MODE_SLEEP is where the session has become inactive, AND the
 * session timeout has elapsed for the idle action.
 **/
static void
idle_changed_cb (CpmIdle      *idle G_GNUC_UNUSED,
		 CpmIdleMode   mode,
		 CpmBacklight *backlight)
{
	gboolean ret;
	GError *error = NULL;
	gboolean on_battery;
	CpmDpmsMode dpms_mode;

	/* don't dim or undim the screen when the lid is closed */
	if (cpm_button_is_lid_closed (backlight->priv->button))
		return;

	/* don't dim or undim the screen unless ConsoleKit/systemd say we are on the active console */
	if (!LOGIND_RUNNING() && !egg_console_kit_is_active (backlight->priv->console)) {
		egg_debug ("ignoring as not on active console");
		return;
	}

	if (mode == CPM_IDLE_MODE_NORMAL) {
		/* sync lcd brightness */
		cpm_backlight_notify_system_idle_changed (backlight, FALSE);
		cpm_backlight_brightness_evaluate_and_set (backlight, FALSE, TRUE);

		/* ensure backlight is on */
		ret = cpm_dpms_set_mode (backlight->priv->dpms, CPM_DPMS_MODE_ON, &error);
		if (!ret) {
			egg_warning ("failed to turn on DPMS: %s", error->message);
			g_error_free (error);
		}

	} else if (mode == CPM_IDLE_MODE_DIM) {

		/* sync lcd brightness */
		cpm_backlight_notify_system_idle_changed (backlight, TRUE);
		cpm_backlight_brightness_evaluate_and_set (backlight, FALSE, TRUE);

		/* ensure backlight is on */
		ret = cpm_dpms_set_mode (backlight->priv->dpms, CPM_DPMS_MODE_ON, &error);
		if (!ret) {
			egg_warning ("failed to turn on DPMS: %s", error->message);
			g_error_free (error);
		}

	} else if (mode == CPM_IDLE_MODE_BLANK) {

		/* sync lcd brightness */
		cpm_backlight_notify_system_idle_changed (backlight, TRUE);
		cpm_backlight_brightness_evaluate_and_set (backlight, FALSE, TRUE);

		/* get the DPMS state we're supposed to use on the power state */
		g_object_get (backlight->priv->client,
			      "on-battery", &on_battery,
			      NULL);
		if (!on_battery)
			dpms_mode = g_settings_get_enum (backlight->priv->settings, CPM_SETTINGS_DPMS_METHOD_AC);
		else
			dpms_mode = g_settings_get_enum (backlight->priv->settings, CPM_SETTINGS_DPMS_METHOD_BATT);

		/* check if method is valid */
		if (dpms_mode == CPM_DPMS_MODE_UNKNOWN || dpms_mode == CPM_DPMS_MODE_ON) {
			egg_warning ("BACKLIGHT method %i unknown. Using OFF.", dpms_mode);
			dpms_mode = CPM_DPMS_MODE_OFF;
		}

		/* turn backlight off */
		ret = cpm_dpms_set_mode (backlight->priv->dpms, dpms_mode, &error);
		if (!ret) {
			egg_warning ("failed to change DPMS: %s", error->message);
			g_error_free (error);
		}

	}
}

/**
 * brightness_changed_cb:
 * @brightness: The CpmBrightness class instance
 * @percentage: The new percentage brightness
 * @brightness: This class instance
 *
 * This callback is called when the brightness value changes.
 **/
static void
brightness_changed_cb (CpmBrightness *brightness G_GNUC_UNUSED,
		       guint          percentage,
		       CpmBacklight  *backlight)
{
	/* save the new percentage */
	backlight->priv->master_percentage = percentage;

	/* we emit a signal for the brightness applet */
	egg_debug ("emitting brightness-changed : %i", percentage);
	g_signal_emit (backlight, signals [BRIGHTNESS_CHANGED], 0, percentage);
}

/**
 * control_resume_cb:
 * @control: The control class instance
 * @power: This power class instance
 *
 * We have to update the caches on resume
 **/
static void
control_resume_cb (CpmControl      *control G_GNUC_UNUSED,
		   CpmControlAction action G_GNUC_UNUSED,
		   CpmBacklight    *backlight)
{
	gboolean ret;
	GError *error = NULL;

	/* ensure backlight is on */
	ret = cpm_dpms_set_mode (backlight->priv->dpms, CPM_DPMS_MODE_ON, &error);
	if (!ret) {
		egg_warning ("failed to turn on DPMS: %s", error->message);
		g_error_free (error);
	}
}

/**
 * cpm_backlight_finalize:
 **/
static void
cpm_backlight_finalize (GObject *object)
{
	CpmBacklight *backlight;
	g_return_if_fail (object != NULL);
	g_return_if_fail (CPM_IS_BACKLIGHT (object));
	backlight = CPM_BACKLIGHT (object);

	g_timer_destroy (backlight->priv->idle_timer);
	ctk_widget_destroy (backlight->priv->popup);

	g_object_unref (backlight->priv->dpms);
	g_object_unref (backlight->priv->control);
	g_object_unref (backlight->priv->settings);
	g_object_unref (backlight->priv->client);
	g_object_unref (backlight->priv->button);
	g_object_unref (backlight->priv->idle);
	g_object_unref (backlight->priv->brightness);
	g_object_unref (backlight->priv->console);

	g_return_if_fail (backlight->priv != NULL);
	G_OBJECT_CLASS (cpm_backlight_parent_class)->finalize (object);
}

/**
 * cpm_backlight_class_init:
 **/
static void
cpm_backlight_class_init (CpmBacklightClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize	   = cpm_backlight_finalize;

	signals [BRIGHTNESS_CHANGED] =
		g_signal_new ("brightness-changed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (CpmBacklightClass, brightness_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
}

/**
 * cpm_backlight_init:
 * @brightness: This brightness class instance
 *
 * initialises the brightness class. NOTE: We expect laptop_panel objects
 * to *NOT* be removed or added during the session.
 * We only control the first laptop_panel object if there are more than one.
 **/
static void
cpm_backlight_init (CpmBacklight *backlight)
{
	backlight->priv = cpm_backlight_get_instance_private (backlight);

	/* record our idle time */
	backlight->priv->idle_timer = g_timer_new ();

	/* watch for manual brightness changes (for the popup widget) */
	backlight->priv->brightness = cpm_brightness_new ();
	g_signal_connect (backlight->priv->brightness, "brightness-changed",
			  G_CALLBACK (brightness_changed_cb), backlight);

	/* we use up_client for the ac-adapter-changed signal */
	backlight->priv->client = up_client_new ();
	g_signal_connect (backlight->priv->client, "notify",
			  G_CALLBACK (cpm_backlight_client_changed_cb), backlight);

	/* gets caps */
	backlight->priv->can_dim = cpm_brightness_has_hw (backlight->priv->brightness);

	/* watch for dim value changes */
	backlight->priv->settings = g_settings_new (CPM_SETTINGS_SCHEMA);
	g_signal_connect (backlight->priv->settings, "changed", G_CALLBACK (cpm_settings_key_changed_cb), backlight);

	/* set the main brightness, this is designed to be updated if the user changes the
	 * brightness so we can undim to the 'correct' value */
	backlight->priv->master_percentage = g_settings_get_double (backlight->priv->settings, CPM_SETTINGS_BRIGHTNESS_AC);

	/* watch for brightness up and down buttons and also check lid state */
	backlight->priv->button = cpm_button_new ();
	g_signal_connect (backlight->priv->button, "button-pressed",
			  G_CALLBACK (cpm_backlight_button_pressed_cb), backlight);

	/* watch for idle mode changes */
	backlight->priv->idle = cpm_idle_new ();
	g_signal_connect (backlight->priv->idle, "idle-changed",
			  G_CALLBACK (idle_changed_cb), backlight);

	/* assumption */
	backlight->priv->system_is_idle = FALSE;
	backlight->priv->idle_dim_timeout = g_settings_get_int (backlight->priv->settings, CPM_SETTINGS_IDLE_DIM_TIME);
	cpm_idle_set_timeout_dim (backlight->priv->idle, backlight->priv->idle_dim_timeout);

	/* use a visual widget */
	backlight->priv->popup = csd_media_keys_window_new ();
	csd_media_keys_window_set_action_custom (CSD_MEDIA_KEYS_WINDOW (backlight->priv->popup),
						 "cpm-brightness-lcd",
						 TRUE);
        ctk_window_set_position (CTK_WINDOW (backlight->priv->popup), CTK_WIN_POS_NONE);

	/* DPMS mode poll class */
	backlight->priv->dpms = cpm_dpms_new ();

	/* we refresh DPMS on resume */
	backlight->priv->control = cpm_control_new ();
	g_signal_connect (backlight->priv->control, "resume",
			  G_CALLBACK (control_resume_cb), backlight);

	/* Don't do dimming on inactive console */
	backlight->priv->console = egg_console_kit_new ();

	/* sync at startup */
	cpm_backlight_brightness_evaluate_and_set (backlight, FALSE, TRUE);
}

/**
 * cpm_backlight_new:
 * Return value: A new brightness class instance.
 **/
CpmBacklight *
cpm_backlight_new (void)
{
	CpmBacklight *backlight = g_object_new (CPM_TYPE_BACKLIGHT, NULL);
	return backlight;
}

