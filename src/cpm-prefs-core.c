/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2005 Jaap Haitsma <jaap@haitsma.org>
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

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>

#include <ctk/ctk.h>
#include <math.h>
#include <string.h>
#define UPOWER_ENABLE_DEPRECATED
#include <libupower-glib/upower.h>

#include "egg-debug.h"
#include "egg-console-kit.h"

#include "cpm-tray-icon.h"
#include "cpm-common.h"
#include "cpm-prefs-core.h"
#include "cpm-icon-names.h"
#include "cpm-brightness.h"

static void cpm_prefs_finalize (GObject *object);

struct CpmPrefsPrivate
{
	UpClient		*client;
	CtkBuilder		*builder;
	gboolean		 has_batteries;
	gboolean		 has_lcd;
	gboolean		 has_ups;
	gboolean		 has_button_lid;
	gboolean		 has_button_suspend;
	gboolean		 can_shutdown;
	gboolean		 can_suspend;
	gboolean		 can_hibernate;
	GSettings		*settings;
	EggConsoleKit		*console;
};

enum {
	ACTION_HELP,
	ACTION_CLOSE,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (CpmPrefs, cpm_prefs, G_TYPE_OBJECT)

/**
 * cpm_prefs_class_init:
 * @klass: This prefs class instance
 **/
static void
cpm_prefs_class_init (CpmPrefsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = cpm_prefs_finalize;

	signals [ACTION_HELP] =
		g_signal_new ("action-help",
				  G_TYPE_FROM_CLASS (object_class),
				  G_SIGNAL_RUN_LAST,
				  G_STRUCT_OFFSET (CpmPrefsClass, action_help),
				  NULL,
				  NULL,
				  g_cclosure_marshal_VOID__VOID,
				  G_TYPE_NONE, 0);
	signals [ACTION_CLOSE] =
		g_signal_new ("action-close",
				  G_TYPE_FROM_CLASS (object_class),
				  G_SIGNAL_RUN_LAST,
				  G_STRUCT_OFFSET (CpmPrefsClass, action_close),
				  NULL,
				  NULL,
				  g_cclosure_marshal_VOID__VOID,
				  G_TYPE_NONE, 0);
}

/**
 * cpm_prefs_activate_window:
 * @prefs: This prefs class instance
 *
 * Activates (shows) the window.
 **/
void
cpm_prefs_activate_window (CtkApplication *app, CpmPrefs *prefs)
{
	CtkWindow *window;
	window = CTK_WINDOW (ctk_builder_get_object (prefs->priv->builder, "dialog_preferences"));
	ctk_application_add_window (CTK_APPLICATION (app), window);
	ctk_window_present (window);
}

/**
 * cpm_prefs_help_cb:
 * @widget: The CtkWidget object
 * @prefs: This prefs class instance
 **/
static void
cpm_prefs_help_cb (CtkWidget *widget G_GNUC_UNUSED,
		   CpmPrefs  *prefs)
{
	egg_debug ("emitting action-help");
	g_signal_emit (prefs, signals [ACTION_HELP], 0);
}

/**
 * cpm_prefs_icon_radio_cb:
 * @widget: The CtkWidget object
 **/
static void
cpm_prefs_icon_radio_cb (CtkWidget *widget, CpmPrefs *prefs)
{
	gint policy;

	policy = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget), "policy"));
	g_settings_set_enum (prefs->priv->settings, CPM_SETTINGS_ICON_POLICY, policy);
}

/**
 * cpm_prefs_format_percentage_cb:
 * @scale: The CtkScale object
 * @value: The value in %.
 **/
static gchar *
cpm_prefs_format_percentage_cb (CtkScale *scale G_GNUC_UNUSED,
				gdouble   value)
{
	return g_strdup_printf ("%.0f%%", value);
}

/**
 * cpm_prefs_action_combo_changed_cb:
 **/
