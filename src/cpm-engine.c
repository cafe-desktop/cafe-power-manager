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
#include <libupower-glib/upower.h>

#include "egg-debug.h"

#include "cpm-common.h"
#include "cpm-upower.h"
#include "cpm-marshal.h"
#include "cpm-engine.h"
#include "cpm-icon-names.h"
#include "cpm-phone.h"

static void     cpm_engine_finalize   (GObject	  *object);

#define CPM_ENGINE_RESUME_DELAY		2*1000
#define CPM_ENGINE_WARN_ACCURACY	20

struct CpmEnginePrivate
{
	GSettings		*settings;
	UpClient		*client;
	UpDevice		*battery_composite;
	GPtrArray		*array;
	CpmPhone		*phone;
	CpmIconPolicy		 icon_policy;
	gchar			*previous_icon;
	gchar			*previous_summary;

	gboolean		 use_time_primary;
	gboolean		 time_is_accurate;

	guint			 low_percentage;
	guint			 critical_percentage;
	guint			 action_percentage;
	guint			 low_time;
	guint			 critical_time;
	guint			 action_time;
};

enum {
	ICON_CHANGED,
	SUMMARY_CHANGED,
	FULLY_CHARGED,
	CHARGE_LOW,
	CHARGE_CRITICAL,
	CHARGE_ACTION,
	DISCHARGING,
	LOW_CAPACITY,
	DEVICES_CHANGED,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };
static gpointer cpm_engine_object = NULL;

G_DEFINE_TYPE_WITH_PRIVATE (CpmEngine, cpm_engine, G_TYPE_OBJECT)

static UpDevice *cpm_engine_get_composite_device (CpmEngine *engine, UpDevice *original_device);
static UpDevice *cpm_engine_update_composite_device (CpmEngine *engine, UpDevice *original_device);
static void cpm_engine_device_changed_cb (UpDevice *device, GParamSpec *pspec, CpmEngine *engine);

#define CPM_ENGINE_WARNING_NONE UP_DEVICE_LEVEL_NONE
#define CPM_ENGINE_WARNING_DISCHARGING UP_DEVICE_LEVEL_DISCHARGING
#define CPM_ENGINE_WARNING_LOW UP_DEVICE_LEVEL_LOW
#define CPM_ENGINE_WARNING_CRITICAL UP_DEVICE_LEVEL_CRITICAL
#define CPM_ENGINE_WARNING_ACTION UP_DEVICE_LEVEL_ACTION

/**
 * cpm_engine_get_warning:
 *
 * This gets the possible engine state for the device according to the
 * policy, which could be per-percent, or per-time.
 *
 * Return value: A CpmEngine state, e.g. CPM_ENGINE_WARNING_DISCHARGING
 **/
static UpDeviceLevel
cpm_engine_get_warning (CpmEngine *engine G_GNUC_UNUSED,
			UpDevice  *device)
{
	UpDeviceLevel warning;
	g_object_get (device, "warning-level", &warning, NULL);
	return warning;
}

/**
 * cpm_engine_get_summary:
 * @engine: This engine class instance
 * @string: The returned string
 *
 * Returns the complete tooltip ready for display
 **/
gchar *
cpm_engine_get_summary (CpmEngine *engine)
{
	guint i;
	GPtrArray *array;
	UpDevice *device;
	UpDeviceState state;
	GString *tooltip = NULL;
	gchar *part;
	gboolean is_present;

	g_return_val_if_fail (CPM_IS_ENGINE (engine), NULL);

	/* need to get AC state */
	tooltip = g_string_new ("");

	/* do we have specific device types? */
	array = engine->priv->array;
	for (i=0;i<array->len;i++) {
		device = g_ptr_array_index (engine->priv->array, i);
		g_object_get (device,
			      "is-present", &is_present,
			      "state", &state,
			      NULL);
		if (!is_present)
			continue;
		if (state == UP_DEVICE_STATE_EMPTY)
			continue;
		part = cpm_upower_get_device_summary (device);
		if (part != NULL)
			g_string_append_printf (tooltip, "%s\n", part);
		g_free (part);
	}

	/* remove the last \n */
	g_string_truncate (tooltip, tooltip->len-1);

	egg_debug ("tooltip: %s", tooltip->str);

	return g_string_free (tooltip, FALSE);
}

