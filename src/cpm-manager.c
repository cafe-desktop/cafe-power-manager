/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2005 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2005-2008 Richard Hughes <richard@hughsie.com>
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
#include <gio/gunixfdlist.h>
#include <ctk/ctk.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <kanberra-ctk.h>
#include <libupower-glib/upower.h>
#include <libnotify/notify.h>

#include "egg-debug.h"
#include "egg-console-kit.h"

#include "cpm-button.h"
#include "cpm-control.h"
#include "cpm-common.h"
#include "cpm-dpms.h"
#include "cpm-idle.h"
#include "cpm-manager.h"
#include "cpm-screensaver.h"
#include "cpm-backlight.h"
#include "cpm-kbd-backlight.h"
#include "cpm-session.h"
#include "cpm-icon-names.h"
#include "cpm-tray-icon.h"
#include "cpm-engine.h"
#include "cpm-upower.h"

#include "org.cafe.PowerManager.Backlight.h"
#include "org.cafe.PowerManager.KbdBacklight.h"

static void     cpm_manager_finalize	(GObject	 *object);

#define GPM_MANAGER_NOTIFY_TIMEOUT_NEVER	0 /* ms */
#define GPM_MANAGER_NOTIFY_TIMEOUT_SHORT	10 * 1000 /* ms */
#define GPM_MANAGER_NOTIFY_TIMEOUT_LONG		30 * 1000 /* ms */

#define GPM_MANAGER_CRITICAL_ALERT_TIMEOUT      5 /* seconds */

struct GpmManagerPrivate
{
	GpmButton		*button;
	GSettings		*settings;
	GpmDpms			*dpms;
	GpmIdle			*idle;
	GpmControl		*control;
	GpmScreensaver		*screensaver;
	GpmTrayIcon		*tray_icon;
	GpmEngine		*engine;
	GpmBacklight		*backlight;
	GpmKbdBacklight		*kbd_backlight;
	EggConsoleKit		*console;
	guint32			 screensaver_ac_throttle_id;
	guint32			 screensaver_dpms_throttle_id;
	guint32			 screensaver_lid_throttle_id;
	guint32                  critical_alert_timeout_id;
	ka_proplist             *critical_alert_loop_props;
	UpClient		*client;
	gboolean		 on_battery;
	gboolean		 just_resumed;
	CtkStatusIcon		*status_icon;
	NotifyNotification	*notification_general;
	NotifyNotification	*notification_warning_low;
	NotifyNotification	*notification_discharging;
	NotifyNotification	*notification_fully_charged;
	gint32                   systemd_inhibit;
	GDBusProxy              *systemd_inhibit_proxy;
};

typedef enum {
	GPM_MANAGER_SOUND_POWER_PLUG,
	GPM_MANAGER_SOUND_POWER_UNPLUG,
	GPM_MANAGER_SOUND_LID_OPEN,
	GPM_MANAGER_SOUND_LID_CLOSE,
	GPM_MANAGER_SOUND_BATTERY_CAUTION,
	GPM_MANAGER_SOUND_BATTERY_LOW,
	GPM_MANAGER_SOUND_BATTERY_FULL,
	GPM_MANAGER_SOUND_SUSPEND_START,
	GPM_MANAGER_SOUND_SUSPEND_RESUME,
	GPM_MANAGER_SOUND_SUSPEND_ERROR,
	GPM_MANAGER_SOUND_LAST
} GpmManagerSound;

G_DEFINE_TYPE_WITH_PRIVATE (GpmManager, cpm_manager, G_TYPE_OBJECT)

/**
 * cpm_manager_error_quark:
 * Return value: Our personal error quark.
 **/
GQuark
cpm_manager_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("cpm_manager_error");
	return quark;
}

/**
 * cpm_manager_error_get_type:
 **/
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }
GType
cpm_manager_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] =
		{
			ENUM_ENTRY (GPM_MANAGER_ERROR_DENIED, "PermissionDenied"),
			ENUM_ENTRY (GPM_MANAGER_ERROR_NO_HW, "NoHardwareSupport"),
			{ 0, 0, 0 }
		};
		etype = g_enum_register_static ("GpmManagerError", values);
	}
	return etype;
}

/**
 * cpm_manager_play_loop_timeout_cb:
 **/
static gboolean
cpm_manager_play_loop_timeout_cb (GpmManager *manager)
{
	ka_context *context;
	context = ka_ctk_context_get_for_screen (cdk_screen_get_default ());
	ka_context_play_full (context, 0,
			      manager->priv->critical_alert_loop_props,
			      NULL,
			      NULL);
	return TRUE;
}

/**
 * cpm_manager_play_loop_stop:
 **/
static gboolean
cpm_manager_play_loop_stop (GpmManager *manager)
{
	if (manager->priv->critical_alert_timeout_id == 0) {
		egg_warning ("no sound loop present to stop");
		return FALSE;
	}

	g_source_remove (manager->priv->critical_alert_timeout_id);
	ka_proplist_destroy (manager->priv->critical_alert_loop_props);

	manager->priv->critical_alert_loop_props = NULL;
	manager->priv->critical_alert_timeout_id = 0;

	return TRUE;
}

/**
 * cpm_manager_play_loop_start:
 **/
static gboolean
cpm_manager_play_loop_start (GpmManager *manager, GpmManagerSound action, gboolean force, guint timeout)
{
	const gchar *id = NULL;
	const gchar *desc = NULL;
	gboolean ret;
	gint retval;
	ka_context *context;

	ret = g_settings_get_boolean (manager->priv->settings, GPM_SETTINGS_ENABLE_SOUND);
	if (!ret && !force) {
		egg_debug ("ignoring sound due to policy");
		return FALSE;
	}

	if (timeout == 0) {
		egg_warning ("received invalid timeout");
		return FALSE;
	}

	/* if a sound loop is already running, stop the existing loop */
	if (manager->priv->critical_alert_timeout_id != 0) {
		egg_warning ("was instructed to play a sound loop with one already playing");
		cpm_manager_play_loop_stop (manager);
	}

	if (action == GPM_MANAGER_SOUND_BATTERY_LOW) {
		id = "battery-low";
		/* TRANSLATORS: this is the sound description */
		desc = _("Battery is very low");
	}

	/* no match */
	if (id == NULL) {
		egg_warning ("no sound match for %i", action);
		return FALSE;
	}

	ka_proplist_create (&(manager->priv->critical_alert_loop_props));
	ka_proplist_sets (manager->priv->critical_alert_loop_props,
			  KA_PROP_EVENT_ID, id);
	ka_proplist_sets (manager->priv->critical_alert_loop_props,
			  KA_PROP_EVENT_DESCRIPTION, desc);

	manager->priv->critical_alert_timeout_id = g_timeout_add_seconds (timeout,
									  (GSourceFunc) cpm_manager_play_loop_timeout_cb,
									  manager);

	g_source_set_name_by_id (manager->priv->critical_alert_timeout_id, "[GpmManager] play-loop");

	/* play the sound, using sounds from the naming spec */
	context = ka_ctk_context_get_for_screen (cdk_screen_get_default ());
	retval = ka_context_play (context, 0,
				  KA_PROP_EVENT_ID, id,
				  KA_PROP_EVENT_DESCRIPTION, desc, NULL);
	if (retval < 0)
		egg_warning ("failed to play %s: %s", id, ka_strerror (retval));
	return TRUE;
}

/**
 * cpm_manager_play:
 **/
static gboolean
cpm_manager_play (GpmManager *manager, GpmManagerSound action, gboolean force)
{
	const gchar *id = NULL;
	const gchar *desc = NULL;
	gboolean ret;
	gint retval;
	ka_context *context;

	ret = g_settings_get_boolean (manager->priv->settings, GPM_SETTINGS_ENABLE_SOUND);
	if (!ret && !force) {
		egg_debug ("ignoring sound due to policy");
		return FALSE;
	}

	if (action == GPM_MANAGER_SOUND_POWER_PLUG) {
		id = "power-plug";
		/* TRANSLATORS: this is the sound description */
		desc = _("Power plugged in");
	} else if (action == GPM_MANAGER_SOUND_POWER_UNPLUG) {
		id = "power-unplug";
		/* TRANSLATORS: this is the sound description */
		desc = _("Power unplugged");
	} else if (action == GPM_MANAGER_SOUND_LID_OPEN) {
		id = "lid-open";
		/* TRANSLATORS: this is the sound description */
		desc = _("Lid has opened");
	} else if (action == GPM_MANAGER_SOUND_LID_CLOSE) {
		id = "lid-close";
		/* TRANSLATORS: this is the sound description */
		desc = _("Lid has closed");
	} else if (action == GPM_MANAGER_SOUND_BATTERY_CAUTION) {
		id = "battery-caution";
		/* TRANSLATORS: this is the sound description */
		desc = _("Battery is low");
	} else if (action == GPM_MANAGER_SOUND_BATTERY_LOW) {
		id = "battery-low";
		/* TRANSLATORS: this is the sound description */
		desc = _("Battery is very low");
	} else if (action == GPM_MANAGER_SOUND_BATTERY_FULL) {
		id = "battery-full";
		/* TRANSLATORS: this is the sound description */
		desc = _("Battery is full");
	} else if (action == GPM_MANAGER_SOUND_SUSPEND_START) {
		id = "suspend-start";
		/* TRANSLATORS: this is the sound description */
		desc = _("Suspend started");
	} else if (action == GPM_MANAGER_SOUND_SUSPEND_RESUME) {
		id = "suspend-resume";
		/* TRANSLATORS: this is the sound description */
		desc = _("Resumed");
	} else if (action == GPM_MANAGER_SOUND_SUSPEND_ERROR) {
		id = "suspend-error";
		/* TRANSLATORS: this is the sound description */
		desc = _("Suspend failed");
	}

	/* no match */
	if (id == NULL) {
		egg_warning ("no match");
		return FALSE;
	}

	/* play the sound, using sounds from the naming spec */
	context = ka_ctk_context_get_for_screen (cdk_screen_get_default ());
	retval = ka_context_play (context, 0,
				  KA_PROP_EVENT_ID, id,
				  KA_PROP_EVENT_DESCRIPTION, desc, NULL);
	if (retval < 0)
		egg_warning ("failed to play %s: %s", id, ka_strerror (retval));
	return TRUE;
}