static void
cpm_prefs_action_combo_changed_cb (CtkWidget *widget, CpmPrefs *prefs)
{
	CpmActionPolicy policy;
	const CpmActionPolicy *actions;
	const gchar *cpm_pref_key;
	guint active;

	actions = (const CpmActionPolicy *) g_object_get_data (G_OBJECT (widget), "actions");
	cpm_pref_key = (const gchar *) g_object_get_data (G_OBJECT (widget), "settings_key");

	active = ctk_combo_box_get_active (CTK_COMBO_BOX (widget));
	policy = actions[active];
	g_settings_set_enum (prefs->priv->settings, cpm_pref_key, policy);
}

/**
 * cpm_prefs_action_time_changed_cb:
 **/
static void
cpm_prefs_action_time_changed_cb (CtkWidget *widget, CpmPrefs *prefs)
{
	guint value;
	const gint *values;
	const gchar *cpm_pref_key;
	guint active;

	values = (const gint *) g_object_get_data (G_OBJECT (widget), "values");
	cpm_pref_key = (const gchar *) g_object_get_data (G_OBJECT (widget), "settings_key");

	active = ctk_combo_box_get_active (CTK_COMBO_BOX (widget));
	value = values[active];

	egg_debug ("Changing %s to %i", cpm_pref_key, value);
	g_settings_set_int (prefs->priv->settings, cpm_pref_key, value);
}

/**
 * cpm_prefs_actions_destroy_cb:
 **/
static void
cpm_prefs_actions_destroy_cb (CpmActionPolicy *array)
{
	g_free (array);
}

/**
 * cpm_prefs_setup_action_combo:
 * @prefs: This prefs class instance
 * @widget_name: The CtkWidget name
 * @cpm_pref_key: The settings key for this preference setting.
 * @actions: The actions to associate in an array.
 **/
static void
cpm_prefs_setup_action_combo (CpmPrefs *prefs, const gchar *widget_name,
				  const gchar *cpm_pref_key, const CpmActionPolicy *actions)
{
	gint i;
	gboolean is_writable;
	CtkWidget *widget;
	CpmActionPolicy policy;
	CpmActionPolicy	value;
	GPtrArray *array;
	CpmActionPolicy *actions_added;

	widget = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder, widget_name));

	value = g_settings_get_enum (prefs->priv->settings, cpm_pref_key);
	is_writable = g_settings_is_writable (prefs->priv->settings, cpm_pref_key);

	ctk_widget_set_sensitive (widget, is_writable);

	array = g_ptr_array_new ();
	g_object_set_data (G_OBJECT (widget), "settings_key", (gpointer) cpm_pref_key);
	g_signal_connect (G_OBJECT (widget), "changed",
			  G_CALLBACK (cpm_prefs_action_combo_changed_cb), prefs);

	for (i=0; actions[i] != -1; i++) {
		policy = actions[i];
		if (policy == CPM_ACTION_POLICY_SHUTDOWN && !prefs->priv->can_shutdown) {
			egg_debug ("Cannot add option, as cannot shutdown.");
		} else if (policy == CPM_ACTION_POLICY_SHUTDOWN && prefs->priv->can_shutdown) {
			ctk_combo_box_text_append_text(CTK_COMBO_BOX_TEXT (widget), _("Shutdown"));
			g_ptr_array_add(array, GINT_TO_POINTER (policy));
		} else if (policy == CPM_ACTION_POLICY_SUSPEND && !prefs->priv->can_suspend) {
			egg_debug ("Cannot add option, as cannot suspend.");
		} else if (policy == CPM_ACTION_POLICY_HIBERNATE && !prefs->priv->can_hibernate) {
			egg_debug ("Cannot add option, as cannot hibernate.");
		} else if (policy == CPM_ACTION_POLICY_SUSPEND && prefs->priv->can_suspend) {
			ctk_combo_box_text_append_text(CTK_COMBO_BOX_TEXT (widget), _("Suspend"));
			g_ptr_array_add (array, GINT_TO_POINTER (policy));
		} else if (policy == CPM_ACTION_POLICY_HIBERNATE && prefs->priv->can_hibernate) {
			ctk_combo_box_text_append_text(CTK_COMBO_BOX_TEXT (widget), _("Hibernate"));
			g_ptr_array_add(array, GINT_TO_POINTER (policy));
		} else if (policy == CPM_ACTION_POLICY_BLANK) {
			ctk_combo_box_text_append_text(CTK_COMBO_BOX_TEXT (widget), _("Blank screen"));
			g_ptr_array_add (array, GINT_TO_POINTER (policy));
		} else if (policy == CPM_ACTION_POLICY_INTERACTIVE) {
			ctk_combo_box_text_append_text(CTK_COMBO_BOX_TEXT (widget), _("Ask me"));
			g_ptr_array_add(array, GINT_TO_POINTER (policy));
		} else if (policy == CPM_ACTION_POLICY_NOTHING) {
			ctk_combo_box_text_append_text(CTK_COMBO_BOX_TEXT (widget), _("Do nothing"));
			g_ptr_array_add(array, GINT_TO_POINTER (policy));
		} else {
			egg_warning ("Unknown action read from settings: %i", policy);
		}
	}

	/* save as array _only_ the actions we could add */
	actions_added = g_new0 (CpmActionPolicy, array->len+1);
	for (i=0; i<array->len; i++)
		actions_added[i] = GPOINTER_TO_INT (g_ptr_array_index (array, i));
	actions_added[i] = -1;

	g_object_set_data_full (G_OBJECT (widget), "actions", (gpointer) actions_added, (GDestroyNotify) cpm_prefs_actions_destroy_cb);

	/* set what we have in the settings */
	for (i=0; actions_added[i] != -1; i++) {
		policy = actions_added[i];
		if (value == policy)
			 ctk_combo_box_set_active (CTK_COMBO_BOX (widget), i);
	}

	g_ptr_array_unref (array);
}