/**
 * cpm_engine_get_icon_priv:
 *
 * Returns the icon
 **/
static gchar *
cpm_engine_get_icon_priv (CpmEngine *engine, UpDeviceKind device_kind, UpDeviceLevel warning, gboolean use_state)
{
	guint i;
	GPtrArray *array;
	UpDevice *device;
	UpDeviceLevel warning_temp;
	UpDeviceKind kind;
	UpDeviceState state;
	gboolean is_present;

	/* do we have specific device types? */
	array = engine->priv->array;
	for (i=0;i<array->len;i++) {
		device = g_ptr_array_index (engine->priv->array, i);

		/* get device properties */
		g_object_get (device,
			      "kind", &kind,
			      "state", &state,
			      "is-present", &is_present,
			      NULL);

		/* if battery then use composite device to cope with multiple batteries */
		if (kind == UP_DEVICE_KIND_BATTERY)
			device = cpm_engine_get_composite_device (engine, device);

		warning_temp = GPOINTER_TO_INT(g_object_get_data (G_OBJECT(device), "engine-warning-old"));
		if (kind == device_kind && is_present) {
			if (warning != CPM_ENGINE_WARNING_NONE) {
				if (warning_temp == warning)
					return cpm_upower_get_device_icon (device);
				continue;
			}
			if (use_state) {
				if (state == UP_DEVICE_STATE_CHARGING || state == UP_DEVICE_STATE_DISCHARGING)
					return cpm_upower_get_device_icon (device);
				continue;
			}
			return cpm_upower_get_device_icon (device);
		}
	}
	return NULL;
}

/**
 * cpm_engine_get_icon:
 *
 * Returns the icon
 **/