/**
 * cpm_manager_is_inhibit_valid:
 * @manager: This class instance
 * @action: The action we want to do, e.g. "suspend"
 *
 * Checks to see if the specific action has been inhibited by a program.
 *
 * Return value: TRUE if we can perform the action.
 **/
static gboolean
cpm_manager_is_inhibit_valid (GpmManager *manager, gboolean user_action, const char *action)
{
	return TRUE;
}

/**
 * cpm_manager_sync_policy_sleep:
 * @manager: This class instance
 *
 * Changes the policy if required, setting brightness, display and computer
 * timeouts.
 * We have to make sure cafe-screensaver disables screensaving, and enables
 * monitor DPMS instead when on batteries to save power.
 **/
static void
cpm_manager_sync_policy_sleep (GpmManager *manager)
{
	guint sleep_display;
	guint sleep_computer;

	if (!manager->priv->on_battery) {
		sleep_computer = g_settings_get_int (manager->priv->settings, GPM_SETTINGS_SLEEP_COMPUTER_AC);
		sleep_display = g_settings_get_int (manager->priv->settings, GPM_SETTINGS_SLEEP_DISPLAY_AC);
	} else {
		sleep_computer = g_settings_get_int (manager->priv->settings, GPM_SETTINGS_SLEEP_COMPUTER_BATT);
		sleep_display = g_settings_get_int (manager->priv->settings, GPM_SETTINGS_SLEEP_DISPLAY_BATT);
	}

	/* set the new sleep (inactivity) value */
	cpm_idle_set_timeout_blank (manager->priv->idle, sleep_display);
	cpm_idle_set_timeout_sleep (manager->priv->idle, sleep_computer);
}

/**
 * cpm_manager_blank_screen:
 * @manager: This class instance
 *
 * Turn off the backlight of the LCD when we shut the lid, and lock
 * if required. This is required because some laptops do not turn off the
 * LCD backlight when the lid is closed.
 * See http://bugzilla.gnome.org/show_bug.cgi?id=321313
 *
 * Return value: Success.
 **/
static gboolean
cpm_manager_blank_screen (GpmManager *manager, GError **noerror)
{
	gboolean do_lock;
	gboolean ret = TRUE;
	GError *error = NULL;

	do_lock = cpm_control_get_lock_policy (manager->priv->control,
					       GPM_SETTINGS_LOCK_ON_BLANK_SCREEN);
	if (do_lock) {
		if (!cpm_screensaver_lock (manager->priv->screensaver))
			egg_debug ("Could not lock screen via cafe-screensaver");
	}
	cpm_dpms_set_mode (manager->priv->dpms, GPM_DPMS_MODE_OFF, &error);
	if (error) {
		egg_debug ("Unable to set DPMS mode: %s", error->message);
		g_error_free (error);
		ret = FALSE;
	}
	return ret;
}

/**
 * cpm_manager_unblank_screen:
 * @manager: This class instance
 *
 * Unblank the screen after we have opened the lid of the laptop
 *
 * Return value: Success.
 **/
static gboolean
cpm_manager_unblank_screen (GpmManager *manager, GError **noerror)
{
	gboolean do_lock;
	gboolean ret = TRUE;
	GError *error = NULL;

	cpm_dpms_set_mode (manager->priv->dpms, GPM_DPMS_MODE_ON, &error);
	if (error) {
		egg_debug ("Unable to set DPMS mode: %s", error->message);
		g_error_free (error);
		ret = FALSE;
	}

	do_lock = cpm_control_get_lock_policy (manager->priv->control, GPM_SETTINGS_LOCK_ON_BLANK_SCREEN);
	if (do_lock)
		cpm_screensaver_poke (manager->priv->screensaver);
	return ret;
}

/**
 * cpm_manager_notify_close:
 **/