/**
 * cpm_prefs_setup_time_combo:
 * @prefs: This prefs class instance
 * @widget_name: The CtkWidget name
 * @cpm_pref_key: The settings key for this preference setting.
 * @actions: The actions to associate in an array.
 **/
static void
cpm_prefs_setup_time_combo (CpmPrefs *prefs, const gchar *widget_name,
				const gchar *cpm_pref_key, const gint *values)
{
	guint value;
	gchar *text;
	guint i;
	gboolean is_writable;
	CtkWidget *widget;

	widget = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder, widget_name));

	value = g_settings_get_int (prefs->priv->settings, cpm_pref_key);
	is_writable = g_settings_is_writable (prefs->priv->settings, cpm_pref_key);
	ctk_widget_set_sensitive (widget, is_writable);

	g_object_set_data (G_OBJECT (widget), "settings_key", (gpointer) cpm_pref_key);
	g_object_set_data (G_OBJECT (widget), "values", (gpointer) values);

	/* add each time */
	for (i=0; values[i] != -1; i++) {

		/* get translation for number of seconds */
		if (values[i] != 0) {
			text = cpm_get_timestring (values[i]);
			ctk_combo_box_text_append_text(CTK_COMBO_BOX_TEXT (widget), text);
			g_free (text);
		} else {
			ctk_combo_box_text_append_text(CTK_COMBO_BOX_TEXT (widget), _("Never"));
		}

		/* matches, so set default */
		if (value == values[i])
			 ctk_combo_box_set_active (CTK_COMBO_BOX (widget), i);
	}

	/* connect after set */
	g_signal_connect (G_OBJECT (widget), "changed",
			  G_CALLBACK (cpm_prefs_action_time_changed_cb), prefs);
}

/**
 * cpm_prefs_close_cb:
 * @widget: The CtkWidget object
 * @prefs: This prefs class instance
 **/
static void
cpm_prefs_close_cb (CtkWidget *widget G_GNUC_UNUSED,
		    CpmPrefs  *prefs)
{
	egg_debug ("emitting action-close");
	g_signal_emit (prefs, signals [ACTION_CLOSE], 0);
}

/**
 * cpm_prefs_delete_event_cb:
 * @widget: The CtkWidget object
 * @event: The event type, unused.
 * @prefs: This prefs class instance
 **/
static gboolean
cpm_prefs_delete_event_cb (CtkWidget *widget,
			   CdkEvent  *event G_GNUC_UNUSED,
			   CpmPrefs  *prefs)
{
	cpm_prefs_close_cb (widget, prefs);
	return FALSE;
}