gchar *
cpm_engine_get_icon (CpmEngine *engine)
{
	gchar *icon = NULL;

	g_return_val_if_fail (CPM_IS_ENGINE (engine), NULL);

	/* policy */
	if (engine->priv->icon_policy == CPM_ICON_POLICY_NEVER) {
		egg_debug ("no icon allowed, so no icon will be displayed.");
		return NULL;
	}

	/* we try CRITICAL: BATTERY, UPS, MOUSE, KEYBOARD */
	icon = cpm_engine_get_icon_priv (engine, UP_DEVICE_KIND_BATTERY, CPM_ENGINE_WARNING_CRITICAL, FALSE);
	if (icon != NULL)
		return icon;
	icon = cpm_engine_get_icon_priv (engine, UP_DEVICE_KIND_UPS, CPM_ENGINE_WARNING_CRITICAL, FALSE);
	if (icon != NULL)
		return icon;
	icon = cpm_engine_get_icon_priv (engine, UP_DEVICE_KIND_MOUSE, CPM_ENGINE_WARNING_CRITICAL, FALSE);
	if (icon != NULL)
		return icon;
	icon = cpm_engine_get_icon_priv (engine, UP_DEVICE_KIND_KEYBOARD, CPM_ENGINE_WARNING_CRITICAL, FALSE);
	if (icon != NULL)
		return icon;

	/* policy */
	if (engine->priv->icon_policy == CPM_ICON_POLICY_CRITICAL) {
		egg_debug ("no devices critical, so no icon will be displayed.");
		return NULL;
	}

	/* we try CRITICAL: BATTERY, UPS, MOUSE, KEYBOARD */
	icon = cpm_engine_get_icon_priv (engine, UP_DEVICE_KIND_BATTERY, CPM_ENGINE_WARNING_LOW, FALSE);
	if (icon != NULL)
		return icon;
	icon = cpm_engine_get_icon_priv (engine, UP_DEVICE_KIND_UPS, CPM_ENGINE_WARNING_LOW, FALSE);
	if (icon != NULL)
		return icon;
	icon = cpm_engine_get_icon_priv (engine, UP_DEVICE_KIND_MOUSE, CPM_ENGINE_WARNING_LOW, FALSE);
	if (icon != NULL)
		return icon;
	icon = cpm_engine_get_icon_priv (engine, UP_DEVICE_KIND_KEYBOARD, CPM_ENGINE_WARNING_LOW, FALSE);
	if (icon != NULL)
		return icon;

	/* policy */
	if (engine->priv->icon_policy == CPM_ICON_POLICY_LOW) {
		egg_debug ("no devices low, so no icon will be displayed.");
		return NULL;
	}

	/* we try (DIS)CHARGING: BATTERY, UPS */
	icon = cpm_engine_get_icon_priv (engine, UP_DEVICE_KIND_BATTERY, CPM_ENGINE_WARNING_NONE, TRUE);
	if (icon != NULL)
		return icon;
	icon = cpm_engine_get_icon_priv (engine, UP_DEVICE_KIND_UPS, CPM_ENGINE_WARNING_NONE, TRUE);
	if (icon != NULL)
		return icon;

	/* policy */
	if (engine->priv->icon_policy == CPM_ICON_POLICY_CHARGE) {
		egg_debug ("no devices (dis)charging, so no icon will be displayed.");
		return NULL;
	}

	/* we try PRESENT: BATTERY, UPS */
	icon = cpm_engine_get_icon_priv (engine, UP_DEVICE_KIND_BATTERY, CPM_ENGINE_WARNING_NONE, FALSE);
	if (icon != NULL)
		return icon;
	icon = cpm_engine_get_icon_priv (engine, UP_DEVICE_KIND_UPS, CPM_ENGINE_WARNING_NONE, FALSE);
	if (icon != NULL)
		return icon;

	/* policy */
	if (engine->priv->icon_policy == CPM_ICON_POLICY_PRESENT) {
		egg_debug ("no devices present, so no icon will be displayed.");
		return NULL;
	}

	/* we fallback to the ac_adapter icon */
	egg_debug ("Using fallback");
	return g_strdup (CPM_ICON_AC_ADAPTER);
}

/**
 * cpm_engine_recalculate_state_icon:
 */
static gboolean
cpm_engine_recalculate_state_icon (CpmEngine *engine)
{
	gchar *icon;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (CPM_IS_ENGINE (engine), FALSE);

	/* show a different icon if we are disconnected */
	icon = cpm_engine_get_icon (engine);
	if (icon == NULL) {
		/* none before, now none */
		if (engine->priv->previous_icon == NULL)
			return FALSE;
		/* icon before, now none */
		egg_debug ("** EMIT: icon-changed: none");
		g_signal_emit (engine, signals [ICON_CHANGED], 0, NULL);

		g_free (engine->priv->previous_icon);
		engine->priv->previous_icon = NULL;
		return TRUE;
	}

	/* no icon before, now icon */
	if (engine->priv->previous_icon == NULL) {
		egg_debug ("** EMIT: icon-changed: %s", icon);
		g_signal_emit (engine, signals [ICON_CHANGED], 0, icon);
		engine->priv->previous_icon = icon;
		return TRUE;
	}

	/* icon before, now different */
	if (strcmp (engine->priv->previous_icon, icon) != 0) {
		g_free (engine->priv->previous_icon);
		engine->priv->previous_icon = icon;
		egg_debug ("** EMIT: icon-changed: %s", icon);
		g_signal_emit (engine, signals [ICON_CHANGED], 0, icon);
		return TRUE;
	}

	egg_debug ("no change");
	/* nothing to do */
	g_free (icon);
	return FALSE;
}

/**
 * cpm_engine_recalculate_state_summary:
 */