static gboolean
cpm_manager_notify_close (GpmManager *manager, NotifyNotification *notification)
{
	gboolean ret = FALSE;
	GError *error = NULL;

	/* exists? */
	if (notification == NULL)
		goto out;

	/* try to close */
	ret = notify_notification_close (notification, &error);
	if (!ret) {
		egg_warning ("failed to close notification: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	return ret;
}

/**
 * cpm_manager_notification_closed_cb:
 **/
static void
cpm_manager_notification_closed_cb (NotifyNotification *notification, NotifyNotification **notification_class)
{
	egg_debug ("caught notification closed signal %p", notification);
	/* the object is already unreffed in _close_signal_handler */
	*notification_class = NULL;
}

/**
 * cpm_manager_notify:
 **/
static gboolean
cpm_manager_notify (GpmManager *manager, NotifyNotification **notification_class,
		    const gchar *title, const gchar *message,
		    guint timeout, const gchar *icon, NotifyUrgency urgency)
{
	gboolean ret;
	GError *error = NULL;
	NotifyNotification *notification;
	CtkWidget *dialog;

	/* close any existing notification of this class */
	cpm_manager_notify_close (manager, *notification_class);

	/* if the status icon is hidden, don't point at it */
	if (manager->priv->status_icon != NULL &&
	    ctk_status_icon_is_embedded (manager->priv->status_icon))
		notification = notify_notification_new (title, message, ctk_status_icon_get_icon_name(manager->priv->status_icon));
	else
		notification = notify_notification_new (title, message, icon);
	notify_notification_set_timeout (notification, timeout);
	notify_notification_set_urgency (notification, urgency);
	g_signal_connect (notification, "closed", G_CALLBACK (cpm_manager_notification_closed_cb), notification_class);

	egg_debug ("notification %p: %s : %s", notification, title, message);

	/* try to show */
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("failed to show notification: %s", error->message);
		g_error_free (error);

		/* show modal dialog as libcafenotify failed */
		dialog = ctk_message_dialog_new_with_markup (NULL, CTK_DIALOG_DESTROY_WITH_PARENT,
							     CTK_MESSAGE_INFO, CTK_BUTTONS_CLOSE,
							     "<span size='larger'><b>%s</b></span>", title);
		ctk_message_dialog_format_secondary_markup (CTK_MESSAGE_DIALOG (dialog), "%s", message);

		/* wait async for close */
		ctk_widget_show (dialog);
		g_signal_connect_swapped (dialog, "response", G_CALLBACK (ctk_widget_destroy), dialog);

		g_object_unref (notification);
		goto out;
	}

	/* save this local instance as the class instance */
	g_object_add_weak_pointer (G_OBJECT (notification), (gpointer) &notification);
	*notification_class = notification;
out:
	return ret;
}


/**
 * cpm_manager_sleep_failure_response_cb:
 **/
static void
cpm_manager_sleep_failure_response_cb (CtkDialog *dialog, gint response_id, GpmManager *manager)
{
	CtkWidget *dialog_error;
	GError *error = NULL;
	gboolean ret;
	gchar *uri = NULL;

	/* user clicked the help button */
	if (response_id == CTK_RESPONSE_HELP) {
		uri = g_settings_get_string (manager->priv->settings, GPM_SETTINGS_NOTIFY_SLEEP_FAILED_URI);
		ret = ctk_show_uri_on_window (CTK_WINDOW (dialog), uri, ctk_get_current_event_time (), &error);
		if (!ret) {
			dialog_error = ctk_message_dialog_new (NULL, CTK_DIALOG_MODAL, CTK_MESSAGE_INFO, CTK_BUTTONS_OK,
							       "Failed to show uri %s", error->message);
			ctk_dialog_run (CTK_DIALOG (dialog_error));
			g_error_free (error);
		}
	}

	ctk_widget_destroy (CTK_WIDGET (dialog));
	g_free (uri);
}

/**
 * cpm_manager_sleep_failure:
 **/
static void
cpm_manager_sleep_failure (GpmManager *manager, gboolean is_suspend, const gchar *detail)
{
	gboolean show_sleep_failed;
	GString *string = NULL;
	const gchar *title;
	gchar *uri = NULL;
	const gchar *icon;
	CtkWidget *dialog;

	/* only show this if specified in settings */
	show_sleep_failed = g_settings_get_boolean (manager->priv->settings, GPM_SETTINGS_NOTIFY_SLEEP_FAILED);

	egg_debug ("sleep failed");
	cpm_manager_play (manager, GPM_MANAGER_SOUND_SUSPEND_ERROR, TRUE);

	/* only emit if specified in settings */
	if (!show_sleep_failed)
		goto out;

	/* TRANSLATORS: window title: there was a problem putting the machine to sleep */
	string = g_string_new ("");
	if (is_suspend) {
		/* TRANSLATORS: message text */
		g_string_append (string, _("Computer failed to suspend."));
		/* TRANSLATORS: title text */
		title = _("Failed to suspend");
		icon = GPM_ICON_SUSPEND;
	} else {
		/* TRANSLATORS: message text */
		g_string_append (string, _("Computer failed to hibernate."));
		/* TRANSLATORS: title text */
		title = _("Failed to hibernate");
		icon = GPM_ICON_HIBERNATE;
	}

	/* TRANSLATORS: message text */
	g_string_append_printf (string, "\n\n%s %s", _("Failure was reported as:"), detail);

	/* show modal dialog */
	dialog = ctk_message_dialog_new_with_markup (NULL, CTK_DIALOG_DESTROY_WITH_PARENT,
						     CTK_MESSAGE_INFO, CTK_BUTTONS_CLOSE,
						     "<span size='larger'><b>%s</b></span>", title);
	ctk_message_dialog_format_secondary_markup (CTK_MESSAGE_DIALOG (dialog), "%s", string->str);
	ctk_window_set_icon_name (CTK_WINDOW(dialog), icon);

	/* show a button? */
	uri = g_settings_get_string (manager->priv->settings, GPM_SETTINGS_NOTIFY_SLEEP_FAILED_URI);
	if (uri != NULL && uri[0] != '\0') {
		/* TRANSLATORS: button text, visit the suspend help website */
		ctk_dialog_add_button (CTK_DIALOG (dialog), _("Visit help page"), CTK_RESPONSE_HELP);
	}

	/* wait async for close */
	ctk_widget_show (dialog);
	g_signal_connect (dialog, "response", G_CALLBACK (cpm_manager_sleep_failure_response_cb), manager);
out:
	g_free (uri);
	g_string_free (string, TRUE);
}

/**
 * cpm_manager_action_suspend:
 **/
static gboolean
cpm_manager_action_suspend (GpmManager *manager, const gchar *reason)
{
	gboolean ret;
	GError *error = NULL;

	/* check to see if we are inhibited */
	if (cpm_manager_is_inhibit_valid (manager, FALSE, "suspend") == FALSE)
		return FALSE;

	egg_debug ("suspending, reason: %s", reason);
	ret = cpm_control_suspend (manager->priv->control, &error);
	if (!ret) {
		cpm_manager_sleep_failure (manager, TRUE, error->message);
		g_error_free (error);
	}
	cpm_button_reset_time (manager->priv->button);
	return TRUE;
}

/**
 * cpm_manager_action_hibernate:
 **/
static gboolean
cpm_manager_action_hibernate (GpmManager *manager, const gchar *reason)
{
	gboolean ret;
	GError *error = NULL;

	/* check to see if we are inhibited */
	if (cpm_manager_is_inhibit_valid (manager, FALSE, "hibernate") == FALSE)
		return FALSE;

	egg_debug ("hibernating, reason: %s", reason);
	ret = cpm_control_hibernate (manager->priv->control, &error);
	if (!ret) {
		cpm_manager_sleep_failure (manager, TRUE, error->message);
		g_error_free (error);
	}
	cpm_button_reset_time (manager->priv->button);
	return TRUE;
}

/**
 * cpm_manager_perform_policy:
 * @manager: This class instance
 * @policy: The policy that we should do, e.g. "suspend"
 * @reason: The reason we are performing the policy action, e.g. "battery critical"
 *
 * Does one of the policy actions specified in the settings.
 **/
static gboolean
cpm_manager_perform_policy (GpmManager  *manager, const gchar *policy_key, const gchar *reason)
{
	GpmActionPolicy policy;

	/* are we inhibited? */
	if (cpm_manager_is_inhibit_valid (manager, FALSE, "policy action") == FALSE)
		return FALSE;

	policy = g_settings_get_enum (manager->priv->settings, policy_key);
	egg_debug ("action: %s set to %i (%s)", policy_key, policy, reason);

	if (policy == GPM_ACTION_POLICY_NOTHING) {
		egg_debug ("doing nothing, reason: %s", reason);
	} else if (policy == GPM_ACTION_POLICY_SUSPEND) {
		cpm_manager_action_suspend (manager, reason);

	} else if (policy == GPM_ACTION_POLICY_HIBERNATE) {
		cpm_manager_action_hibernate (manager, reason);

	} else if (policy == GPM_ACTION_POLICY_BLANK) {
		cpm_manager_blank_screen (manager, NULL);

	} else if (policy == GPM_ACTION_POLICY_SHUTDOWN) {
		egg_debug ("shutting down, reason: %s", reason);
		cpm_control_shutdown (manager->priv->control, NULL);

	} else if (policy == GPM_ACTION_POLICY_INTERACTIVE) {
		GpmSession *session;
		egg_debug ("logout, reason: %s", reason);
		session = cpm_session_new ();
		cpm_session_logout (session);
		g_object_unref (session);
	} else {
		egg_warning ("unknown action %i", policy);
	}

	return TRUE;
}

/**
 * cpm_manager_idle_do_sleep:
 * @manager: This class instance
 *
 * This callback is called when we want to sleep. Use the users
 * preference from the settings, but change it if we can't do the action.
 **/
static void
cpm_manager_idle_do_sleep (GpmManager *manager)
{
	gboolean ret;
	GError *error = NULL;
	GpmActionPolicy policy;

	if (!manager->priv->on_battery)
		policy = g_settings_get_enum (manager->priv->settings, GPM_SETTINGS_ACTION_SLEEP_TYPE_AC);
	else
		policy = g_settings_get_enum (manager->priv->settings, GPM_SETTINGS_ACTION_SLEEP_TYPE_BATT);

	if (policy == GPM_ACTION_POLICY_NOTHING) {
		egg_debug ("doing nothing as system idle action");

	} else if (policy == GPM_ACTION_POLICY_SUSPEND) {
		egg_debug ("suspending, reason: System idle");
		ret = cpm_control_suspend (manager->priv->control, &error);
		if (!ret) {
			egg_warning ("cannot suspend (error: %s), so trying hibernate", error->message);
			g_error_free (error);
			error = NULL;
			ret = cpm_control_hibernate (manager->priv->control, &error);
			if (!ret) {
				egg_warning ("cannot suspend or hibernate: %s", error->message);
				g_error_free (error);
			}
		}

	} else if (policy == GPM_ACTION_POLICY_HIBERNATE) {
		egg_debug ("hibernating, reason: System idle");
		ret = cpm_control_hibernate (manager->priv->control, &error);
		if (!ret) {
			egg_warning ("cannot hibernate (error: %s), so trying suspend", error->message);
			g_error_free (error);
			error = NULL;
			ret = cpm_control_suspend (manager->priv->control, &error);
			if (!ret) {
				egg_warning ("cannot suspend or hibernate: %s", error->message);
				g_error_free (error);
			}
		}
	}
}

/**
 * cpm_manager_idle_changed_cb:
 * @idle: The idle class instance
 * @mode: The idle mode, e.g. GPM_IDLE_MODE_BLANK
 * @manager: This class instance
 *
 * This callback is called when the idle class detects that the idle state
 * has changed. GPM_IDLE_MODE_BLANK is when the session has become inactive,
 * and GPM_IDLE_MODE_SLEEP is where the session has become inactive, AND the
 * session timeout has elapsed for the idle action.
 **/
static void
cpm_manager_idle_changed_cb (GpmIdle *idle, GpmIdleMode mode, GpmManager *manager)
{
	/* ConsoleKit/systemd say we are not on active console */
	if (!LOGIND_RUNNING() && !egg_console_kit_is_active (manager->priv->console)) {
		egg_debug ("ignoring as not on active console");
		return;
	}

	/* Ignore back-to-NORMAL events when the lid is closed, as the DPMS is
	 * already off, and we don't want to re-enable the screen when the user
	 * moves the mouse on systems that do not support hardware blanking. */
	if (cpm_button_is_lid_closed (manager->priv->button) &&
	    mode == GPM_IDLE_MODE_NORMAL) {
		egg_debug ("lid is closed, so we are ignoring ->NORMAL state changes");
		return;
	}

	if (mode == GPM_IDLE_MODE_SLEEP) {
		egg_debug ("Idle state changed: SLEEP");
		if (cpm_manager_is_inhibit_valid (manager, FALSE, "timeout action") == FALSE)
			return;
		cpm_manager_idle_do_sleep (manager);
	}
}

/**
 * cpm_manager_lid_button_pressed:
 * @manager: This class instance
 * @state: TRUE for closed
 *
 * Does actions when the lid is closed, depending on if we are on AC or
 * battery power.
 **/
static void
cpm_manager_lid_button_pressed (GpmManager *manager, gboolean pressed)
{
	if (pressed)
		cpm_manager_play (manager, GPM_MANAGER_SOUND_LID_CLOSE, FALSE);
	else
		cpm_manager_play (manager, GPM_MANAGER_SOUND_LID_OPEN, FALSE);

	if (pressed == FALSE) {
		/* we turn the lid dpms back on unconditionally */
		cpm_manager_unblank_screen (manager, NULL);
		return;
	}

	if (!manager->priv->on_battery) {
		egg_debug ("Performing AC policy");
		cpm_manager_perform_policy (manager, GPM_SETTINGS_BUTTON_LID_AC,
					    "The lid has been closed on ac power.");
		return;
	}

	egg_debug ("Performing battery policy");
	cpm_manager_perform_policy (manager, GPM_SETTINGS_BUTTON_LID_BATT,
				    "The lid has been closed on battery power.");
}

static void
cpm_manager_update_dpms_throttle (GpmManager *manager)
{
	GpmDpmsMode mode;
	cpm_dpms_get_mode (manager->priv->dpms, &mode, NULL);

	/* Throttle the manager when DPMS is active since we can't see it anyway */
	if (mode == GPM_DPMS_MODE_ON) {
		if (manager->priv->screensaver_dpms_throttle_id != 0) {
			cpm_screensaver_remove_throttle (manager->priv->screensaver, manager->priv->screensaver_dpms_throttle_id);
			manager->priv->screensaver_dpms_throttle_id = 0;
		}
	} else {
		/* if throttle already exists then remove */
		if (manager->priv->screensaver_dpms_throttle_id != 0) {
			cpm_screensaver_remove_throttle (manager->priv->screensaver, manager->priv->screensaver_dpms_throttle_id);
		}
		/* TRANSLATORS: this is the cafe-screensaver throttle */
		manager->priv->screensaver_dpms_throttle_id = cpm_screensaver_add_throttle (manager->priv->screensaver, _("Display DPMS activated"));
	}
}

static void
cpm_manager_update_ac_throttle (GpmManager *manager)
{
	/* Throttle the manager when we are not on AC power so we don't
	   waste the battery */
	if (!manager->priv->on_battery) {
		if (manager->priv->screensaver_ac_throttle_id != 0) {
			cpm_screensaver_remove_throttle (manager->priv->screensaver, manager->priv->screensaver_ac_throttle_id);
			manager->priv->screensaver_ac_throttle_id = 0;
		}
	} else {
		/* if throttle already exists then remove */
		if (manager->priv->screensaver_ac_throttle_id != 0)
			cpm_screensaver_remove_throttle (manager->priv->screensaver, manager->priv->screensaver_ac_throttle_id);
		/* TRANSLATORS: this is the cafe-screensaver throttle */
		manager->priv->screensaver_ac_throttle_id = cpm_screensaver_add_throttle (manager->priv->screensaver, _("On battery power"));
	}
}

static void
cpm_manager_update_lid_throttle (GpmManager *manager, gboolean lid_is_closed)
{
	/* Throttle the screensaver when the lid is close since we can't see it anyway
	   and it may overheat the laptop */
	if (lid_is_closed == FALSE) {
		if (manager->priv->screensaver_lid_throttle_id != 0) {
			cpm_screensaver_remove_throttle (manager->priv->screensaver, manager->priv->screensaver_lid_throttle_id);
			manager->priv->screensaver_lid_throttle_id = 0;
		}
	} else {
		/* if throttle already exists then remove */
		if (manager->priv->screensaver_lid_throttle_id != 0)
			cpm_screensaver_remove_throttle (manager->priv->screensaver, manager->priv->screensaver_lid_throttle_id);
		manager->priv->screensaver_lid_throttle_id = cpm_screensaver_add_throttle (manager->priv->screensaver, _("Laptop lid is closed"));
	}
}

/**
 * cpm_manager_button_pressed_cb:
 * @power: The power class instance
 * @type: The button type, e.g. "power"
 * @state: The state, where TRUE is depressed or closed
 * @manager: This class instance
 **/
static void
cpm_manager_button_pressed_cb (GpmButton *button, const gchar *type, GpmManager *manager)
{
	gchar *message;
	egg_debug ("Button press event type=%s", type);

	/* ConsoleKit/systemd say we are not on active console */
	if (!LOGIND_RUNNING() && !egg_console_kit_is_active (manager->priv->console)) {
		egg_debug ("ignoring as not on active console");
		return;
	}

	if (g_strcmp0 (type, GPM_BUTTON_POWER) == 0) {
		cpm_manager_perform_policy (manager, GPM_SETTINGS_BUTTON_POWER, "The power button has been pressed.");
	} else if (g_strcmp0 (type, GPM_BUTTON_SLEEP) == 0) {
		cpm_manager_perform_policy (manager, GPM_SETTINGS_BUTTON_SUSPEND, "The suspend button has been pressed.");
	} else if (g_strcmp0 (type, GPM_BUTTON_SUSPEND) == 0) {
		cpm_manager_perform_policy (manager, GPM_SETTINGS_BUTTON_SUSPEND, "The suspend button has been pressed.");
	} else if (g_strcmp0 (type, GPM_BUTTON_HIBERNATE) == 0) {
		cpm_manager_perform_policy (manager, GPM_SETTINGS_BUTTON_HIBERNATE, "The hibernate button has been pressed.");
	} else if (g_strcmp0 (type, GPM_BUTTON_LID_OPEN) == 0) {
		cpm_manager_lid_button_pressed (manager, FALSE);
	} else if (g_strcmp0 (type, GPM_BUTTON_LID_CLOSED) == 0) {
		cpm_manager_lid_button_pressed (manager, TRUE);
	} else if (g_strcmp0 (type, GPM_BUTTON_BATTERY) == 0) {
		message = cpm_engine_get_summary (manager->priv->engine);
		cpm_manager_notify (manager, &manager->priv->notification_general,
				    _("Power Information"),
				    message,
				    GPM_MANAGER_NOTIFY_TIMEOUT_LONG,
				    "dialog-information",
				    NOTIFY_URGENCY_NORMAL);
		g_free (message);
	}

	/* really belongs in cafe-screensaver */
	if (g_strcmp0 (type, GPM_BUTTON_LOCK) == 0)
		cpm_screensaver_lock (manager->priv->screensaver);

	/* disable or enable the fancy screensaver, as we don't want
	 * this starting when the lid is shut */
	if (g_strcmp0 (type, GPM_BUTTON_LID_CLOSED) == 0)
		cpm_manager_update_lid_throttle (manager, TRUE);
	else if (g_strcmp0 (type, GPM_BUTTON_LID_OPEN) == 0)
		cpm_manager_update_lid_throttle (manager, FALSE);
}

/**
 * cpm_manager_client_changed_cb:
 **/
static void
cpm_manager_client_changed_cb (UpClient *client, GParamSpec *pspec, GpmManager *manager)
{
	gboolean event_when_closed;
	gint timeout;
	gboolean on_battery;
	gboolean lid_is_closed;

	/* get the client state */
	g_object_get (client,
		      "on-battery", &on_battery,
		      "lid-is-closed", &lid_is_closed,
		      NULL);
	if (on_battery == manager->priv->on_battery) {
		egg_debug ("same state as before, ignoring");
		return;
	}

	/* close any discharging notifications */
	if (!on_battery) {
		egg_debug ("clearing notify due ac being present");
		cpm_manager_notify_close (manager, manager->priv->notification_warning_low);
		cpm_manager_notify_close (manager, manager->priv->notification_discharging);
	}

	/* if we are playing a critical charge sound loop, stop it */
	if (!on_battery && manager->priv->critical_alert_timeout_id) {
		egg_debug ("stopping alert loop due to ac being present");
		cpm_manager_play_loop_stop (manager);
	}

	/* save in local cache */
	manager->priv->on_battery = on_battery;

	/* ConsoleKit/systemd say we are not on active console */
	if (!LOGIND_RUNNING() && !egg_console_kit_is_active (manager->priv->console)) {
		egg_debug ("ignoring as not on active console");
		return;
	}

	egg_debug ("on_battery: %d", on_battery);

	cpm_manager_sync_policy_sleep (manager);

	cpm_manager_update_ac_throttle (manager);

	/* simulate user input, but only when the lid is open */
	if (!lid_is_closed)
		cpm_screensaver_poke (manager->priv->screensaver);

	if (!on_battery)
		cpm_manager_play (manager, GPM_MANAGER_SOUND_POWER_PLUG, FALSE);
	else
		cpm_manager_play (manager, GPM_MANAGER_SOUND_POWER_UNPLUG, FALSE);

	/* We do the lid close on battery action if the ac adapter is removed
	   when the laptop is closed and on battery. Fixes #331655 */
	event_when_closed = g_settings_get_boolean (manager->priv->settings, GPM_SETTINGS_SLEEP_WHEN_CLOSED);

	/* We keep track of the lid state so we can do the
	   lid close on battery action if the ac adapter is removed when the laptop
	   is closed. Fixes #331655 */
	if (event_when_closed && on_battery && lid_is_closed) {
		cpm_manager_perform_policy (manager, GPM_SETTINGS_BUTTON_LID_BATT,
					    "The lid has been closed, and the ac adapter "
					    "removed (and GSettings is okay).");
	}
}

/**
 * manager_critical_action_do:
 * @manager: This class instance
 *
 * This is the stub function when we have waited a few seconds for the user to
 * see the message, explaining what we are about to do.
 *
 * Return value: FALSE, as we don't want to repeat this action on resume.
 **/
static gboolean
manager_critical_action_do (GpmManager *manager)
{
	/* stop playing the alert as it's too late to do anything now */
	if (manager->priv->critical_alert_timeout_id)
		cpm_manager_play_loop_stop (manager);

	cpm_manager_perform_policy (manager, GPM_SETTINGS_ACTION_CRITICAL_BATT, "Battery is critically low.");
	return FALSE;
}

/**
 * cpm_manager_class_init:
 * @klass: The GpmManagerClass
 **/
static void
cpm_manager_class_init (GpmManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = cpm_manager_finalize;
}

/**
 * cpm_manager_settings_changed_cb:
 *
 * We might have to do things when the keys change; do them here.
 **/
static void
cpm_manager_settings_changed_cb (GSettings *settings, const gchar *key, GpmManager *manager)
{
	if (g_strcmp0 (key, GPM_SETTINGS_SLEEP_COMPUTER_BATT) == 0 ||
	    g_strcmp0 (key, GPM_SETTINGS_SLEEP_COMPUTER_AC) == 0 ||
	    g_strcmp0 (key, GPM_SETTINGS_SLEEP_DISPLAY_BATT) == 0 ||
	    g_strcmp0 (key, GPM_SETTINGS_SLEEP_DISPLAY_AC) == 0)
		cpm_manager_sync_policy_sleep (manager);
}

/**
 * cpm_manager_engine_icon_changed_cb:
 */
static void
cpm_manager_engine_icon_changed_cb (GpmEngine  *engine, gchar *icon, GpmManager *manager)
{
	cpm_tray_icon_set_icon (manager->priv->tray_icon, icon);
}

/**
 * cpm_manager_engine_summary_changed_cb:
 */
static void
cpm_manager_engine_summary_changed_cb (GpmEngine *engine, gchar *summary, GpmManager *manager)
{
	cpm_tray_icon_set_tooltip (manager->priv->tray_icon, summary);
}

/**
 * cpm_manager_engine_low_capacity_cb:
 */
static void
cpm_manager_engine_low_capacity_cb (GpmEngine *engine, UpDevice *device, GpmManager *manager)
{
	gchar *message = NULL;
	const gchar *title;
	gdouble capacity;

	/* don't show when running under GDM */
	if (g_getenv ("RUNNING_UNDER_GDM") != NULL) {
		egg_debug ("running under gdm, so no notification");
		goto out;
	}

	/* get device properties */
	g_object_get (device,
		      "capacity", &capacity,
		      NULL);

	/* We should notify the user if the battery has a low capacity,
	 * where capacity is the ratio of the last_full capacity with that of
	 * the design capacity. (#326740) */

	/* TRANSLATORS: battery is old or broken */
	title = _("Battery may be broken");

	/* TRANSLATORS: notify the user that that battery is broken as the capacity is very low */
	message = g_strdup_printf (_("Battery has a very low capacity (%1.1f%%), "
				     "which means that it may be old or broken."), capacity);
	cpm_manager_notify (manager, &manager->priv->notification_general, title, message, GPM_MANAGER_NOTIFY_TIMEOUT_SHORT,
			    "dialog-information", NOTIFY_URGENCY_LOW);
out:
	g_free (message);
}

/**
 * cpm_manager_engine_fully_charged_cb:
 */
static void
cpm_manager_engine_fully_charged_cb (GpmEngine *engine, UpDevice *device, GpmManager *manager)
{
	UpDeviceKind kind;
	gchar *native_path = NULL;
	gboolean ret;
	guint plural = 1;
	const gchar *title;

	/* only action this if specified in the setings */
	ret = g_settings_get_boolean (manager->priv->settings, GPM_SETTINGS_NOTIFY_FULLY_CHARGED);
	if (!ret) {
		egg_debug ("no notification");
		goto out;
	}

	/* don't show when running under GDM */
	if (g_getenv ("RUNNING_UNDER_GDM") != NULL) {
		egg_debug ("running under gdm, so no notification");
		goto out;
	}

	/* get device properties */
	g_object_get (device,
		      "kind", &kind,
		      "native-path", &native_path,
		      NULL);

	if (kind == UP_DEVICE_KIND_BATTERY) {
		/* is this a dummy composite device, which is plural? */
		if (g_str_has_prefix (native_path, "dummy"))
			plural = 2;

		/* hide the discharging notification */
		cpm_manager_notify_close (manager, manager->priv->notification_warning_low);
		cpm_manager_notify_close (manager, manager->priv->notification_discharging);

		/* TRANSLATORS: show the charged notification */
		title = ngettext ("Battery Charged", "Batteries Charged", plural);
		cpm_manager_notify (manager, &manager->priv->notification_fully_charged,
				    title, NULL, GPM_MANAGER_NOTIFY_TIMEOUT_SHORT,
				    "dialog-information", NOTIFY_URGENCY_LOW);
	}
out:
	g_free (native_path);
}

/**
 * cpm_manager_engine_discharging_cb:
 */
static void
cpm_manager_engine_discharging_cb (GpmEngine *engine, UpDevice *device, GpmManager *manager)
{
	UpDeviceKind kind;
	gboolean ret;
	const gchar *title;
	const gchar *message;
	gdouble percentage;
	gint64 time_to_empty;
	gchar *remaining_text = NULL;
	gchar *icon = NULL;
	const gchar *kind_desc;

	/* only action this if specified in the settings */
	ret = g_settings_get_boolean (manager->priv->settings, GPM_SETTINGS_NOTIFY_DISCHARGING);
	if (!ret) {
		egg_debug ("no notification");
		goto out;
	}

	/* get device properties */
	g_object_get (device,
		      "kind", &kind,
		      "percentage", &percentage,
		      "time-to-empty", &time_to_empty,
		      NULL);

	/* only show text if there is a valid time */
	if (time_to_empty > 0)
		remaining_text = cpm_get_timestring (time_to_empty);
	kind_desc = cpm_device_kind_to_localised_string (kind, 1);

	if (kind == UP_DEVICE_KIND_BATTERY) {
		/* TRANSLATORS: laptop battery is now discharging */
		title = _("Battery Discharging");

		if (remaining_text != NULL) {
			/* TRANSLATORS: tell the user how much time they have got */
			message = g_strdup_printf (_("%s of battery power remaining (%.0f%%)"), remaining_text, percentage);
		} else {
			/* TRANSLATORS: the device is discharging, but we only have a percentage */
			message = g_strdup_printf (_("%s discharging (%.0f%%)"),
						   kind_desc, percentage);
		}
	} else if (kind == UP_DEVICE_KIND_UPS) {
		/* TRANSLATORS: UPS is now discharging */
		title = _("UPS Discharging");

		if (remaining_text != NULL) {
			/* TRANSLATORS: tell the user how much time they have got */
			message = g_strdup_printf (_("%s of UPS backup power remaining (%.0f%%)"), remaining_text, percentage);
		} else {
			/* TRANSLATORS: the device is discharging, but we only have a percentage */
			message = g_strdup_printf (_("%s discharging (%.0f%%)"),
						   kind_desc, percentage);
		}
	} else {
		/* nothing else of interest */
		goto out;
	}

	icon = cpm_upower_get_device_icon (device);
	/* show the notification */
	cpm_manager_notify (manager, &manager->priv->notification_discharging, title, message, GPM_MANAGER_NOTIFY_TIMEOUT_LONG,
			    icon, NOTIFY_URGENCY_NORMAL);
out:
	g_free (icon);
	g_free (remaining_text);
	return;
}

/**
 * cpm_manager_engine_just_laptop_battery:
 */
static gboolean
cpm_manager_engine_just_laptop_battery (GpmManager *manager)
{
	UpDevice *device;
	UpDeviceKind kind;
	GPtrArray *array;
	gboolean ret = TRUE;
	guint i;

	/* find if there are any other device types that mean we have to
	 * be more specific in our wording */
	array = cpm_engine_get_devices (manager->priv->engine);
	for (i=0; i<array->len; i++) {
		device = g_ptr_array_index (array, i);
		g_object_get (device, "kind", &kind, NULL);
		if (kind != UP_DEVICE_KIND_BATTERY) {
			ret = FALSE;
			break;
		}
	}
	g_ptr_array_unref (array);
	return ret;
}

/**
 * cpm_manager_engine_charge_low_cb:
 */
static void
cpm_manager_engine_charge_low_cb (GpmEngine *engine, UpDevice *device, GpmManager *manager)
{
	const gchar *title = NULL;
	gchar *message = NULL;
	gchar *remaining_text;
	gchar *icon = NULL;
	UpDeviceKind kind;
	gdouble percentage;
	gint64 time_to_empty;
	gboolean ret;

	/* get device properties */
	g_object_get (device,
		      "kind", &kind,
		      "percentage", &percentage,
		      "time-to-empty", &time_to_empty,
		      NULL);

	/* check to see if the batteries have not noticed we are on AC */
	if (kind == UP_DEVICE_KIND_BATTERY) {
		if (!manager->priv->on_battery) {
			egg_warning ("ignoring critically low message as we are not on battery power");
			goto out;
		}
	}

	if (kind == UP_DEVICE_KIND_BATTERY) {

		/* if the user has no other batteries, drop the "Laptop" wording */
		ret = cpm_manager_engine_just_laptop_battery (manager);
		if (ret) {
			/* TRANSLATORS: laptop battery low, and we only have one battery */
			title = _("Battery low");
		} else {
			/* TRANSLATORS: laptop battery low, and we have more than one kind of battery */
			title = _("Laptop battery low");
		}

		remaining_text = cpm_get_timestring (time_to_empty);

		/* TRANSLATORS: tell the user how much time they have got */
		message = g_strdup_printf (_("Approxicafely <b>%s</b> remaining (%.0f%%)"), remaining_text, percentage);

	} else if (kind == UP_DEVICE_KIND_UPS) {
		/* TRANSLATORS: UPS is starting to get a little low */
		title = _("UPS low");
		remaining_text = cpm_get_timestring (time_to_empty);

		/* TRANSLATORS: tell the user how much time they have got */
		message = g_strdup_printf (_("Approxicafely <b>%s</b> of remaining UPS backup power (%.0f%%)"),
					   remaining_text, percentage);
	} else if (kind == UP_DEVICE_KIND_MOUSE) {
		gboolean notify = g_settings_get_boolean (manager->priv->settings,
				GPM_SETTINGS_NOTIFY_LOW_CAPACITY_MOUSE);
		if(!notify)
			goto out;

		/* TRANSLATORS: mouse is getting a little low */
		title = _("Mouse battery low");

		/* TRANSLATORS: tell user more details */
		message = g_strdup_printf (_("Wireless mouse is low in power (%.0f%%)"), percentage);

	} else if (kind == UP_DEVICE_KIND_KEYBOARD) {
		/* TRANSLATORS: keyboard is getting a little low */
		title = _("Keyboard battery low");

		/* TRANSLATORS: tell user more details */
		message = g_strdup_printf (_("Wireless keyboard is low in power (%.0f%%)"), percentage);

	} else if (kind == UP_DEVICE_KIND_PDA) {
		/* TRANSLATORS: PDA is getting a little low */
		title = _("PDA battery low");

		/* TRANSLATORS: tell user more details */
		message = g_strdup_printf (_("PDA is low in power (%.0f%%)"), percentage);

	} else if (kind == UP_DEVICE_KIND_PHONE) {
		/* TRANSLATORS: cell phone (mobile) is getting a little low */
		title = _("Cell phone battery low");

		/* TRANSLATORS: tell user more details */
		message = g_strdup_printf (_("Cell phone is low in power (%.0f%%)"), percentage);

	} else if (kind == UP_DEVICE_KIND_MEDIA_PLAYER) {
		/* TRANSLATORS: media player, e.g. mp3 is getting a little low */
		title = _("Media player battery low");

		/* TRANSLATORS: tell user more details */
		message = g_strdup_printf (_("Media player is low in power (%.0f%%)"), percentage);

	} else if (kind == UP_DEVICE_KIND_TABLET) {
		/* TRANSLATORS: graphics tablet, e.g. wacom is getting a little low */
		title = _("Tablet battery low");

		/* TRANSLATORS: tell user more details */
		message = g_strdup_printf (_("Tablet is low in power (%.0f%%)"), percentage);

	} else if (kind == UP_DEVICE_KIND_COMPUTER) {
		/* TRANSLATORS: computer, e.g. ipad is getting a little low */
		title = _("Attached computer battery low");

		/* TRANSLATORS: tell user more details */
		message = g_strdup_printf (_("Attached computer is low in power (%.0f%%)"), percentage);
	}

	/* get correct icon */
	icon = cpm_upower_get_device_icon (device);
	cpm_manager_notify (manager, &manager->priv->notification_warning_low, title, message, GPM_MANAGER_NOTIFY_TIMEOUT_LONG, icon, NOTIFY_URGENCY_NORMAL);
	cpm_manager_play (manager, GPM_MANAGER_SOUND_BATTERY_CAUTION, TRUE);
out:
	g_free (icon);
	g_free (message);
}

/**
 * cpm_manager_engine_charge_critical_cb:
 */
static void
cpm_manager_engine_charge_critical_cb (GpmEngine *engine, UpDevice *device, GpmManager *manager)
{
	const gchar *title = NULL;
	gchar *message = NULL;
	gchar *icon = NULL;
	UpDeviceKind kind;
	gdouble percentage;
	gint64 time_to_empty;
	GpmActionPolicy policy;
	gboolean ret;

	/* get device properties */
	g_object_get (device,
		      "kind", &kind,
		      "percentage", &percentage,
		      "time-to-empty", &time_to_empty,
		      NULL);

	/* check to see if the batteries have not noticed we are on AC */
	if (kind == UP_DEVICE_KIND_BATTERY) {
		if (!manager->priv->on_battery) {
			egg_warning ("ignoring critically low message as we are not on battery power");
			goto out;
		}
	}

	if (kind == UP_DEVICE_KIND_BATTERY) {

		/* if the user has no other batteries, drop the "Laptop" wording */
		ret = cpm_manager_engine_just_laptop_battery (manager);
		if (ret) {
			/* TRANSLATORS: laptop battery critically low, and only have one kind of battery */
			title = _("Battery critically low");
		} else {
			/* TRANSLATORS: laptop battery critically low, and we have more than one type of battery */
			title = _("Laptop battery critically low");
		}

		/* we have to do different warnings depending on the policy */
		policy = g_settings_get_enum (manager->priv->settings, GPM_SETTINGS_ACTION_CRITICAL_BATT);

		/* use different text for different actions */
		if (policy == GPM_ACTION_POLICY_NOTHING) {
			/* TRANSLATORS: tell the use to insert the plug, as we're not going to do anything */
			message = g_strdup (_("Plug in your AC adapter to avoid losing data."));

		} else if (policy == GPM_ACTION_POLICY_SUSPEND) {
			/* TRANSLATORS: give the user a ultimatum */
			message = g_strdup_printf (_("Computer will suspend very soon unless it is plugged in."));

		} else if (policy == GPM_ACTION_POLICY_HIBERNATE) {
			/* TRANSLATORS: give the user a ultimatum */
			message = g_strdup_printf (_("Computer will hibernate very soon unless it is plugged in."));

		} else if (policy == GPM_ACTION_POLICY_SHUTDOWN) {
			/* TRANSLATORS: give the user a ultimatum */
			message = g_strdup_printf (_("Computer will shutdown very soon unless it is plugged in."));
		}

	} else if (kind == UP_DEVICE_KIND_UPS) {
		gchar *remaining_text;

		/* TRANSLATORS: the UPS is very low */
		title = _("UPS critically low");
		remaining_text = cpm_get_timestring (time_to_empty);

		/* TRANSLATORS: give the user a ultimatum */
		message = g_strdup_printf (_("Approxicafely <b>%s</b> of remaining UPS power (%.0f%%). "
					     "Restore AC power to your computer to avoid losing data."),
					   remaining_text, percentage);
		g_free (remaining_text);
	} else if (kind == UP_DEVICE_KIND_MOUSE) {
		gboolean notify = g_settings_get_boolean (manager->priv->settings,
				GPM_SETTINGS_NOTIFY_LOW_CAPACITY_MOUSE);
		if(!notify)
			goto out;

		/* TRANSLATORS: the mouse battery is very low */
		title = _("Mouse battery low");

		/* TRANSLATORS: the device is just going to stop working */
		message = g_strdup_printf (_("Wireless mouse is very low in power (%.0f%%). "
					     "This device will soon stop functioning if not charged."),
					   percentage);
	} else if (kind == UP_DEVICE_KIND_KEYBOARD) {
		/* TRANSLATORS: the keyboard battery is very low */
		title = _("Keyboard battery low");

		/* TRANSLATORS: the device is just going to stop working */
		message = g_strdup_printf (_("Wireless keyboard is very low in power (%.0f%%). "
					     "This device will soon stop functioning if not charged."),
					   percentage);
	} else if (kind == UP_DEVICE_KIND_PDA) {

		/* TRANSLATORS: the PDA battery is very low */
		title = _("PDA battery low");

		/* TRANSLATORS: the device is just going to stop working */
		message = g_strdup_printf (_("PDA is very low in power (%.0f%%). "
					     "This device will soon stop functioning if not charged."),
					   percentage);

	} else if (kind == UP_DEVICE_KIND_PHONE) {

		/* TRANSLATORS: the cell battery is very low */
		title = _("Cell phone battery low");

		/* TRANSLATORS: the device is just going to stop working */
		message = g_strdup_printf (_("Cell phone is very low in power (%.0f%%). "
					     "This device will soon stop functioning if not charged."),
					   percentage);

	} else if (kind == UP_DEVICE_KIND_MEDIA_PLAYER) {

		/* TRANSLATORS: the cell battery is very low */
		title = _("Cell phone battery low");

		/* TRANSLATORS: the device is just going to stop working */
		message = g_strdup_printf (_("Media player is very low in power (%.0f%%). "
					     "This device will soon stop functioning if not charged."),
					   percentage);
	} else if (kind == UP_DEVICE_KIND_TABLET) {

		/* TRANSLATORS: the cell battery is very low */
		title = _("Tablet battery low");

		/* TRANSLATORS: the device is just going to stop working */
		message = g_strdup_printf (_("Tablet is very low in power (%.0f%%). "
					     "This device will soon stop functioning if not charged."),
					   percentage);
	} else if (kind == UP_DEVICE_KIND_COMPUTER) {

		/* TRANSLATORS: the cell battery is very low */
		title = _("Attached computer battery low");

		/* TRANSLATORS: the device is just going to stop working */
		message = g_strdup_printf (_("Attached computer is very low in power (%.0f%%). "
					     "The device will soon shutdown if not charged."),
					   percentage);
	}

	/* get correct icon */
	icon = cpm_upower_get_device_icon (device);
	cpm_manager_notify (manager, &manager->priv->notification_warning_low, title, message, GPM_MANAGER_NOTIFY_TIMEOUT_NEVER, icon, NOTIFY_URGENCY_CRITICAL);

	switch (kind) {

	case UP_DEVICE_KIND_BATTERY:
	case UP_DEVICE_KIND_UPS:
		egg_debug ("critical charge level reached, starting sound loop");
		cpm_manager_play_loop_start (manager,
					     GPM_MANAGER_SOUND_BATTERY_LOW,
					     TRUE,
					     GPM_MANAGER_CRITICAL_ALERT_TIMEOUT);
		break;

	default:
		cpm_manager_play (manager, GPM_MANAGER_SOUND_BATTERY_LOW, TRUE);
	}
out:
	g_free (icon);
	g_free (message);
}

/**
 * cpm_manager_engine_charge_action_cb:
 */
static void
cpm_manager_engine_charge_action_cb (GpmEngine *engine, UpDevice *device, GpmManager *manager)
{
	const gchar *title = NULL;
	gchar *message = NULL;
	gchar *icon = NULL;
	UpDeviceKind kind;
	GpmActionPolicy policy;
	guint timer_id;

	/* get device properties */
	g_object_get (device,
		      "kind", &kind,
		      NULL);

	/* check to see if the batteries have not noticed we are on AC */
	if (kind == UP_DEVICE_KIND_BATTERY) {
		if (!manager->priv->on_battery) {
			egg_warning ("ignoring critically low message as we are not on battery power");
			goto out;
		}
	}

	if (kind == UP_DEVICE_KIND_BATTERY) {

		/* TRANSLATORS: laptop battery is really, really, low */
		title = _("Laptop battery critically low");

		/* we have to do different warnings depending on the policy */
		policy = g_settings_get_enum (manager->priv->settings, GPM_SETTINGS_ACTION_CRITICAL_BATT);

		/* use different text for different actions */
		if (policy == GPM_ACTION_POLICY_NOTHING) {
			/* TRANSLATORS: computer will shutdown without saving data */
			message = g_strdup (_("The battery is below the critical level and "
					      "this computer will <b>power-off</b> when the "
					      "battery becomes completely empty."));

		} else if (policy == GPM_ACTION_POLICY_SUSPEND) {
			/* TRANSLATORS: computer will suspend */
			message = g_strdup (_("The battery is below the critical level and "
					      "this computer is about to suspend.<br>"
					      "<b>NOTE:</b> A small amount of power is required "
					      "to keep your computer in a suspended state."));

		} else if (policy == GPM_ACTION_POLICY_HIBERNATE) {
			/* TRANSLATORS: computer will hibernate */
			message = g_strdup (_("The battery is below the critical level and "
					      "this computer is about to hibernate."));

		} else if (policy == GPM_ACTION_POLICY_SHUTDOWN) {
			/* TRANSLATORS: computer will just shutdown */
			message = g_strdup (_("The battery is below the critical level and "
					      "this computer is about to shutdown."));
		}

		/* wait 20 seconds for user-panic */
		timer_id = g_timeout_add_seconds (20, (GSourceFunc) manager_critical_action_do, manager);
		g_source_set_name_by_id (timer_id, "[GpmManager] battery critical-action");

	} else if (kind == UP_DEVICE_KIND_UPS) {
		/* TRANSLATORS: UPS is really, really, low */
		title = _("UPS critically low");

		/* we have to do different warnings depending on the policy */
		policy = g_settings_get_enum (manager->priv->settings, GPM_SETTINGS_ACTION_CRITICAL_UPS);

		/* use different text for different actions */
		if (policy == GPM_ACTION_POLICY_NOTHING) {
			/* TRANSLATORS: computer will shutdown without saving data */
			message = g_strdup (_("The UPS is below the critical level and "
				              "this computer will <b>power-off</b> when the "
				              "UPS becomes completely empty."));

		} else if (policy == GPM_ACTION_POLICY_HIBERNATE) {
			/* TRANSLATORS: computer will hibernate */
			message = g_strdup (_("The UPS is below the critical level and "
				              "this computer is about to hibernate."));

		} else if (policy == GPM_ACTION_POLICY_SHUTDOWN) {
			/* TRANSLATORS: computer will just shutdown */
			message = g_strdup (_("The UPS is below the critical level and "
				              "this computer is about to shutdown."));
		}

		/* wait 20 seconds for user-panic */
		timer_id = g_timeout_add_seconds (20, (GSourceFunc) manager_critical_action_do, manager);
		g_source_set_name_by_id (timer_id, "[GpmManager] ups critical-action");

	}

	/* not all types have actions */
	if (title == NULL) {
		g_free (message);
		return;
	}

	/* get correct icon */
	icon = cpm_upower_get_device_icon (device);
	cpm_manager_notify (manager, &manager->priv->notification_warning_low,
			    title, message, GPM_MANAGER_NOTIFY_TIMEOUT_NEVER,
			    icon, NOTIFY_URGENCY_CRITICAL);
	cpm_manager_play (manager, GPM_MANAGER_SOUND_BATTERY_LOW, TRUE);
out:
	g_free (icon);
	g_free (message);
}

/**
 * cpm_manager_dpms_mode_changed_cb:
 * @mode: The DPMS mode, e.g. GPM_DPMS_MODE_OFF
 * @info: This class instance
 *
 * Log when the DPMS mode is changed.
 **/
static void
cpm_manager_dpms_mode_changed_cb (GpmDpms *dpms, GpmDpmsMode mode, GpmManager *manager)
{
	egg_debug ("DPMS mode changed: %d", mode);

	if (mode == GPM_DPMS_MODE_ON)
		egg_debug ("dpms on");
	else if (mode == GPM_DPMS_MODE_STANDBY)
		egg_debug ("dpms standby");
	else if (mode == GPM_DPMS_MODE_SUSPEND)
		egg_debug ("suspend");
	else if (mode == GPM_DPMS_MODE_OFF)
		egg_debug ("dpms off");

	cpm_manager_update_dpms_throttle (manager);
}

/*
 * cpm_manager_reset_just_resumed_cb
 */
static gboolean
cpm_manager_reset_just_resumed_cb (gpointer user_data)
{
	GpmManager *manager = GPM_MANAGER (user_data);

	if (manager->priv->notification_general != NULL)
		cpm_manager_notify_close (manager, manager->priv->notification_general);
	if (manager->priv->notification_warning_low != NULL)
		cpm_manager_notify_close (manager, manager->priv->notification_warning_low);
	if (manager->priv->notification_discharging != NULL)
		cpm_manager_notify_close (manager, manager->priv->notification_discharging);
	if (manager->priv->notification_fully_charged != NULL)
		cpm_manager_notify_close (manager, manager->priv->notification_fully_charged);

	manager->priv->just_resumed = FALSE;
	return FALSE;
}

/**
 * cpm_manager_control_resume_cb
 **/
static void
cpm_manager_control_resume_cb (GpmControl *control, GpmControlAction action, GpmManager *manager)
{
	guint timer_id;
	manager->priv->just_resumed = TRUE;
	timer_id = g_timeout_add_seconds (1, cpm_manager_reset_just_resumed_cb, manager);
	g_source_set_name_by_id (timer_id, "[GpmManager] just-resumed");
}

/**
 * cpm_main_systemd_inhibit:
 *
 * Return a fd to the to the inhibitor, that we can close on exit.
 *
 * Return value: fd, -1 on error
 **/
static gint32
cpm_manager_systemd_inhibit (GDBusProxy *proxy) {
    GError *error = NULL;
    gint32 r = -1;
    gint32 fd = -1;
    GVariant *res;
    GUnixFDList *fd_list = NULL;

    /* Should we define these elsewhere? */
    const char* arg_what = "handle-power-key:handle-suspend-key:handle-lid-switch";
    const char* arg_who = g_get_user_name ();
    const char* arg_why = "Cafe power manager handles these events";
    const char* arg_mode = "block";

	egg_debug ("Inhibiting systemd sleep");
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
        return -1;
    }
    res = g_dbus_proxy_call_with_unix_fd_list_sync (proxy, "Inhibit", 
                            g_variant_new( "(ssss)",
                                arg_what,
                                arg_who,
                                arg_why,
                                arg_mode
                            ),
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            NULL,
                            &fd_list,
                            NULL,
                            &error
                            );
    if (error == NULL && res != NULL) {
        g_variant_get(res, "(h)", &r);
	    egg_debug ("Inhibiting systemd sleep res = %i", r);
        fd = g_unix_fd_list_get (fd_list, r, &error); 
        if (fd == -1) {
            egg_debug("Failed to get systemd inhibitor");
            return r;
        }
        egg_debug ("System inhibitor fd is %d", fd);
        g_object_unref (fd_list);
        g_variant_unref (res);
    } else if (error != NULL || res == NULL) {
        egg_error ("Error in dbus - %s", error->message);
        g_error_free (error);
        return -EIO;
    }
    egg_debug ("Inhibiting systemd sleep - success");
    return r;
}