/** setup the notification page */
static void
prefs_setup_notification (CpmPrefs *prefs)
{
	gint icon_policy;
	CtkWidget *radiobutton_icon_always;
	CtkWidget *radiobutton_icon_present;
	CtkWidget *radiobutton_icon_charge;
	CtkWidget *radiobutton_icon_low;
	CtkWidget *radiobutton_icon_never;
	gboolean is_writable;

	icon_policy = g_settings_get_enum (prefs->priv->settings, CPM_SETTINGS_ICON_POLICY);

	radiobutton_icon_always = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder,
						  "radiobutton_notification_always"));
	radiobutton_icon_present = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder,
						   "radiobutton_notification_present"));
	radiobutton_icon_charge = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder,
						  "radiobutton_notification_charge"));
	radiobutton_icon_low = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder,
					   "radiobutton_notification_low"));
	radiobutton_icon_never = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder,
						 "radiobutton_notification_never"));

	is_writable = g_settings_is_writable (prefs->priv->settings, CPM_SETTINGS_ICON_POLICY);
	ctk_widget_set_sensitive (radiobutton_icon_always, is_writable);
	ctk_widget_set_sensitive (radiobutton_icon_present, is_writable);
	ctk_widget_set_sensitive (radiobutton_icon_charge, is_writable);
	ctk_widget_set_sensitive (radiobutton_icon_low, is_writable);
	ctk_widget_set_sensitive (radiobutton_icon_never, is_writable);

	ctk_toggle_button_set_active (CTK_TOGGLE_BUTTON (radiobutton_icon_always),
					  icon_policy == CPM_ICON_POLICY_ALWAYS);
	ctk_toggle_button_set_active (CTK_TOGGLE_BUTTON (radiobutton_icon_present),
					  icon_policy == CPM_ICON_POLICY_PRESENT);
	ctk_toggle_button_set_active (CTK_TOGGLE_BUTTON (radiobutton_icon_charge),
					  icon_policy == CPM_ICON_POLICY_CHARGE);
	ctk_toggle_button_set_active (CTK_TOGGLE_BUTTON (radiobutton_icon_low),
					  icon_policy == CPM_ICON_POLICY_LOW);
	ctk_toggle_button_set_active (CTK_TOGGLE_BUTTON (radiobutton_icon_never),
					  icon_policy == CPM_ICON_POLICY_NEVER);

	g_object_set_data (G_OBJECT (radiobutton_icon_always), "policy",
			   GINT_TO_POINTER (CPM_ICON_POLICY_ALWAYS));
	g_object_set_data (G_OBJECT (radiobutton_icon_present), "policy",
			   GINT_TO_POINTER (CPM_ICON_POLICY_PRESENT));
	g_object_set_data (G_OBJECT (radiobutton_icon_charge), "policy",
			   GINT_TO_POINTER (CPM_ICON_POLICY_CHARGE));
	g_object_set_data (G_OBJECT (radiobutton_icon_low), "policy",
			   GINT_TO_POINTER (CPM_ICON_POLICY_LOW));
	g_object_set_data (G_OBJECT (radiobutton_icon_never), "policy",
			   GINT_TO_POINTER (CPM_ICON_POLICY_NEVER));

	/* only connect the callbacks after we set the value, else the settings
	 * keys gets written to (for a split second), and the icon flickers. */
	g_signal_connect (radiobutton_icon_always, "clicked",
			  G_CALLBACK (cpm_prefs_icon_radio_cb), prefs);
	g_signal_connect (radiobutton_icon_present, "clicked",
			  G_CALLBACK (cpm_prefs_icon_radio_cb), prefs);
	g_signal_connect (radiobutton_icon_charge, "clicked",
			  G_CALLBACK (cpm_prefs_icon_radio_cb), prefs);
	g_signal_connect (radiobutton_icon_low, "clicked",
			  G_CALLBACK (cpm_prefs_icon_radio_cb), prefs);
	g_signal_connect (radiobutton_icon_never, "clicked",
			  G_CALLBACK (cpm_prefs_icon_radio_cb), prefs);
}