static gboolean
cpm_engine_recalculate_state_summary (CpmEngine *engine)
{
	gchar *summary;

	summary = cpm_engine_get_summary (engine);
	if (engine->priv->previous_summary == NULL) {
		engine->priv->previous_summary = summary;
		egg_debug ("** EMIT: summary-changed(1): %s", summary);
		g_signal_emit (engine, signals [SUMMARY_CHANGED], 0, summary);
		return TRUE;
	}	

	if (strcmp (engine->priv->previous_summary, summary) != 0) {
		g_free (engine->priv->previous_summary);
		engine->priv->previous_summary = summary;
		egg_debug ("** EMIT: summary-changed(2): %s", summary);
		g_signal_emit (engine, signals [SUMMARY_CHANGED], 0, summary);
		return TRUE;
	}
	egg_debug ("no change");
	/* nothing to do */
	g_free (summary);
	return FALSE;
}

/**
 * cpm_engine_recalculate_state:
 */
static void
cpm_engine_recalculate_state (CpmEngine *engine)
{

	g_return_if_fail (engine != NULL);
	g_return_if_fail (CPM_IS_ENGINE (engine));

	cpm_engine_recalculate_state_icon (engine);
	cpm_engine_recalculate_state_summary (engine);

	g_signal_emit (engine, signals [DEVICES_CHANGED], 0);
}

/**
 * cpm_engine_settings_key_changed_cb:
 **/
static void
cpm_engine_settings_key_changed_cb (GSettings *settings, const gchar *key, CpmEngine *engine)
{

	if (g_strcmp0 (key, CPM_SETTINGS_USE_TIME_POLICY) == 0) {
		engine->priv->use_time_primary = g_settings_get_boolean (settings, key);

	} else if (g_strcmp0 (key, CPM_SETTINGS_ICON_POLICY) == 0) {

		/* do we want to display the icon in the tray */
		engine->priv->icon_policy = g_settings_get_enum (settings, key);

		/* perhaps change icon */
		cpm_engine_recalculate_state_icon (engine);
	}
}

/**
 * cpm_engine_device_check_capacity:
 **/
static gboolean
cpm_engine_device_check_capacity (CpmEngine *engine, UpDevice *device)
{
	gboolean ret;
	UpDeviceKind kind;
	gdouble capacity;

	/* get device properties */
	g_object_get (device,
		      "kind", &kind,
		      "capacity", &capacity,
		      NULL);

	/* not laptop battery */
	if (kind != UP_DEVICE_KIND_BATTERY)
		return FALSE;

	/* capacity okay */
	if (capacity > 50.0f)
		return FALSE;

	/* capacity invalid */
	if (capacity < 1.0f)
		return FALSE;

	/* only emit this if specified in the settings */
	ret = g_settings_get_boolean (engine->priv->settings, CPM_SETTINGS_NOTIFY_LOW_CAPACITY);
	if (ret) {
		egg_debug ("** EMIT: low-capacity");
		g_signal_emit (engine, signals [LOW_CAPACITY], 0, device);
	}
	return TRUE;
}

/**
 * cpm_engine_get_composite_device:
 **/
static UpDevice *
cpm_engine_get_composite_device (CpmEngine *engine,
				 UpDevice  *original_device G_GNUC_UNUSED)
{
	return engine->priv->battery_composite;
}

/**
 * cpm_engine_update_composite_device:
 **/
static UpDevice *
cpm_engine_update_composite_device (CpmEngine *engine,
				    UpDevice  *original_device G_GNUC_UNUSED)
{
	gchar *text;

	text = up_device_to_text (engine->priv->battery_composite);
	egg_debug ("composite:\n%s", text);
	g_free (text);

	/* force update of icon */
	cpm_engine_recalculate_state_icon (engine);

	return engine->priv->battery_composite;
}

/**
 * cpm_engine_device_add:
 **/