static void
on_icon_theme_change (CtkSettings *settings,
                      GParamSpec  *pspec,
                      GpmManager  *manager)
{
	gchar *icon = cpm_engine_get_icon (manager->priv->engine);
	if (icon == NULL) {
		return;
	}

	cpm_tray_icon_set_icon (manager->priv->tray_icon, icon);
	g_free (icon);
}

/**
 * cpm_manager_init:
 * @manager: This class instance
 **/
static void
cpm_manager_init (GpmManager *manager)
{
	gboolean check_type_cpu;
	DBusGConnection *connection;
	GDBusConnection *g_connection;
	GError *error = NULL;

	manager->priv = cpm_manager_get_instance_private (manager);
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	g_connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

	/* We want to inhibit the systemd suspend options, and take care of them ourselves */
	if (LOGIND_RUNNING()) {
		manager->priv->systemd_inhibit = cpm_manager_systemd_inhibit (manager->priv->systemd_inhibit_proxy);
	}

	/* init to unthrottled */
	manager->priv->screensaver_ac_throttle_id = 0;
	manager->priv->screensaver_dpms_throttle_id = 0;
	manager->priv->screensaver_lid_throttle_id = 0;

	manager->priv->critical_alert_timeout_id = 0;
	manager->priv->critical_alert_loop_props = NULL;

	/* init to not just_resumed */
	manager->priv->just_resumed = FALSE;

	/* don't apply policy when not active, so listen to ConsoleKit */
	manager->priv->console = egg_console_kit_new ();

	manager->priv->notification_general = NULL;
	manager->priv->notification_warning_low = NULL;
	manager->priv->notification_discharging = NULL;
	manager->priv->notification_fully_charged = NULL;
	manager->priv->settings = g_settings_new (GPM_SETTINGS_SCHEMA);
	g_signal_connect (manager->priv->settings, "changed",
			  G_CALLBACK (cpm_manager_settings_changed_cb), manager);
	manager->priv->client = up_client_new ();
	g_signal_connect (manager->priv->client, "notify::lid-is-closed",
			  G_CALLBACK (cpm_manager_client_changed_cb), manager);
	g_signal_connect (manager->priv->client, "notify::on-battery",
			  G_CALLBACK (cpm_manager_client_changed_cb), manager);

	/* use libcafenotify */
	notify_init (GPM_NAME);

	/* coldplug so we are in the correct state at startup */
	g_object_get (manager->priv->client,
		      "on-battery", &manager->priv->on_battery,
		      NULL);

	manager->priv->button = cpm_button_new ();
	g_signal_connect (manager->priv->button, "button-pressed",
			  G_CALLBACK (cpm_manager_button_pressed_cb), manager);

	/* try and start an interactive service */
	manager->priv->screensaver = cpm_screensaver_new ();

	/* try an start an interactive service */
	manager->priv->backlight = cpm_backlight_new ();
	if (manager->priv->backlight != NULL) {
		/* add the new brightness lcd DBUS interface */
		dbus_g_object_type_install_info (GPM_TYPE_BACKLIGHT,
						 &dbus_glib_cpm_backlight_object_info);
		dbus_g_connection_register_g_object (connection, GPM_DBUS_PATH_BACKLIGHT,
						     G_OBJECT (manager->priv->backlight));
	}

    manager->priv->kbd_backlight = cpm_kbd_backlight_new ();
    if (manager->priv->kbd_backlight != NULL) {
    	dbus_g_object_type_install_info (GPM_TYPE_KBD_BACKLIGHT,
						 &dbus_glib_cpm_kbd_backlight_object_info);
		dbus_g_connection_register_g_object (connection, GPM_DBUS_PATH_KBD_BACKLIGHT,
						     G_OBJECT (manager->priv->kbd_backlight));
    
    }

	manager->priv->idle = cpm_idle_new ();
	g_signal_connect (manager->priv->idle, "idle-changed",
			  G_CALLBACK (cpm_manager_idle_changed_cb), manager);

	/* set up the check_type_cpu, so we can disable the CPU load check */
	check_type_cpu = g_settings_get_boolean (manager->priv->settings, GPM_SETTINGS_IDLE_CHECK_CPU);
	cpm_idle_set_check_cpu (manager->priv->idle, check_type_cpu);

	manager->priv->dpms = cpm_dpms_new ();
	g_signal_connect (manager->priv->dpms, "mode-changed",
			  G_CALLBACK (cpm_manager_dpms_mode_changed_cb), manager);

	/* use the control object */
	egg_debug ("creating new control instance");
	manager->priv->control = cpm_control_new ();
	g_signal_connect (manager->priv->control, "resume",
			  G_CALLBACK (cpm_manager_control_resume_cb), manager);

	egg_debug ("creating new tray icon");
	manager->priv->tray_icon = cpm_tray_icon_new ();

	/* keep a reference for the notifications */
	manager->priv->status_icon = cpm_tray_icon_get_status_icon (manager->priv->tray_icon);

	cpm_manager_sync_policy_sleep (manager);

	manager->priv->engine = cpm_engine_new ();
	g_signal_connect (manager->priv->engine, "low-capacity",
			  G_CALLBACK (cpm_manager_engine_low_capacity_cb), manager);
	g_signal_connect (manager->priv->engine, "icon-changed",
			  G_CALLBACK (cpm_manager_engine_icon_changed_cb), manager);
	g_signal_connect (manager->priv->engine, "summary-changed",
			  G_CALLBACK (cpm_manager_engine_summary_changed_cb), manager);
	g_signal_connect (manager->priv->engine, "fully-charged",
			  G_CALLBACK (cpm_manager_engine_fully_charged_cb), manager);
	g_signal_connect (manager->priv->engine, "discharging",
			  G_CALLBACK (cpm_manager_engine_discharging_cb), manager);
	g_signal_connect (manager->priv->engine, "charge-low",
			  G_CALLBACK (cpm_manager_engine_charge_low_cb), manager);
	g_signal_connect (manager->priv->engine, "charge-critical",
			  G_CALLBACK (cpm_manager_engine_charge_critical_cb), manager);
	g_signal_connect (manager->priv->engine, "charge-action",
			  G_CALLBACK (cpm_manager_engine_charge_action_cb), manager);

	g_signal_connect (ctk_settings_get_default (),
	                  "notify::ctk-icon-theme-name",
	                  G_CALLBACK (on_icon_theme_change),
	                  manager);

	/* update ac throttle */
	cpm_manager_update_ac_throttle (manager);
}