static void
prefs_setup_ac (CpmPrefs *prefs)
{
	CtkWidget *widget;
	const CpmActionPolicy button_lid_actions[] =
				{CPM_ACTION_POLICY_NOTHING,
				 CPM_ACTION_POLICY_BLANK,
				 CPM_ACTION_POLICY_SUSPEND,
				 CPM_ACTION_POLICY_HIBERNATE,
				 CPM_ACTION_POLICY_SHUTDOWN,
				 -1};

	static const gint computer_times[] =
		{10*60,
		 30*60,
		 1*60*60,
		 2*60*60,
		 0, /* never */
		 -1};
	static const gint display_times[] =
		{1*60,
		 5*60,
		 10*60,
		 30*60,
		 1*60*60,
		 0, /* never */
		 -1};

	cpm_prefs_setup_time_combo (prefs, "combobox_ac_computer",
					CPM_SETTINGS_SLEEP_COMPUTER_AC,
					computer_times);
	cpm_prefs_setup_time_combo (prefs, "combobox_ac_display",
					CPM_SETTINGS_SLEEP_DISPLAY_AC,
					display_times);

	cpm_prefs_setup_action_combo (prefs, "combobox_ac_lid",
					  CPM_SETTINGS_BUTTON_LID_AC,
					  button_lid_actions);

	/* setup brightness slider */
	widget = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder, "hscale_ac_brightness"));
	g_settings_bind (prefs->priv->settings, CPM_SETTINGS_BRIGHTNESS_AC,
			 ctk_range_get_adjustment (CTK_RANGE (widget)), "value",
			 G_SETTINGS_BIND_DEFAULT);
	g_signal_connect (G_OBJECT (widget), "format-value",
			  G_CALLBACK (cpm_prefs_format_percentage_cb), NULL);

	/* set up the checkboxes */
	widget = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder, "checkbutton_ac_display_dim"));
	g_settings_bind (prefs->priv->settings, CPM_SETTINGS_IDLE_DIM_AC,
			 widget, "active",
			 G_SETTINGS_BIND_DEFAULT);

	if (prefs->priv->has_button_lid == FALSE) {
		widget = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder, "hbox_ac_lid"));
		ctk_widget_hide(widget);
	}
	if (prefs->priv->has_lcd == FALSE) {
		widget = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder, "hbox_ac_brightness"));

		ctk_widget_hide(widget);

		widget = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder, "checkbutton_ac_display_dim"));

		ctk_widget_hide(widget);
	}
}

static void
prefs_setup_battery (CpmPrefs *prefs)
{
	CtkWidget *widget;
	CtkNotebook *notebook;
	gint page;

	const CpmActionPolicy button_lid_actions[] =
				{CPM_ACTION_POLICY_NOTHING,
				 CPM_ACTION_POLICY_BLANK,
				 CPM_ACTION_POLICY_SUSPEND,
				 CPM_ACTION_POLICY_HIBERNATE,
				 CPM_ACTION_POLICY_SHUTDOWN,
				 -1};
	const CpmActionPolicy battery_critical_actions[] =
				{CPM_ACTION_POLICY_NOTHING,
				 CPM_ACTION_POLICY_SUSPEND,
				 CPM_ACTION_POLICY_HIBERNATE,
				 CPM_ACTION_POLICY_SHUTDOWN,
				 -1};

	static const gint computer_times[] =
		{10*60,
		 30*60,
		 1*60*60,
		 2*60*60,
		 0, /* never */
		 -1};
	static const gint display_times[] =
		{1*60,
		 5*60,
		 10*60,
		 30*60,
		 1*60*60,
		 0, /* never */
		 -1};

	cpm_prefs_setup_time_combo (prefs, "combobox_battery_computer",
					CPM_SETTINGS_SLEEP_COMPUTER_BATT,
					computer_times);
	cpm_prefs_setup_time_combo (prefs, "combobox_battery_display",
					CPM_SETTINGS_SLEEP_DISPLAY_BATT,
					display_times);

	if (prefs->priv->has_batteries == FALSE) {
		notebook = CTK_NOTEBOOK (ctk_builder_get_object (prefs->priv->builder, "notebook_preferences"));
		widget = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder, "vbox_battery"));
		page = ctk_notebook_page_num (notebook, CTK_WIDGET (widget));
		ctk_notebook_remove_page (notebook, page);
		return;
	}

	cpm_prefs_setup_action_combo (prefs, "combobox_battery_lid",
					  CPM_SETTINGS_BUTTON_LID_BATT,
					  button_lid_actions);
	cpm_prefs_setup_action_combo (prefs, "combobox_battery_critical",
					  CPM_SETTINGS_ACTION_CRITICAL_BATT,
					  battery_critical_actions);

	/* set up the checkboxes */
	widget = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder, "checkbutton_battery_display_reduce"));
	g_settings_bind (prefs->priv->settings, CPM_SETTINGS_BACKLIGHT_BATTERY_REDUCE,
			 widget, "active",
			 G_SETTINGS_BIND_DEFAULT);
	widget = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder, "checkbutton_battery_display_dim"));
	g_settings_bind (prefs->priv->settings, CPM_SETTINGS_IDLE_DIM_BATT,
			 widget, "active",
			 G_SETTINGS_BIND_DEFAULT);

	if (prefs->priv->has_button_lid == FALSE) {
		widget = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder, "hbox_battery_lid"));

		ctk_widget_hide(widget);
	}
	if (prefs->priv->has_lcd == FALSE) {
		widget = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder, "checkbutton_battery_display_dim"));
		ctk_widget_hide(widget);
	}
}