static void
cpm_engine_device_add (CpmEngine *engine, UpDevice *device)
{
	UpDeviceLevel warning;
	UpDeviceState state;
	UpDeviceKind kind;
	UpDevice *composite;

	/* assign warning */
	warning = cpm_engine_get_warning (engine, device);
	g_object_set_data (G_OBJECT(device), "engine-warning-old", GUINT_TO_POINTER(warning));

	/* check capacity */
	cpm_engine_device_check_capacity (engine, device);

	/* get device properties */
	g_object_get (device,
		      "kind", &kind,
		      "state", &state,
		      NULL);

	/* add old state for transitions */
	egg_debug ("adding %s with state %s", up_device_get_object_path (device), up_device_state_to_string (state));
	g_object_set_data (G_OBJECT(device), "engine-state-old", GUINT_TO_POINTER(state));

	if (kind == UP_DEVICE_KIND_BATTERY) {
		egg_debug ("updating because we added a device");
		composite = cpm_engine_update_composite_device (engine, device);

		/* get the same values for the composite device */
		warning = cpm_engine_get_warning (engine, composite);
		g_object_set_data (G_OBJECT(composite), "engine-warning-old", GUINT_TO_POINTER(warning));
		g_object_get (composite, "state", &state, NULL);
		g_object_set_data (G_OBJECT(composite), "engine-state-old", GUINT_TO_POINTER(state));
	}

	g_signal_connect (device, "notify", G_CALLBACK (cpm_engine_device_changed_cb), engine);
	g_ptr_array_add (engine->priv->array, g_object_ref (device));
	cpm_engine_recalculate_state (engine);
}

/**
 * cpm_engine_coldplug_idle_cb:
 **/
static gboolean
cpm_engine_coldplug_idle_cb (CpmEngine *engine)
{
	guint i;
	GPtrArray *array = NULL;
	UpDevice *device;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (CPM_IS_ENGINE (engine), FALSE);
	/* connected mobile phones */
	cpm_phone_coldplug (engine->priv->phone);

	cpm_engine_recalculate_state (engine);

	/* add to database */
	array = up_client_get_devices2 (engine->priv->client);
	for (i=0;i<array->len;i++) {
		device = g_ptr_array_index (array, i);
		cpm_engine_device_add (engine, device);
	}
	if (array != NULL)
		g_ptr_array_unref (array);
	/* never repeat */
	return FALSE;
}

/**
 * cpm_engine_device_added_cb:
 **/
static void
cpm_engine_device_added_cb (UpClient  *client G_GNUC_UNUSED,
			    UpDevice  *device,
			    CpmEngine *engine)
{
	cpm_engine_device_add (engine, device);
}

/**
 * cpm_engine_device_removed_cb:
 **/
static void
cpm_engine_device_removed_cb (UpClient   *client G_GNUC_UNUSED,
			      const char *object_path,
			      CpmEngine  *engine)
{
	guint i;

	for (i = 0; i < engine->priv->array->len; i++) {
		UpDevice *device = g_ptr_array_index (engine->priv->array, i);

		if (g_strcmp0 (object_path, up_device_get_object_path (device)) == 0) {
			g_ptr_array_remove_index (engine->priv->array, i);
			break;
		}
	}
	cpm_engine_recalculate_state (engine);
}

/**
 * cpm_engine_device_changed_cb:
 **/