/**
 * cpm_manager_finalize:
 * @object: The object to finalize
 *
 * Finalise the manager, by unref'ing all the depending modules.
 **/
static void
cpm_manager_finalize (GObject *object)
{
	GpmManager *manager;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GPM_IS_MANAGER (object));

	manager = GPM_MANAGER (object);

	g_return_if_fail (manager->priv != NULL);

	/* close any notifications (also unrefs them) */
	if (manager->priv->notification_general != NULL)
		cpm_manager_notify_close (manager, manager->priv->notification_general);
	if (manager->priv->notification_warning_low != NULL)
		cpm_manager_notify_close (manager, manager->priv->notification_warning_low);
	if (manager->priv->notification_discharging != NULL)
		cpm_manager_notify_close (manager, manager->priv->notification_discharging);
	if (manager->priv->notification_fully_charged != NULL)
		cpm_manager_notify_close (manager, manager->priv->notification_fully_charged);

	if (manager->priv->critical_alert_timeout_id != 0) {
		g_source_remove (manager->priv->critical_alert_timeout_id);
		manager->priv->critical_alert_timeout_id = 0;
	}

	g_signal_handlers_disconnect_by_func (ctk_settings_get_default (),
	                                      on_icon_theme_change,
	                                      manager);

	g_object_unref (manager->priv->settings);
	g_object_unref (manager->priv->dpms);
	g_object_unref (manager->priv->idle);
	g_object_unref (manager->priv->engine);
	g_object_unref (manager->priv->tray_icon);
	g_object_unref (manager->priv->screensaver);
	g_object_unref (manager->priv->control);
	g_object_unref (manager->priv->button);
	g_object_unref (manager->priv->backlight);
	g_object_unref (manager->priv->kbd_backlight);
	g_object_unref (manager->priv->console);
	g_object_unref (manager->priv->client);
	g_object_unref (manager->priv->status_icon);

	if (LOGIND_RUNNING()) {
		/* Let systemd take over again ... */
		if (manager->priv->systemd_inhibit > 0) {
			close(manager->priv->systemd_inhibit);
		}
		if (manager->priv->systemd_inhibit_proxy != NULL) {
			g_object_unref (manager->priv->systemd_inhibit_proxy);
		}
	}

	G_OBJECT_CLASS (cpm_manager_parent_class)->finalize (object);
}

/**
 * cpm_manager_new:
 *
 * Return value: a new GpmManager object.
 **/
GpmManager *
cpm_manager_new (void)
{
	GpmManager *manager;
	manager = g_object_new (GPM_TYPE_MANAGER, NULL);
	return GPM_MANAGER (manager);
}