static void
prefs_setup_ups (CpmPrefs *prefs)
{
	CtkWidget *widget;
	CtkWidget *notebook;
	CtkWidget *window;
	gint page;

	const CpmActionPolicy ups_low_actions[] =
				{CPM_ACTION_POLICY_NOTHING,
				 CPM_ACTION_POLICY_HIBERNATE,
				 CPM_ACTION_POLICY_SHUTDOWN,
				 -1};

	static const gint computer_times[] =
		{10*60,
		 30*60,
		 1*60*60,
		 2*60*60,
		 0, /* never */
		 -1};
	static const gint display_times[] =
		{1*60,
		 5*60,
		 10*60,
		 30*60,
		 1*60*60,
		 0, /* never */
		 -1};

	cpm_prefs_setup_time_combo (prefs, "combobox_ups_computer",
					CPM_SETTINGS_SLEEP_COMPUTER_UPS,
					computer_times);
	cpm_prefs_setup_time_combo (prefs, "combobox_ups_display",
					CPM_SETTINGS_SLEEP_DISPLAY_UPS,
					display_times);

	window = cpm_window (prefs);
	notebook = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder, "notebook_preferences"));
	ctk_widget_add_events (notebook, CDK_SCROLL_MASK);
	g_signal_connect (CTK_NOTEBOOK (notebook), "scroll-event",
			  G_CALLBACK (cpm_dialog_page_scroll_event_cb),
			  window);

	if (prefs->priv->has_ups == FALSE) {
		widget = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder, "vbox_ups"));
		page = ctk_notebook_page_num (CTK_NOTEBOOK (notebook), CTK_WIDGET (widget));
		ctk_notebook_remove_page (CTK_NOTEBOOK (notebook), page);
		return;
	}

	cpm_prefs_setup_action_combo (prefs, "combobox_ups_low",
					  CPM_SETTINGS_ACTION_LOW_UPS,
					  ups_low_actions);
	cpm_prefs_setup_action_combo (prefs, "combobox_ups_critical",
					  CPM_SETTINGS_ACTION_CRITICAL_UPS,
					  ups_low_actions);
}