static void
cpm_engine_device_changed_cb (UpDevice   *device,
			      GParamSpec *pspec G_GNUC_UNUSED,
			      CpmEngine  *engine)
{
	UpDeviceKind kind;
	UpDeviceState state;
	UpDeviceState state_old;
	UpDeviceLevel warning_old;
	UpDeviceLevel warning;

	/* get device properties */
	g_object_get (device,
		      "kind", &kind,
		      NULL);

	/* if battery then use composite device to cope with multiple batteries */
	if (kind == UP_DEVICE_KIND_BATTERY) {
		egg_debug ("updating because %s changed", up_device_get_object_path (device));
		device = cpm_engine_update_composite_device (engine, device);
	}

	/* get device properties (may be composite) */
	g_object_get (device,
		      "state", &state,
		      NULL);

	egg_debug ("%s state is now %s", up_device_get_object_path (device), up_device_state_to_string (state));

	/* see if any interesting state changes have happened */
	state_old = GPOINTER_TO_INT(g_object_get_data (G_OBJECT(device), "engine-state-old"));
	if (state_old != state) {
		if (state == UP_DEVICE_STATE_DISCHARGING) {
			egg_debug ("** EMIT: discharging");
			g_signal_emit (engine, signals [DISCHARGING], 0, device);
		} else if (state == UP_DEVICE_STATE_FULLY_CHARGED) {
			egg_debug ("** EMIT: fully charged");
			g_signal_emit (engine, signals [FULLY_CHARGED], 0, device);
		}

		/* save new state */
		g_object_set_data (G_OBJECT(device), "engine-state-old", GUINT_TO_POINTER(state));
	}

	/* check the warning state has not changed */
	warning_old = GPOINTER_TO_INT(g_object_get_data (G_OBJECT(device), "engine-warning-old"));
	warning = cpm_engine_get_warning (engine, device);
	if (warning != warning_old) {
		if (warning == CPM_ENGINE_WARNING_LOW) {
			egg_debug ("** EMIT: charge-low");
			g_signal_emit (engine, signals [CHARGE_LOW], 0, device);
		} else if (warning == CPM_ENGINE_WARNING_CRITICAL) {
			egg_debug ("** EMIT: charge-critical");
			g_signal_emit (engine, signals [CHARGE_CRITICAL], 0, device);
		} else if (warning == CPM_ENGINE_WARNING_ACTION) {
			egg_debug ("** EMIT: charge-action");
			g_signal_emit (engine, signals [CHARGE_ACTION], 0, device);
		}
		/* save new state */
		g_object_set_data (G_OBJECT(device), "engine-warning-old", GUINT_TO_POINTER(warning));
	}

	cpm_engine_recalculate_state (engine);
}

/**
 * cpm_engine_get_devices:
 *
 * Return value: the UpDevice array, free with g_ptr_array_unref()
 **/
GPtrArray *
cpm_engine_get_devices (CpmEngine *engine)
{
	return g_ptr_array_ref (engine->priv->array);
}

/**
 * cpm_engine_get_primary_device:
 *
 * Return value: the #UpDevice, free with g_object_unref()
 **/
UpDevice *
cpm_engine_get_primary_device (CpmEngine *engine)
{
	guint i;
	UpDevice *device = NULL;
	UpDevice *device_tmp;
	UpDeviceKind kind;
	UpDeviceState state;
	gboolean is_present;

	for (i=0; i<engine->priv->array->len; i++) {
		device_tmp = g_ptr_array_index (engine->priv->array, i);

		/* get device properties */
		g_object_get (device_tmp,
			      "kind", &kind,
			      "state", &state,
			      "is-present", &is_present,
			      NULL);

		/* not present */
		if (!is_present)
			continue;

		/* not discharging */
		if (state != UP_DEVICE_STATE_DISCHARGING)
			continue;

		/* not battery */
		if (kind != UP_DEVICE_KIND_BATTERY)
			continue;

		/* use composite device to cope with multiple batteries */
		device = g_object_ref (cpm_engine_get_composite_device (engine, device_tmp));
		break;
	}
	return device;
}

/**
 * phone_device_added_cb:
 **/
static void
phone_device_added_cb (CpmPhone  *phone G_GNUC_UNUSED,
		       guint      idx,
		       CpmEngine *engine)
{
	UpDevice *device;
	device = up_device_new ();

	egg_debug ("phone added %i", idx);

	/* get device properties */
	g_object_set (device,
		      "kind", UP_DEVICE_KIND_PHONE,
		      "is-rechargeable", TRUE,
		      "native-path", g_strdup_printf ("dummy:phone_%i", idx),
		      "is-present", TRUE,
		      NULL);

	/* state changed */
	cpm_engine_device_add (engine, device);
	g_ptr_array_add (engine->priv->array, g_object_ref (device));
	cpm_engine_recalculate_state (engine);
}

/**
 * phone_device_removed_cb:
 **/
static void
phone_device_removed_cb (CpmPhone  *phone G_GNUC_UNUSED,
			 guint      idx,
			 CpmEngine *engine)
{
	guint i;
	UpDevice *device;
	UpDeviceKind kind;

	egg_debug ("phone removed %i", idx);

	for (i=0; i<engine->priv->array->len; i++) {
		device = g_ptr_array_index (engine->priv->array, i);

		/* get device properties */
		g_object_get (device,
			      "kind", &kind,
			      NULL);

		if (kind == UP_DEVICE_KIND_PHONE) {
			g_ptr_array_remove_index (engine->priv->array, i);
			break;
		}
	}

	/* state changed */
	cpm_engine_recalculate_state (engine);
}

/**
 * phone_device_refresh_cb:
 **/
static void
phone_device_refresh_cb (CpmPhone *phone, guint idx, CpmEngine *engine)
{
	guint i;
	UpDevice *device;
	UpDeviceKind kind;
	UpDeviceState state;
	gboolean is_present;
	gdouble percentage;

	egg_debug ("phone refresh %i", idx);

	for (i=0; i<engine->priv->array->len; i++) {
		device = g_ptr_array_index (engine->priv->array, i);

		/* get device properties */
		g_object_get (device,
			      "kind", &kind,
			      "state", &state,
			      "percentage", &percentage,
			      "is-present", &is_present,
			      NULL);

		if (kind == UP_DEVICE_KIND_PHONE) {
			is_present = cpm_phone_get_present (phone, idx);
			state = cpm_phone_get_on_ac (phone, idx) ? UP_DEVICE_STATE_CHARGING : UP_DEVICE_STATE_DISCHARGING;
			percentage = cpm_phone_get_percentage (phone, idx);
			break;
		}
	}

	/* state changed */
	cpm_engine_recalculate_state (engine);
}

/**
 * cpm_engine_init:
 * @engine: This class instance
 **/
static void
cpm_engine_init (CpmEngine *engine)
{
	GPtrArray *array = NULL;
	guint i;
	guint idle_id;
	engine->priv = cpm_engine_get_instance_private (engine);

	engine->priv->array = g_ptr_array_new_with_free_func (g_object_unref);
	engine->priv->client = up_client_new ();
	g_signal_connect (engine->priv->client, "device-added",
			  G_CALLBACK (cpm_engine_device_added_cb), engine);
	g_signal_connect (engine->priv->client, "device-removed",
			  G_CALLBACK (cpm_engine_device_removed_cb), engine);

	engine->priv->settings = g_settings_new (CPM_SETTINGS_SCHEMA);
	g_signal_connect (engine->priv->settings, "changed",
			  G_CALLBACK (cpm_engine_settings_key_changed_cb), engine);

	engine->priv->phone = cpm_phone_new ();
	g_signal_connect (engine->priv->phone, "device-added",
			  G_CALLBACK (phone_device_added_cb), engine);
	g_signal_connect (engine->priv->phone, "device-removed",
			  G_CALLBACK (phone_device_removed_cb), engine);
	g_signal_connect (engine->priv->phone, "device-refresh",
			  G_CALLBACK (phone_device_refresh_cb), engine);

	/* create a fake virtual composite battery */
	engine->priv->battery_composite = up_client_get_display_device (engine->priv->client);
	g_signal_connect (engine->priv->battery_composite, "notify",
			  G_CALLBACK (cpm_engine_device_changed_cb), engine);

	engine->priv->previous_icon = NULL;
	engine->priv->previous_summary = NULL;

	/* do we want to display the icon in the tray */
	engine->priv->icon_policy = g_settings_get_enum (engine->priv->settings, CPM_SETTINGS_ICON_POLICY);

	/* get percentage policy */
	engine->priv->low_percentage = g_settings_get_int (engine->priv->settings, CPM_SETTINGS_PERCENTAGE_LOW);
	engine->priv->critical_percentage = g_settings_get_int (engine->priv->settings, CPM_SETTINGS_PERCENTAGE_CRITICAL);
	engine->priv->action_percentage = g_settings_get_int (engine->priv->settings, CPM_SETTINGS_PERCENTAGE_ACTION);

	/* get time policy */
	engine->priv->low_time = g_settings_get_int (engine->priv->settings, CPM_SETTINGS_TIME_LOW);
	engine->priv->critical_time = g_settings_get_int (engine->priv->settings, CPM_SETTINGS_TIME_CRITICAL);
	engine->priv->action_time = g_settings_get_int (engine->priv->settings, CPM_SETTINGS_TIME_ACTION);

	/* we can disable this if the time remaining is inaccurate or just plain wrong */
	engine->priv->use_time_primary = g_settings_get_boolean (engine->priv->settings, CPM_SETTINGS_USE_TIME_POLICY);
	if (engine->priv->use_time_primary)
		egg_debug ("Using per-time notification policy");
	else
		egg_debug ("Using percentage notification policy");

	idle_id = g_idle_add ((GSourceFunc) cpm_engine_coldplug_idle_cb, engine);
	g_source_set_name_by_id (idle_id, "[CpmEngine] coldplug");
}