static void
prefs_setup_general (CpmPrefs *prefs)
{
	CtkWidget *widget;
	const CpmActionPolicy power_button_actions[] =
				{CPM_ACTION_POLICY_INTERACTIVE,
				 CPM_ACTION_POLICY_SUSPEND,
				 CPM_ACTION_POLICY_HIBERNATE,
				 CPM_ACTION_POLICY_SHUTDOWN,
				 CPM_ACTION_POLICY_NOTHING,
				 -1};
	const CpmActionPolicy suspend_button_actions[] =
				{CPM_ACTION_POLICY_NOTHING,
				 CPM_ACTION_POLICY_SUSPEND,
				 CPM_ACTION_POLICY_HIBERNATE,
				 -1};

	cpm_prefs_setup_action_combo (prefs, "combobox_general_power",
					  CPM_SETTINGS_BUTTON_POWER,
					  power_button_actions);
	cpm_prefs_setup_action_combo (prefs, "combobox_general_suspend",
					  CPM_SETTINGS_BUTTON_SUSPEND,
					  suspend_button_actions);

	if (prefs->priv->has_button_suspend == FALSE) {
		widget = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder, "hbox_general_suspend"));

		ctk_widget_hide(widget);
	}
}

/**
 * cpm_prefs_init:
 * @prefs: This prefs class instance
 **/
static void
cpm_prefs_init (CpmPrefs *prefs)
{
	CtkWidget *main_window;
	CtkWidget *widget;
	guint retval;
	GError *error = NULL;
	GPtrArray *devices = NULL;
	UpDevice *device;
	UpDeviceKind kind;
	CpmBrightness *brightness;
	guint i;

	GDBusProxy *proxy;
	GVariant *res, *inner;
	gchar * r;

	prefs->priv = cpm_prefs_get_instance_private (prefs);

	prefs->priv->client = up_client_new ();
	prefs->priv->console = egg_console_kit_new ();
	prefs->priv->settings = g_settings_new (CPM_SETTINGS_SCHEMA);

	prefs->priv->can_shutdown = FALSE;
	prefs->priv->can_suspend = FALSE;
	prefs->priv->can_hibernate = FALSE;

	if (LOGIND_RUNNING()) {
		/* get values from logind */

		proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
						       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
						       NULL,
						       "org.freedesktop.login1",
						       "/org/freedesktop/login1",
						       "org.freedesktop.login1.Manager",
						       NULL,
						       &error );
		if (proxy == NULL) {
			egg_error("Error connecting to dbus - %s", error->message);
			g_error_free (error);
			return;
		}

		res = g_dbus_proxy_call_sync (proxy, "CanPowerOff",
					      NULL,
					      G_DBUS_CALL_FLAGS_NONE,
					      -1,
					      NULL,
					      &error
					      );
		if (error == NULL && res != NULL) {
			g_variant_get(res,"(&s)", &r);
			prefs->priv->can_shutdown = g_strcmp0(r,"yes")==0?TRUE:FALSE;
			g_variant_unref (res);
		} else if (error != NULL ) {
			egg_error ("Error in dbus - %s", error->message);
			g_error_free (error);
		}

		res = g_dbus_proxy_call_sync (proxy, "CanSuspend",
					      NULL,
					      G_DBUS_CALL_FLAGS_NONE,
					      -1,
					      NULL,
					      &error
					      );
		if (error == NULL && res != NULL) {
			g_variant_get(res,"(&s)", &r);
			prefs->priv->can_suspend = g_strcmp0(r,"yes")==0?TRUE:FALSE;
			g_variant_unref (res);
		} else if (error != NULL ) {
			egg_error ("Error in dbus - %s", error->message);
			g_error_free (error);
		}

		res = g_dbus_proxy_call_sync (proxy, "CanHibernate",
					      NULL,
					      G_DBUS_CALL_FLAGS_NONE,
					      -1,
					      NULL,
					      &error
					      );
		if (error == NULL && res != NULL) {
			g_variant_get(res,"(&s)", &r);
			prefs->priv->can_hibernate = g_strcmp0(r,"yes")==0?TRUE:FALSE;
			g_variant_unref (res);
		} else if (error != NULL ) {
			egg_error ("Error in dbus - %s", error->message);
			g_error_free (error);
		}
		g_object_unref(proxy);
	}
	else {
		/* Get values from ConsoleKit */
		egg_console_kit_can_stop (prefs->priv->console, &prefs->priv->can_shutdown, NULL);
		egg_console_kit_can_suspend (prefs->priv->console, &prefs->priv->can_suspend, NULL);
		egg_console_kit_can_hibernate (prefs->priv->console, &prefs->priv->can_hibernate, NULL);
	}

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
			return;
		}

		res = g_dbus_proxy_call_sync (proxy, "Get",
					      g_variant_new( "(ss)",
							     "org.freedesktop.UPower",
							     "LidIsPresent"),
					      G_DBUS_CALL_FLAGS_NONE,
					      -1,
					      NULL,
					      &error
					      );
		if (error == NULL && res != NULL) {
			g_variant_get(res, "(v)", &inner );
			prefs->priv->has_button_lid = g_variant_get_boolean(inner);
			g_variant_unref (inner);
			g_variant_unref (res);
		} else if (error != NULL ) {
			egg_error ("Error in dbus - %s", error->message);
			g_error_free (error);
		}
		g_object_unref(proxy);
	}
	else {
		prefs->priv->has_button_lid = up_client_get_lid_is_present (prefs->priv->client);
	}

	prefs->priv->has_button_suspend = TRUE;

	/* find if we have brightness hardware */
	brightness = cpm_brightness_new ();
	prefs->priv->has_lcd = cpm_brightness_has_hw (brightness);
	g_object_unref (brightness);
	devices = up_client_get_devices2 (prefs->priv->client);
	for (i=0; i<devices->len; i++) {
		device = g_ptr_array_index (devices, i);
		g_object_get (device,
			      "kind", &kind,
			      NULL);
		if (kind == UP_DEVICE_KIND_BATTERY)
			prefs->priv->has_batteries = TRUE;
		if (kind == UP_DEVICE_KIND_UPS)
			prefs->priv->has_ups = TRUE;
	}
	g_ptr_array_unref (devices);

	error = NULL;
	prefs->priv->builder = ctk_builder_new ();
	retval = ctk_builder_add_from_resource (prefs->priv->builder, "/org/cafe/powermanager/preferences/cpm-prefs.ui", &error);

	if (error) {
		egg_error ("failed to load ui: %s", error->message);
	}

	main_window = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder, "dialog_preferences"));

	/* Hide window first so that the dialogue resizes itself without redrawing */
	ctk_widget_hide (main_window);
	ctk_window_set_default_icon_name (CPM_ICON_APP_ICON);

	/* Get the main window quit */
	g_signal_connect (main_window, "delete_event",
			  G_CALLBACK (cpm_prefs_delete_event_cb), prefs);

	widget = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder, "button_close"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (cpm_prefs_close_cb), prefs);

	widget = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder, "button_help"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (cpm_prefs_help_cb), prefs);

	widget = CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder, "button_defaults"));
	ctk_widget_hide (widget);

	prefs_setup_ac (prefs);
	prefs_setup_battery (prefs);
	prefs_setup_ups (prefs);
	prefs_setup_general (prefs);
	prefs_setup_notification (prefs);
}

/**
 * cpm_prefs_finalize:
 * @object: This prefs class instance
 **/
static void
cpm_prefs_finalize (GObject *object)
{
	CpmPrefs *prefs;
	g_return_if_fail (object != NULL);
	g_return_if_fail (CPM_IS_PREFS (object));

	prefs = CPM_PREFS (object);
	prefs->priv = cpm_prefs_get_instance_private (prefs);

	g_object_unref (prefs->priv->settings);
	g_object_unref (prefs->priv->client);
	g_object_unref (prefs->priv->console);
	g_object_unref (prefs->priv->builder);

	G_OBJECT_CLASS (cpm_prefs_parent_class)->finalize (object);
}

/**
 * cpm_prefs_new:
 * Return value: new CpmPrefs instance.
 **/
CpmPrefs *
cpm_prefs_new (void)
{
	CpmPrefs *prefs;
	prefs = g_object_new (CPM_TYPE_PREFS, NULL);
	return CPM_PREFS (prefs);
}

/**
 * cpm_window:
 * Return value: Prefs window widget.
 **/
CtkWidget *
cpm_window (CpmPrefs *prefs)
{
	return CTK_WIDGET (ctk_builder_get_object (prefs->priv->builder, "dialog_preferences"));
}