/**
 * cpm_engine_class_init:
 * @engine: This class instance
 **/
static void
cpm_engine_class_init (CpmEngineClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = cpm_engine_finalize;

	signals [ICON_CHANGED] =
		g_signal_new ("icon-changed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (CpmEngineClass, icon_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);
	signals [SUMMARY_CHANGED] =
		g_signal_new ("summary-changed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (CpmEngineClass, summary_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);
	signals [LOW_CAPACITY] =
		g_signal_new ("low-capacity",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (CpmEngineClass, low_capacity),
			      NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals [FULLY_CHARGED] =
		g_signal_new ("fully-charged",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (CpmEngineClass, fully_charged),
			      NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals [DISCHARGING] =
		g_signal_new ("discharging",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (CpmEngineClass, discharging),
			      NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals [CHARGE_ACTION] =
		g_signal_new ("charge-action",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (CpmEngineClass, charge_action),
			      NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals [CHARGE_LOW] =
		g_signal_new ("charge-low",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (CpmEngineClass, charge_low),
			      NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals [CHARGE_CRITICAL] =
		g_signal_new ("charge-critical",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (CpmEngineClass, charge_critical),
			      NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals [DEVICES_CHANGED] =
		g_signal_new ("devices-changed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (CpmEngineClass, devices_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

/**
 * cpm_engine_finalize:
 * @object: This class instance
 **/
static void
cpm_engine_finalize (GObject *object)
{
	CpmEngine *engine;

	g_return_if_fail (object != NULL);
	g_return_if_fail (CPM_IS_ENGINE (object));

	engine = CPM_ENGINE (object);
	engine->priv = cpm_engine_get_instance_private (engine);

	g_ptr_array_unref (engine->priv->array);
	g_object_unref (engine->priv->client);
	g_object_unref (engine->priv->phone);
	g_object_unref (engine->priv->battery_composite);

	g_free (engine->priv->previous_icon);
	g_free (engine->priv->previous_summary);

	G_OBJECT_CLASS (cpm_engine_parent_class)->finalize (object);
}

/**
 * cpm_engine_new:
 * Return value: new class instance.
 **/
CpmEngine *
cpm_engine_new (void)
{
	if (cpm_engine_object != NULL) {
		g_object_ref (cpm_engine_object);
	} else {
		cpm_engine_object = g_object_new (CPM_TYPE_ENGINE, NULL);
		g_object_add_weak_pointer (cpm_engine_object, &cpm_engine_object);
	}
	return CPM_ENGINE (cpm_engine_object);

}

