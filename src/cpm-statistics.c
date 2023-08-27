/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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

#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <ctk/ctk.h>
#include <dbus/dbus-glib.h>
#include <libupower-glib/upower.h>

#include "egg-debug.h"
#include "egg-color.h"
#include "egg-array-float.h"

#include "cpm-common.h"
#include "cpm-icon-names.h"
#include "cpm-upower.h"
#include "cpm-graph-widget.h"

static CtkBuilder *builder = NULL;
static CtkListStore *list_store_info = NULL;
static CtkListStore *list_store_devices = NULL;
gchar *current_device = NULL;
static const gchar *history_type;
static const gchar *stats_type;
static guint history_time;
static GSettings *settings;
static gfloat sigma_smoothing = 0.0f;
static CtkWidget *graph_history = NULL;
static CtkWidget *graph_statistics = NULL;

enum {
	GPM_INFO_COLUMN_TEXT,
	GPM_INFO_COLUMN_VALUE,
	GPM_INFO_COLUMN_LAST
};

enum {
	GPM_DEVICES_COLUMN_ICON,
	GPM_DEVICES_COLUMN_TEXT,
	GPM_DEVICES_COLUMN_ID,
	GPM_DEVICES_COLUMN_LAST
};

#define GPM_HISTORY_RATE_TEXT			_("Rate")
#define GPM_HISTORY_CHARGE_TEXT			_("Charge")
#define GPM_HISTORY_TIME_FULL_TEXT		_("Time to full")
#define GPM_HISTORY_TIME_EMPTY_TEXT		_("Time to empty")

#define GPM_HISTORY_RATE_VALUE			"rate"
#define GPM_HISTORY_CHARGE_VALUE		"charge"
#define GPM_HISTORY_TIME_FULL_VALUE		"time-full"
#define GPM_HISTORY_TIME_EMPTY_VALUE		"time-empty"

#define GPM_HISTORY_MINUTE_TEXT			_("10 minutes")
#define GPM_HISTORY_HOUR_TEXT			_("2 hours")
#define GPM_HISTORY_HOURS_TEXT			_("6 hours")
#define GPM_HISTORY_DAY_TEXT			_("1 day")
#define GPM_HISTORY_WEEK_TEXT			_("1 week")

#define GPM_HISTORY_MINUTE_VALUE		10*60
#define GPM_HISTORY_HOUR_VALUE			2*60*60
#define GPM_HISTORY_HOURS_VALUE			6*60*60
#define GPM_HISTORY_DAY_VALUE			24*60*60
#define GPM_HISTORY_WEEK_VALUE			7*24*60*60

/* TRANSLATORS: what we've observed about the device */
#define GPM_STATS_CHARGE_DATA_TEXT		_("Charge profile")
#define GPM_STATS_DISCHARGE_DATA_TEXT		_("Discharge profile")
/* TRANSLATORS: how accurately we can predict the time remaining of the battery */
#define GPM_STATS_CHARGE_ACCURACY_TEXT		_("Charge accuracy")
#define GPM_STATS_DISCHARGE_ACCURACY_TEXT	_("Discharge accuracy")

#define GPM_STATS_CHARGE_DATA_VALUE		"charge-data"
#define GPM_STATS_CHARGE_ACCURACY_VALUE		"charge-accuracy"
#define GPM_STATS_DISCHARGE_DATA_VALUE		"discharge-data"
#define GPM_STATS_DISCHARGE_ACCURACY_VALUE	"discharge-accuracy"

/**
 * cpm_stats_button_help_cb:
 **/
static void
cpm_stats_button_help_cb (CtkWidget *widget, gboolean data)
{
	cpm_help_display ("statistics");
}

/**
 * cpm_stats_add_info_columns:
 **/
static void
cpm_stats_add_info_columns (CtkTreeView *treeview)
{
	CtkCellRenderer *renderer;
	CtkTreeViewColumn *column;

	/* image */
	renderer = ctk_cell_renderer_text_new ();
	column = ctk_tree_view_column_new_with_attributes (_("Attribute"), renderer,
							   "markup", GPM_INFO_COLUMN_TEXT, NULL);
	ctk_tree_view_column_set_sort_column_id (column, GPM_INFO_COLUMN_TEXT);
	ctk_tree_view_append_column (treeview, column);

	/* column for text */
	renderer = ctk_cell_renderer_text_new ();
	column = ctk_tree_view_column_new_with_attributes (_("Value"), renderer,
							   "markup", GPM_INFO_COLUMN_VALUE, NULL);
	ctk_tree_view_append_column (treeview, column);
}

/**
 * cpm_stats_add_devices_columns:
 **/
static void
cpm_stats_add_devices_columns (CtkTreeView *treeview)
{
	CtkCellRenderer *renderer;
	CtkTreeViewColumn *column;

	/* image */
	renderer = ctk_cell_renderer_pixbuf_new ();
	g_object_set (renderer, "stock-size", CTK_ICON_SIZE_DIALOG, NULL);
	column = ctk_tree_view_column_new_with_attributes (_("Image"), renderer,
							   "icon-name", GPM_DEVICES_COLUMN_ICON, NULL);
	ctk_tree_view_append_column (treeview, column);

	/* column for text */
	renderer = ctk_cell_renderer_text_new ();
	column = ctk_tree_view_column_new_with_attributes (_("Description"), renderer,
							   "markup", GPM_DEVICES_COLUMN_TEXT, NULL);
	ctk_tree_view_column_set_sort_column_id (column, GPM_INFO_COLUMN_TEXT);
	ctk_tree_view_append_column (treeview, column);
	ctk_tree_view_column_set_expand (column, TRUE);
}

/**
 * cpm_stats_add_info_data:
 **/
static void
cpm_stats_add_info_data (const gchar *attr, const gchar *text)
{
	CtkTreeIter iter;
	ctk_list_store_append (list_store_info, &iter);
	ctk_list_store_set (list_store_info, &iter,
			    GPM_INFO_COLUMN_TEXT, attr,
			    GPM_INFO_COLUMN_VALUE, text, -1);
}

/**
 * cpm_stats_update_smooth_data:
 **/
static GPtrArray *
cpm_stats_update_smooth_data (GPtrArray *list)
{
	guint i;
	GpmPointObj *point;
	GpmPointObj *point_new;
	GPtrArray *new;
	EggArrayFloat *raw;
	EggArrayFloat *convolved;
	EggArrayFloat *outliers;
	EggArrayFloat *gaussian = NULL;

	/* convert the y data to a EggArrayFloat array */
	raw = egg_array_float_new (list->len);
	for (i=0; i<list->len; i++) {
		point = (GpmPointObj *) g_ptr_array_index (list, i);
		egg_array_float_set (raw, i, point->y);
	}

	/* remove any outliers */
	outliers = egg_array_float_remove_outliers (raw, 3, 0.1);

	/* convolve with gaussian */
	gaussian = egg_array_float_compute_gaussian (15, sigma_smoothing);
	convolved = egg_array_float_convolve (outliers, gaussian);

	/* add the smoothed data back into a new array */
	new = g_ptr_array_new_with_free_func ((GDestroyNotify) cpm_point_obj_free);
	for (i=0; i<list->len; i++) {
		point = (GpmPointObj *) g_ptr_array_index (list, i);
		point_new = g_new0 (GpmPointObj, 1);
		point_new->color = point->color;
		point_new->x = point->x;
		point_new->y = egg_array_float_get (convolved, i);
		g_ptr_array_add (new, point_new);
	}

	/* free data */
	egg_array_float_free (gaussian);
	egg_array_float_free (raw);
	egg_array_float_free (convolved);
	egg_array_float_free (outliers);

	return new;
}

/**
 * cpm_stats_time_to_string:
 **/
static gchar *
cpm_stats_time_to_string (gint seconds)
{
	gfloat value = seconds;

	if (value < 0) {
		/* TRANSLATORS: this is when the stats time is not known */
		return g_strdup (_("Unknown"));
	}
	if (value < 60) {
		/* TRANSLATORS: this is a time value, usually to show on a graph */
		return g_strdup_printf (ngettext ("%.0f second", "%.0f seconds", value), value);
	}
	value /= 60.0;
	if (value < 60) {
		/* TRANSLATORS: this is a time value, usually to show on a graph */
		return g_strdup_printf (ngettext ("%.1f minute", "%.1f minutes", value), value);
	}
	value /= 60.0;
	if (value < 60) {
		/* TRANSLATORS: this is a time value, usually to show on a graph */
		return g_strdup_printf (ngettext ("%.1f hour", "%.1f hours", value), value);
	}
	value /= 24.0;
	/* TRANSLATORS: this is a time value, usually to show on a graph */
	return g_strdup_printf (ngettext ("%.1f day", "%.1f days", value), value);
}

/**
 * cpm_stats_bool_to_string:
 **/
static const gchar *
cpm_stats_bool_to_string (gboolean ret)
{
	return ret ? _("Yes") : _("No");
}

/**
 * cpm_stats_get_printable_device_path:
 **/
static gchar *
cpm_stats_get_printable_device_path (UpDevice *device)
{
	const gchar *object_path;
	gchar *device_path = NULL;

	/* get object path */
	object_path = up_device_get_object_path (device);
	if (object_path != NULL)
		device_path = g_filename_display_basename (object_path);

	return device_path;
}

/**
 * cpm_stats_update_info_page_details:
 **/
static void
cpm_stats_update_info_page_details (UpDevice *device)
{
	struct tm *time_tm;
	time_t t;
	gchar time_buf[256];
	gchar *text;
	guint refreshed;
	UpDeviceKind kind;
	UpDeviceState state;
	UpDeviceTechnology technology;
	gdouble percentage;
	gdouble capacity;
	gdouble energy;
	gdouble energy_empty;
	gdouble energy_full;
	gdouble energy_full_design;
	gdouble energy_rate;
	gdouble voltage;
	gboolean online;
	gboolean is_present;
	gboolean power_supply;
	gboolean is_rechargeable;
	guint64 update_time;
	gint64 time_to_full;
	gint64 time_to_empty;
	gchar *vendor = NULL;
	gchar *serial = NULL;
	gchar *model = NULL;
	gchar *device_path = NULL;

	ctk_list_store_clear (list_store_info);

	/* get device properties */
	g_object_get (device,
		      "kind", &kind,
		      "state", &state,
		      "percentage", &percentage,
		      "online", &online,
		      "update_time", &update_time,
		      "power_supply", &power_supply,
		      "is_rechargeable", &is_rechargeable,
		      "is-present", &is_present,
		      "time-to-full", &time_to_full,
		      "time-to-empty", &time_to_empty,
		      "technology", &technology,
		      "capacity", &capacity,
		      "energy", &energy,
		      "energy-empty", &energy_empty,
		      "energy-full", &energy_full,
		      "energy-full-design", &energy_full_design,
		      "energy-rate", &energy_rate,
		      "voltage", &voltage,
		      "vendor", &vendor,
		      "serial", &serial,
		      "model", &model,
		      NULL);

	/* get a human readable time */
	t = (time_t) update_time;
	time_tm = localtime (&t);
	strftime (time_buf, sizeof time_buf, "%c", time_tm);

	/* remove prefix */
	device_path = cpm_stats_get_printable_device_path (device);
	/* TRANSLATORS: the device ID of the current device, e.g. "battery0" */
	cpm_stats_add_info_data (_("Device"), device_path);
	g_free (device_path);

	cpm_stats_add_info_data (_("Type"), cpm_device_kind_to_localised_string (kind, 1));
	if (vendor != NULL && vendor[0] != '\0')
		cpm_stats_add_info_data (_("Vendor"), vendor);
	if (model != NULL && model[0] != '\0')
		cpm_stats_add_info_data (_("Model"), model);
	if (serial != NULL && serial[0] != '\0')
		cpm_stats_add_info_data (_("Serial number"), serial);

	/* TRANSLATORS: a boolean attribute that means if the device is supplying the
	 * main power for the computer. For instance, an AC adapter or laptop battery
	 * would be TRUE,  but a mobile phone or mouse taking power is FALSE */
	cpm_stats_add_info_data (_("Supply"), cpm_stats_bool_to_string (power_supply));

	refreshed = (int) (time (NULL) - update_time);
	text = g_strdup_printf (ngettext ("%d second", "%d seconds", refreshed), refreshed);

	/* TRANSLATORS: when the device was last updated with new data. It's
	* usually a few seconds when a device is discharging or charging. */
	cpm_stats_add_info_data (_("Refreshed"), text);
	g_free (text);

	if (kind == UP_DEVICE_KIND_BATTERY ||
	    kind == UP_DEVICE_KIND_MOUSE ||
	    kind == UP_DEVICE_KIND_KEYBOARD ||
	    kind == UP_DEVICE_KIND_UPS) {
		/* TRANSLATORS: Present is whether the device is currently attached
		 * to the computer, as some devices (e.g. laptop batteries) can
		 * be removed, but still observed as devices on the system */
		cpm_stats_add_info_data (_("Present"), cpm_stats_bool_to_string (is_present));
	}
	if (kind == UP_DEVICE_KIND_BATTERY ||
	    kind == UP_DEVICE_KIND_MOUSE ||
	    kind == UP_DEVICE_KIND_KEYBOARD) {
		/* TRANSLATORS: If the device can be recharged, e.g. lithium
		 * batteries rather than alkaline ones */
		cpm_stats_add_info_data (_("Rechargeable"), cpm_stats_bool_to_string (is_rechargeable));
	}
	if (kind == UP_DEVICE_KIND_BATTERY ||
	    kind == UP_DEVICE_KIND_MOUSE ||
	    kind == UP_DEVICE_KIND_KEYBOARD) {
		/* TRANSLATORS: The state of the device, e.g. "Changing" or "Fully charged" */
		cpm_stats_add_info_data (_("State"), cpm_device_state_to_localised_string (state));
	}
	if (kind == UP_DEVICE_KIND_BATTERY) {
		text = g_strdup_printf ("%.1f Wh", energy);
		cpm_stats_add_info_data (_("Energy"), text);
		g_free (text);
		text = g_strdup_printf ("%.1f Wh", energy_empty);
		cpm_stats_add_info_data (_("Energy when empty"), text);
		g_free (text);
		text = g_strdup_printf ("%.1f Wh", energy_full);
		cpm_stats_add_info_data (_("Energy when full"), text);
		g_free (text);
		text = g_strdup_printf ("%.1f Wh", energy_full_design);
		cpm_stats_add_info_data (_("Energy (design)"), text);
		g_free (text);
	}
	if (kind == UP_DEVICE_KIND_BATTERY ||
	    kind == UP_DEVICE_KIND_MONITOR) {
		text = g_strdup_printf ("%.1f W", energy_rate);
		/* TRANSLATORS: the rate of discharge for the device */
		cpm_stats_add_info_data (_("Rate"), text);
		g_free (text);
	}
	if (kind == UP_DEVICE_KIND_UPS ||
	    kind == UP_DEVICE_KIND_BATTERY ||
	    kind == UP_DEVICE_KIND_MONITOR) {
		text = g_strdup_printf ("%.1f V", voltage);
		cpm_stats_add_info_data (_("Voltage"), text);
		g_free (text);
	}
	if (kind == UP_DEVICE_KIND_BATTERY ||
	    kind == UP_DEVICE_KIND_UPS) {
		if (time_to_full >= 0) {
			text = cpm_stats_time_to_string (time_to_full);
			cpm_stats_add_info_data (_("Time to full"), text);
			g_free (text);
		}
		if (time_to_empty >= 0) {
			text = cpm_stats_time_to_string (time_to_empty);
			cpm_stats_add_info_data (_("Time to empty"), text);
			g_free (text);
		}
	}
	if (kind == UP_DEVICE_KIND_BATTERY ||
	    kind == UP_DEVICE_KIND_MOUSE ||
	    kind == UP_DEVICE_KIND_KEYBOARD ||
	    kind == UP_DEVICE_KIND_UPS) {
		text = g_strdup_printf ("%.1f%%", percentage);
		/* TRANSLATORS: the amount of charge the cell contains */
		cpm_stats_add_info_data (_("Percentage"), text);
		g_free (text);
	}
	if (kind == UP_DEVICE_KIND_BATTERY) {
		text = g_strdup_printf ("%.1f%%", capacity);
		/* TRANSLATORS: the capacity of the device, which is basically a measure
		 * of how full it can get, relative to the design capacity */
		cpm_stats_add_info_data (_("Capacity"), text);
		g_free (text);
	}
	if (kind == UP_DEVICE_KIND_BATTERY) {
		/* TRANSLATORS: the type of battery, e.g. lithium or nikel metal hydroxide */
		cpm_stats_add_info_data (_("Technology"), cpm_device_technology_to_localised_string (technology));
	}
	if (kind == UP_DEVICE_KIND_LINE_POWER) {
		/* TRANSLATORS: this is when the device is plugged in, typically
		 * only shown for the ac adaptor device */
		cpm_stats_add_info_data (_("Online"), cpm_stats_bool_to_string (online));
	}

	g_free (vendor);
	g_free (serial);
	g_free (model);
}

/**
 * cpm_stats_set_graph_data:
 **/
static void
cpm_stats_set_graph_data (CtkWidget *widget, GPtrArray *data, gboolean use_smoothed, gboolean use_points)
{
	GPtrArray *smoothed;

	cpm_graph_widget_data_clear (GPM_GRAPH_WIDGET (widget));

	/* add correct data */
	if (!use_smoothed) {
		if (use_points)
			cpm_graph_widget_data_assign (GPM_GRAPH_WIDGET (widget), GPM_GRAPH_WIDGET_PLOT_BOTH, data);
		else
			cpm_graph_widget_data_assign (GPM_GRAPH_WIDGET (widget), GPM_GRAPH_WIDGET_PLOT_LINE, data);
	} else {
		smoothed = cpm_stats_update_smooth_data (data);
		if (use_points)
			cpm_graph_widget_data_assign (GPM_GRAPH_WIDGET (widget), GPM_GRAPH_WIDGET_PLOT_POINTS, data);
		cpm_graph_widget_data_assign (GPM_GRAPH_WIDGET (widget), GPM_GRAPH_WIDGET_PLOT_LINE, smoothed);
		g_ptr_array_unref (smoothed);
	}

	/* show */
	ctk_widget_show (widget);
}

/**
 * cpm_stats_update_info_page_history:
 **/
static void
cpm_stats_update_info_page_history (UpDevice *device)
{
	GPtrArray *array;
	guint i;
	UpHistoryItem *item;
	CtkWidget *widget;
	gboolean checked;
	gboolean points;
	GpmPointObj *point;
	GPtrArray *new;
	gint32 offset = 0;
	GTimeVal timeval;

	new = g_ptr_array_new_with_free_func ((GDestroyNotify) cpm_point_obj_free);
	if (g_strcmp0 (history_type, GPM_HISTORY_CHARGE_VALUE) == 0) {
		g_object_set (graph_history,
			      "type-x", GPM_GRAPH_WIDGET_TYPE_TIME,
			      "type-y", GPM_GRAPH_WIDGET_TYPE_PERCENTAGE,
			      "autorange-x", FALSE,
			      "start-x", -history_time,
			      "stop-x", 0,
			      "autorange-y", FALSE,
			      "start-y", 0,
			      "stop-y", 100,
			      NULL);
	} else if (g_strcmp0 (history_type, GPM_HISTORY_RATE_VALUE) == 0) {
		g_object_set (graph_history,
			      "type-x", GPM_GRAPH_WIDGET_TYPE_TIME,
			      "type-y", GPM_GRAPH_WIDGET_TYPE_POWER,
			      "autorange-x", FALSE,
			      "start-x", -history_time,
			      "stop-x", 0,
			      "autorange-y", TRUE,
			      NULL);
	} else {
		g_object_set (graph_history,
			      "type-x", GPM_GRAPH_WIDGET_TYPE_TIME,
			      "type-y", GPM_GRAPH_WIDGET_TYPE_TIME,
			      "autorange-x", FALSE,
			      "start-x", -history_time,
			      "stop-x", 0,
			      "autorange-y", TRUE,
			      NULL);
	}

	widget = CTK_WIDGET (ctk_builder_get_object (builder, "label_history_nodata"));
	array = up_device_get_history_sync (device, history_type, history_time, 150, NULL, NULL);
	if (array == NULL) {
		/* show no data label and hide graph */
		ctk_widget_hide (graph_history);
		ctk_widget_show (widget);
		goto out;
	}

	/* hide no data and show graph */
	ctk_widget_hide (widget);
	ctk_widget_show (graph_history);

	g_get_current_time (&timeval);
	offset = timeval.tv_sec;

	for (i=0; i<array->len; i++) {
		item = (UpHistoryItem *) g_ptr_array_index (array, i);

		/* abandon this point */
		if (up_history_item_get_state (item) == UP_DEVICE_STATE_UNKNOWN)
			continue;

		point = cpm_point_obj_new ();
		point->x = (gint32) up_history_item_get_time (item) - offset;
		point->y = up_history_item_get_value (item);
		if (up_history_item_get_state (item) == UP_DEVICE_STATE_CHARGING)
			point->color = egg_color_from_rgb (255, 0, 0);
		else if (up_history_item_get_state (item) == UP_DEVICE_STATE_DISCHARGING)
			point->color = egg_color_from_rgb (0, 0, 255);
		else if (up_history_item_get_state (item) == UP_DEVICE_STATE_PENDING_CHARGE)
			point->color = egg_color_from_rgb (200, 0, 0);
		else if (up_history_item_get_state (item) == UP_DEVICE_STATE_PENDING_DISCHARGE)
			point->color = egg_color_from_rgb (0, 0, 200);
		else {
			if (g_strcmp0 (history_type, GPM_HISTORY_RATE_VALUE) == 0)
				point->color = egg_color_from_rgb (255, 255, 255);
			else
				point->color = egg_color_from_rgb (0, 255, 0);
		}
		g_ptr_array_add (new, point);
	}

	/* render */
	sigma_smoothing = 2.0;
	widget = CTK_WIDGET (ctk_builder_get_object (builder, "checkbutton_smooth_history"));
	checked = ctk_toggle_button_get_active (CTK_TOGGLE_BUTTON (widget));
	widget = CTK_WIDGET (ctk_builder_get_object (builder, "checkbutton_points_history"));
	points = ctk_toggle_button_get_active (CTK_TOGGLE_BUTTON (widget));

	/* present data to graph */
	cpm_stats_set_graph_data (graph_history, new, checked, points);

	g_ptr_array_unref (array);
	g_ptr_array_unref (new);
out:
	return;
}

/**
 * cpm_stats_update_info_page_stats:
 **/
static void
cpm_stats_update_info_page_stats (UpDevice *device)
{
	GPtrArray *array;
	guint i;
	UpStatsItem *item;
	CtkWidget *widget;
	gboolean checked;
	gboolean points;
	GpmPointObj *point;
	GPtrArray *new;
	gboolean use_data = FALSE;
	const gchar *type = NULL;

	new = g_ptr_array_new_with_free_func ((GDestroyNotify) cpm_point_obj_free);
	if (g_strcmp0 (stats_type, GPM_STATS_CHARGE_DATA_VALUE) == 0) {
		type = "charging";
		use_data = TRUE;
	} else if (g_strcmp0 (stats_type, GPM_STATS_DISCHARGE_DATA_VALUE) == 0) {
		type = "discharging";
		use_data = TRUE;
	} else if (g_strcmp0 (stats_type, GPM_STATS_CHARGE_ACCURACY_VALUE) == 0) {
		type = "charging";
		use_data = FALSE;
	} else if (g_strcmp0 (stats_type, GPM_STATS_DISCHARGE_ACCURACY_VALUE) == 0) {
		type = "discharging";
		use_data = FALSE;
	} else {
		g_assert_not_reached ();
	}

	if (use_data) {
		g_object_set (graph_statistics,
			      "type-x", GPM_GRAPH_WIDGET_TYPE_PERCENTAGE,
			      "type-y", GPM_GRAPH_WIDGET_TYPE_FACTOR,
			      "autorange-x", TRUE,
			      "autorange-y", TRUE,
			      NULL);
	} else {
		g_object_set (graph_statistics,
			      "type-x", GPM_GRAPH_WIDGET_TYPE_PERCENTAGE,
			      "type-y", GPM_GRAPH_WIDGET_TYPE_PERCENTAGE,
			      "autorange-x", TRUE,
			      "autorange-y", TRUE,
			      NULL);
	}

	widget = CTK_WIDGET (ctk_builder_get_object (builder, "label_stats_nodata"));
	array = up_device_get_statistics_sync (device, type, NULL, NULL);
	if (array == NULL) {
		/* show no data label and hide graph */
		ctk_widget_hide (graph_statistics);
		ctk_widget_show (widget);
		goto out;
	}

	/* hide no data and show graph */
	ctk_widget_hide (widget);
	ctk_widget_show (graph_statistics);

	for (i=0; i<array->len; i++) {
		item = (UpStatsItem *) g_ptr_array_index (array, i);
		point = cpm_point_obj_new ();
		point->x = i;
		if (use_data)
			point->y = up_stats_item_get_value (item);
		else
			point->y = up_stats_item_get_accuracy (item);
		point->color = egg_color_from_rgb (255, 0, 0);
		g_ptr_array_add (new, point);
	}

	/* render */
	sigma_smoothing = 1.1;
	widget = CTK_WIDGET (ctk_builder_get_object (builder, "checkbutton_smooth_stats"));
	checked = ctk_toggle_button_get_active (CTK_TOGGLE_BUTTON (widget));
	widget = CTK_WIDGET (ctk_builder_get_object (builder, "checkbutton_points_stats"));
	points = ctk_toggle_button_get_active (CTK_TOGGLE_BUTTON (widget));

	/* present data to graph */
	cpm_stats_set_graph_data (graph_statistics, new, checked, points);

	g_ptr_array_unref (array);
	g_ptr_array_unref (new);
out:
	return;
}

/**
 * cpm_stats_update_info_data_page:
 **/
static void
cpm_stats_update_info_data_page (UpDevice *device, gint page)
{
	if (page == 0)
		cpm_stats_update_info_page_details (device);
	else if (page == 1)
		cpm_stats_update_info_page_history (device);
	else if (page == 2)
		cpm_stats_update_info_page_stats (device);
}

/**
 * cpm_stats_update_info_data:
 **/
static void
cpm_stats_update_info_data (UpDevice *device)
{
	gint page;
	CtkNotebook *notebook;
	CtkWidget *page_widget;
	gboolean has_history;
	gboolean has_statistics;

	/* get properties */
	g_object_get (device,
		      "has-history", &has_history,
		      "has-statistics", &has_statistics,
		      NULL);


	notebook = CTK_NOTEBOOK (ctk_builder_get_object (builder, "notebook1"));

	/* show info page */
	page_widget = ctk_notebook_get_nth_page (notebook, 0);
	ctk_widget_show (page_widget);

	/* hide history if no support */
	page_widget = ctk_notebook_get_nth_page (notebook, 1);
	if (has_history)
		ctk_widget_show (page_widget);
	else
		ctk_widget_hide (page_widget);

	/* hide statistics if no support */
	page_widget = ctk_notebook_get_nth_page (notebook, 2);
	if (has_statistics)
		ctk_widget_show (page_widget);
	else
		ctk_widget_hide (page_widget);

	page = ctk_notebook_get_current_page (notebook);
	cpm_stats_update_info_data_page (device, page);

	return;
}

static void
cpm_stats_set_title (CtkWindow *window, gint page_num)
{
	gchar *title;
	const gchar * const page_titles[] = {
		/* TRANSLATORS: shown on the titlebar */
		N_("Device Information"),
		/* TRANSLATORS: shown on the titlebar */
		N_("Device History"),
		/* TRANSLATORS: shown on the titlebar */
		N_("Device Profile"),
	};

	/* TRANSLATORS: shown on the titlebar */
	title = g_strdup_printf ("%s - %s", _("Power Statistics"), _(page_titles[page_num]));
	ctk_window_set_title (window, title);
	g_free (title);
}

/**
 * cpm_stats_notebook_changed_cb:
 **/
static void
cpm_stats_notebook_changed_cb (CtkNotebook *notebook, gpointer page, gint page_num, gpointer user_data)
{
	UpDevice *device;
	CtkWidget *widget;

	/* set the window title depending on the mode */
	widget = CTK_WIDGET (ctk_builder_get_object (builder, "dialog_stats"));
	cpm_stats_set_title (CTK_WINDOW (widget), page_num);

	/* save page in gsettings */
	g_settings_set_int (settings, GPM_SETTINGS_INFO_PAGE_NUMBER, page_num);

	if (current_device == NULL)
		return;

	device = up_device_new ();
	up_device_set_object_path_sync (device, current_device, NULL, NULL);
	cpm_stats_update_info_data_page (device, page_num);
	cpm_stats_update_info_data (device);
	g_object_unref (device);
}

/**
 * cpm_stats_button_update_ui:
 **/
static void
cpm_stats_button_update_ui (void)
{
	UpDevice *device;

	if (current_device == NULL)
		return;

	device = up_device_new ();
	up_device_set_object_path_sync (device, current_device, NULL, NULL);
	cpm_stats_update_info_data (device);
	g_object_unref (device);
}

/**
 * cpm_stats_devices_treeview_clicked_cb:
 **/
static void
cpm_stats_devices_treeview_clicked_cb (CtkTreeSelection *selection, gboolean data)
{
	CtkTreeModel *model;
	CtkTreeIter iter;
	UpDevice *device;

	/* This will only work in single or browse selection mode! */
	if (ctk_tree_selection_get_selected (selection, &model, &iter)) {
		g_free (current_device);
		ctk_tree_model_get (model, &iter, GPM_DEVICES_COLUMN_ID, &current_device, -1);

		/* save device in gsettings */
		g_settings_set_string (settings, GPM_SETTINGS_INFO_LAST_DEVICE, current_device);

		/* show transaction_id */
		egg_debug ("selected row is: %s", current_device);

		/* is special device */
		device = up_device_new ();
		up_device_set_object_path_sync (device, current_device, NULL, NULL);
		cpm_stats_update_info_data (device);
		g_object_unref (device);

	} else {
		egg_debug ("no row selected");
	}
}

/**
 * cpm_stats_window_activated_cb
 **/
static void
cpm_stats_window_activated_cb (CtkApplication *app, gpointer data)
{
	CtkWidget *widget;
	widget = CTK_WIDGET (ctk_builder_get_object (builder, "dialog_stats"));
	ctk_application_add_window (CTK_APPLICATION (app), CTK_WINDOW (widget));
	ctk_window_present (CTK_WINDOW (widget));
}

/**
 * cpm_stats_device_changed_cb:
 **/
static void
cpm_stats_device_changed_cb (UpDevice *device, GParamSpec *pspec, gpointer user_data)
{
	const gchar *object_path;
	object_path = up_device_get_object_path (device);
	if (object_path == NULL || current_device == NULL)
		return;
	egg_debug ("changed:   %s", object_path);
	if (g_strcmp0 (current_device, object_path) == 0)
		cpm_stats_update_info_data (device);
}

/**
 * cpm_stats_add_device:
 **/
static void
cpm_stats_add_device (UpDevice *device, GPtrArray *devices)
{
	const gchar *id;
	CtkTreeIter iter;
	const gchar *text;
	const gchar *icon;
	UpDeviceKind kind;
	gchar *label, *vendor, *model;

	if (devices != NULL)
		g_ptr_array_add (devices, device);

	g_signal_connect (device, "notify",
	                  G_CALLBACK (cpm_stats_device_changed_cb), NULL);

	/* get device properties */
	g_object_get (device,
		      "kind", &kind,
		      "vendor", &vendor,
		      "model", &model,
		      NULL);

	id = up_device_get_object_path (device);
	if ((vendor != NULL && strlen(vendor) != 0) && (model != NULL && strlen(model) != 0)) {
		label = g_strdup_printf ("%s %s", vendor, model);
	}
	else {
		label = g_strdup_printf ("%s", cpm_device_kind_to_localised_string (kind, 1));
	}
	icon = cpm_upower_get_device_icon (device);

	ctk_list_store_append (list_store_devices, &iter);
	ctk_list_store_set (list_store_devices, &iter,
			    GPM_DEVICES_COLUMN_ID, id,
			    GPM_DEVICES_COLUMN_TEXT, label,
			    GPM_DEVICES_COLUMN_ICON, icon, -1);
	g_free (label);
	g_free (vendor);
	g_free (model);
}

/**
 * cpm_stats_device_added_cb:
 **/
static void
cpm_stats_device_added_cb (UpClient *client, UpDevice *device, GPtrArray *devices)
{
	const gchar *object_path;
	object_path = up_device_get_object_path (device);
	egg_debug ("added:     %s", object_path);

	cpm_stats_add_device (device, devices);
}

/**
 * cpm_stats_device_removed_cb:
 **/
static void
cpm_stats_device_removed_cb (UpClient *client, const gchar *object_path, GPtrArray *devices)
{
	CtkTreeIter iter;
	gchar *id = NULL;
	gboolean ret;

	UpDevice *device_tmp;
	guint i;

	for (i = 0; i < devices->len; i++) {
		device_tmp = g_ptr_array_index (devices, i);
		if (g_strcmp0 (up_device_get_object_path (device_tmp), object_path) == 0) {
			g_ptr_array_remove_index_fast (devices, i);
			break;
		}
	}
	egg_debug ("removed:   %s", object_path);
	if (g_strcmp0 (current_device, object_path) == 0) {
		ctk_list_store_clear (list_store_info);
	}

	/* search the list and remove the object path entry */
	ret = ctk_tree_model_get_iter_first (CTK_TREE_MODEL (list_store_devices), &iter);
	while (ret) {
		ctk_tree_model_get (CTK_TREE_MODEL (list_store_devices), &iter, GPM_DEVICES_COLUMN_ID, &id, -1);
		if (g_strcmp0 (id, object_path) == 0) {
			ctk_list_store_remove (list_store_devices, &iter);
			break;
		}
		g_free (id);
		ret = ctk_tree_model_iter_next (CTK_TREE_MODEL (list_store_devices), &iter);
	};
}

/**
 * cpm_stats_history_type_combo_changed_cb:
 **/
static void
cpm_stats_history_type_combo_changed_cb (CtkWidget *widget, gpointer data)
{
	guint active;
	const gchar *axis_x = NULL;
	const gchar *axis_y = NULL;

	active = ctk_combo_box_get_active (CTK_COMBO_BOX (widget));

	if (active == 0) {
		history_type = GPM_HISTORY_RATE_VALUE;
		/* TRANSLATORS: this is the X axis on the graph */
		axis_x = _("Time elapsed");
		/* TRANSLATORS: this is the Y axis on the graph */
		axis_y = _("Power");
	} else if (active == 1) {
		history_type = GPM_HISTORY_CHARGE_VALUE;
		/* TRANSLATORS: this is the X axis on the graph */
		axis_x = _("Time elapsed");
		/* TRANSLATORS: this is the Y axis on the graph for the whole battery device */
		axis_y = _("Cell charge");
	} else if (active == 2) {
		history_type = GPM_HISTORY_TIME_FULL_VALUE;
		/* TRANSLATORS: this is the X axis on the graph */
		axis_x = _("Time elapsed");
		/* TRANSLATORS: this is the Y axis on the graph */
		axis_y = _("Predicted time");
	} else if (active == 3) {
		history_type = GPM_HISTORY_TIME_EMPTY_VALUE;
		/* TRANSLATORS: this is the X axis on the graph */
		axis_x = _("Time elapsed");
		/* TRANSLATORS: this is the Y axis on the graph */
		axis_y = _("Predicted time");
	} else {
		g_assert (FALSE);
	}

	/* set axis */
	widget = CTK_WIDGET (ctk_builder_get_object (builder, "label_axis_history_x"));
	ctk_label_set_label (CTK_LABEL(widget), axis_x);
	widget = CTK_WIDGET (ctk_builder_get_object (builder, "label_axis_history_y"));
	ctk_label_set_label (CTK_LABEL(widget), axis_y);

	cpm_stats_button_update_ui ();

	/* save to gsettings */
	g_settings_set_string (settings, GPM_SETTINGS_INFO_HISTORY_TYPE, history_type);
}

/**
 * cpm_stats_type_combo_changed_cb:
 **/
static void
cpm_stats_type_combo_changed_cb (CtkWidget *widget, gpointer data)
{
	guint active;
	const gchar *axis_x = NULL;
	const gchar *axis_y = NULL;

	active = ctk_combo_box_get_active (CTK_COMBO_BOX (widget));

	if (active == 0) {
		stats_type = GPM_STATS_CHARGE_DATA_VALUE;
		/* TRANSLATORS: this is the X axis on the graph for the whole battery device */
		axis_x = _("Cell charge");
		/* TRANSLATORS: this is the Y axis on the graph */
		axis_y = _("Correction factor");
	} else if (active == 1) {
		stats_type = GPM_STATS_CHARGE_ACCURACY_VALUE;
		/* TRANSLATORS: this is the X axis on the graph for the whole battery device */
		axis_x = _("Cell charge");
		/* TRANSLATORS: this is the Y axis on the graph */
		axis_y = _("Prediction accuracy");
	} else if (active == 2) {
		stats_type = GPM_STATS_DISCHARGE_DATA_VALUE;
		/* TRANSLATORS: this is the X axis on the graph for the whole battery device */
		axis_x = _("Cell charge");
		/* TRANSLATORS: this is the Y axis on the graph */
		axis_y = _("Correction factor");
	} else if (active == 3) {
		stats_type = GPM_STATS_DISCHARGE_ACCURACY_VALUE;
		/* TRANSLATORS: this is the X axis on the graph for the whole battery device */
		axis_x = _("Cell charge");
		/* TRANSLATORS: this is the Y axis on the graph */
		axis_y = _("Prediction accuracy");
	} else {
		g_assert (FALSE);
	}

	/* set axis */
	widget = CTK_WIDGET (ctk_builder_get_object (builder, "label_axis_stats_x"));
	ctk_label_set_label (CTK_LABEL(widget), axis_x);
	widget = CTK_WIDGET (ctk_builder_get_object (builder, "label_axis_stats_y"));
	ctk_label_set_label (CTK_LABEL(widget), axis_y);

	cpm_stats_button_update_ui ();

	/* save to gsettings */
	g_settings_set_string (settings, GPM_SETTINGS_INFO_STATS_TYPE, stats_type);
}

/**
 * cpm_stats_range_combo_changed:
 **/
static void
cpm_stats_range_combo_changed (CtkWidget *widget, gpointer data)
{
	guint active;

	active = ctk_combo_box_get_active (CTK_COMBO_BOX (widget));

	if (active == 0)
		history_time = GPM_HISTORY_MINUTE_VALUE;
	else if (active == 1)
		history_time = GPM_HISTORY_HOUR_VALUE;
	else if (active == 2)
		history_time = GPM_HISTORY_HOURS_VALUE;
	else if (active == 3)
		history_time = GPM_HISTORY_DAY_VALUE;
	else if (active == 4)
		history_time = GPM_HISTORY_WEEK_VALUE;
	else
		g_assert (FALSE);

	/* save to gsettings */
	g_settings_set_int (settings, GPM_SETTINGS_INFO_HISTORY_TIME, history_time);

	cpm_stats_button_update_ui ();
}

/**
 * cpm_stats_smooth_checkbox_history_cb:
 * @widget: The CtkWidget object
 **/
static void
cpm_stats_smooth_checkbox_history_cb (CtkWidget *widget, gpointer data)
{
	gboolean checked;
	checked = ctk_toggle_button_get_active (CTK_TOGGLE_BUTTON (widget));
	g_settings_set_boolean (settings, GPM_SETTINGS_INFO_HISTORY_GRAPH_SMOOTH, checked);
	cpm_stats_button_update_ui ();
}

/**
 * cpm_stats_smooth_checkbox_stats_cb:
 * @widget: The CtkWidget object
 **/
static void
cpm_stats_smooth_checkbox_stats_cb (CtkWidget *widget, gpointer data)
{
	gboolean checked;
	checked = ctk_toggle_button_get_active (CTK_TOGGLE_BUTTON (widget));
	g_settings_set_boolean (settings, GPM_SETTINGS_INFO_STATS_GRAPH_SMOOTH, checked);
	cpm_stats_button_update_ui ();
}

/**
 * cpm_stats_points_checkbox_history_cb:
 * @widget: The CtkWidget object
 **/
static void
cpm_stats_points_checkbox_history_cb (CtkWidget *widget, gpointer data)
{
	gboolean checked;
	checked = ctk_toggle_button_get_active (CTK_TOGGLE_BUTTON (widget));
	g_settings_set_boolean (settings, GPM_SETTINGS_INFO_HISTORY_GRAPH_POINTS, checked);
	cpm_stats_button_update_ui ();
}

/**
 * cpm_stats_points_checkbox_stats_cb:
 * @widget: The CtkWidget object
 **/
static void
cpm_stats_points_checkbox_stats_cb (CtkWidget *widget, gpointer data)
{
	gboolean checked;
	checked = ctk_toggle_button_get_active (CTK_TOGGLE_BUTTON (widget));
	g_settings_set_boolean (settings, GPM_SETTINGS_INFO_STATS_GRAPH_POINTS, checked);
	cpm_stats_button_update_ui ();
}

/**
 * cpm_stats_highlight_device:
 **/
static void
cpm_stats_highlight_device (const gchar *object_path)
{
	gboolean ret;
	gchar *id = NULL;
	gchar *path_str;
	guint i;
	CtkTreeIter iter;
	CtkTreePath *path;
	CtkWidget *widget;

	/* check valid */
	if (!g_str_has_prefix (object_path, "/"))
		return;

	/* we have to reuse the treeview data as it may be sorted */
	ret = ctk_tree_model_get_iter_first (CTK_TREE_MODEL (list_store_devices), &iter);
	for (i=0; ret; i++) {
		ctk_tree_model_get (CTK_TREE_MODEL (list_store_devices), &iter,
				    GPM_DEVICES_COLUMN_ID, &id,
				    -1);
		if (g_strcmp0 (id, object_path) == 0) {
			path_str = g_strdup_printf ("%i", i);
			path = ctk_tree_path_new_from_string (path_str);
			widget = CTK_WIDGET (ctk_builder_get_object (builder, "treeview_devices"));
			ctk_tree_view_set_cursor_on_cell (CTK_TREE_VIEW (widget), path, NULL, NULL, FALSE);
			g_free (path_str);
			ctk_tree_path_free (path);
		}
		g_free (id);
		ret = ctk_tree_model_iter_next (CTK_TREE_MODEL (list_store_devices), &iter);
	}
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean verbose = FALSE;
	GOptionContext *context;
	CtkBox *box;
	CtkWidget *widget, *window;
	CtkTreeSelection *selection;
	CtkApplication *app;
	gint status;
	UpClient *client;
	GPtrArray *devices = NULL;
	UpDevice *device;
	UpDeviceKind kind;
	guint i, j;
	gint page;
	gboolean checked;
	gchar *last_device = NULL;
	guint retval;
	GError *error = NULL;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  /* TRANSLATORS: show verbose debugging */
		  N_("Show extra debugging information"), NULL },
		{ "device", '\0', 0, G_OPTION_ARG_STRING, &last_device,
		  /* TRANSLATORS: show a device by default */
		  N_("Select this device at startup"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, CAFELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	dbus_g_thread_init ();

	context = g_option_context_new (NULL);
	/* TRANSLATORS: the program name */
	g_option_context_set_summary (context, _("Power Statistics"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	egg_debug_init (verbose);
	ctk_init (&argc, &argv);

	app = ctk_application_new ("org.cafe.PowerManager.Statistics", 0);

	g_signal_connect (app, "activate",
			  G_CALLBACK (cpm_stats_window_activated_cb), NULL);

	/* add application specific icons to search path */
	ctk_icon_theme_append_search_path (ctk_icon_theme_get_default (),
                                           GPM_ICONS_DATA);

	/* get data from the settings */
	settings = g_settings_new (GPM_SETTINGS_SCHEMA);

	/* get UI */
	builder = ctk_builder_new ();
	retval = ctk_builder_add_from_resource (builder, "/org/cafe/powermanager/statistics/cpm-statistics.ui", &error);

	if (error) {
		egg_error ("failed to load ui: %s", error->message);
	}

	/* add history graph */
	box = CTK_BOX (ctk_builder_get_object (builder, "hbox_history"));
	graph_history = cpm_graph_widget_new ();
	ctk_box_pack_start (box, graph_history, TRUE, TRUE, 0);
	ctk_widget_set_size_request (graph_history, 400, 250);
	ctk_widget_show (graph_history);

	/* add statistics graph */
	box = CTK_BOX (ctk_builder_get_object (builder, "hbox_statistics"));
	graph_statistics = cpm_graph_widget_new ();
	ctk_box_pack_start (box, graph_statistics, TRUE, TRUE, 0);
	ctk_widget_set_size_request (graph_statistics, 400, 250);
	ctk_widget_show (graph_statistics);

	window = CTK_WIDGET (ctk_builder_get_object (builder, "dialog_stats"));
	ctk_window_set_default_size (CTK_WINDOW(window), 800, 500);
	ctk_window_set_default_icon_name (GPM_ICON_APP_ICON);

	/* Get the main window quit */
	widget = CTK_WIDGET (ctk_builder_get_object (builder, "button_close"));
	g_signal_connect_swapped (window, "delete_event", G_CALLBACK (ctk_widget_destroy), window);
	g_signal_connect_swapped (widget, "clicked", G_CALLBACK (ctk_widget_destroy), window);
	ctk_widget_grab_default (widget);

	widget = CTK_WIDGET (ctk_builder_get_object (builder, "button_help"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (cpm_stats_button_help_cb), NULL);

	widget = CTK_WIDGET (ctk_builder_get_object (builder, "checkbutton_smooth_history"));
	checked = g_settings_get_boolean (settings, GPM_SETTINGS_INFO_HISTORY_GRAPH_SMOOTH);
	ctk_toggle_button_set_active (CTK_TOGGLE_BUTTON (widget), checked);
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (cpm_stats_smooth_checkbox_history_cb), NULL);

	widget = CTK_WIDGET (ctk_builder_get_object (builder, "checkbutton_smooth_stats"));
	checked = g_settings_get_boolean (settings, GPM_SETTINGS_INFO_STATS_GRAPH_SMOOTH);
	ctk_toggle_button_set_active (CTK_TOGGLE_BUTTON (widget), checked);
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (cpm_stats_smooth_checkbox_stats_cb), NULL);

	widget = CTK_WIDGET (ctk_builder_get_object (builder, "checkbutton_points_history"));
	checked = g_settings_get_boolean (settings, GPM_SETTINGS_INFO_HISTORY_GRAPH_POINTS);
	ctk_toggle_button_set_active (CTK_TOGGLE_BUTTON (widget), checked);
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (cpm_stats_points_checkbox_history_cb), NULL);

	widget = CTK_WIDGET (ctk_builder_get_object (builder, "checkbutton_points_stats"));
	checked = g_settings_get_boolean (settings, GPM_SETTINGS_INFO_STATS_GRAPH_POINTS);
	ctk_toggle_button_set_active (CTK_TOGGLE_BUTTON (widget), checked);
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (cpm_stats_points_checkbox_stats_cb), NULL);

	widget = CTK_WIDGET (ctk_builder_get_object (builder, "notebook1"));

	ctk_widget_add_events (widget, CDK_SCROLL_MASK);
	g_signal_connect (widget, "scroll-event",
	                  G_CALLBACK (cpm_dialog_page_scroll_event_cb),
	                  window);

	page = g_settings_get_int (settings, GPM_SETTINGS_INFO_PAGE_NUMBER);
	ctk_notebook_set_current_page (CTK_NOTEBOOK (widget), page);
	g_signal_connect (widget, "switch-page",
			  G_CALLBACK (cpm_stats_notebook_changed_cb), NULL);

	/* create list stores */
	list_store_info = ctk_list_store_new (GPM_INFO_COLUMN_LAST, G_TYPE_STRING, G_TYPE_STRING);
	list_store_devices = ctk_list_store_new (GPM_DEVICES_COLUMN_LAST, G_TYPE_STRING,
						 G_TYPE_STRING, G_TYPE_STRING);

	/* create transaction_id tree view */
	widget = CTK_WIDGET (ctk_builder_get_object (builder, "treeview_info"));
	ctk_tree_view_set_model (CTK_TREE_VIEW (widget),
				 CTK_TREE_MODEL (list_store_info));

	/* add columns to the tree view */
	cpm_stats_add_info_columns (CTK_TREE_VIEW (widget));
	ctk_tree_view_columns_autosize (CTK_TREE_VIEW (widget)); /* show */

	/* create transaction_id tree view */
	widget = CTK_WIDGET (ctk_builder_get_object (builder, "treeview_devices"));
	ctk_tree_view_set_model (CTK_TREE_VIEW (widget),
				 CTK_TREE_MODEL (list_store_devices));
	selection = ctk_tree_view_get_selection (CTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (cpm_stats_devices_treeview_clicked_cb), NULL);

	/* add columns to the tree view */
	cpm_stats_add_devices_columns (CTK_TREE_VIEW (widget));
	ctk_tree_view_columns_autosize (CTK_TREE_VIEW (widget)); /* show */

	history_type = g_settings_get_string (settings, GPM_SETTINGS_INFO_HISTORY_TYPE);
	history_time = g_settings_get_int (settings, GPM_SETTINGS_INFO_HISTORY_TIME);
	if (history_type == NULL)
		history_type = GPM_HISTORY_CHARGE_VALUE;
	if (history_time == 0)
		history_time = GPM_HISTORY_HOUR_VALUE;

	stats_type = g_settings_get_string (settings, GPM_SETTINGS_INFO_STATS_TYPE);
	if (stats_type == NULL)
		stats_type = GPM_STATS_CHARGE_DATA_VALUE;

	widget = CTK_WIDGET (ctk_builder_get_object (builder, "combobox_history_type"));
	ctk_combo_box_text_append_text (CTK_COMBO_BOX_TEXT (widget), GPM_HISTORY_RATE_TEXT);
	ctk_combo_box_text_append_text (CTK_COMBO_BOX_TEXT (widget), GPM_HISTORY_CHARGE_TEXT);
	ctk_combo_box_text_append_text (CTK_COMBO_BOX_TEXT (widget), GPM_HISTORY_TIME_FULL_TEXT);
	ctk_combo_box_text_append_text (CTK_COMBO_BOX_TEXT (widget), GPM_HISTORY_TIME_EMPTY_TEXT);
	if (g_strcmp0 (history_type, GPM_HISTORY_RATE_VALUE) == 0)
		ctk_combo_box_set_active (CTK_COMBO_BOX (widget), 0);
	else
		ctk_combo_box_set_active (CTK_COMBO_BOX (widget), 1);
	g_signal_connect (G_OBJECT (widget), "changed",
			  G_CALLBACK (cpm_stats_history_type_combo_changed_cb), NULL);

	widget = CTK_WIDGET (ctk_builder_get_object (builder, "combobox_stats_type"));
	ctk_combo_box_text_append_text (CTK_COMBO_BOX_TEXT (widget), GPM_STATS_CHARGE_DATA_TEXT);
	ctk_combo_box_text_append_text (CTK_COMBO_BOX_TEXT (widget), GPM_STATS_CHARGE_ACCURACY_TEXT);
	ctk_combo_box_text_append_text (CTK_COMBO_BOX_TEXT (widget), GPM_STATS_DISCHARGE_DATA_TEXT);
	ctk_combo_box_text_append_text (CTK_COMBO_BOX_TEXT (widget), GPM_STATS_DISCHARGE_ACCURACY_TEXT);
	if (g_strcmp0 (stats_type, GPM_STATS_CHARGE_DATA_VALUE) == 0)
		ctk_combo_box_set_active (CTK_COMBO_BOX (widget), 0);
	else if (g_strcmp0 (stats_type, GPM_STATS_CHARGE_DATA_VALUE) == 0)
		ctk_combo_box_set_active (CTK_COMBO_BOX (widget), 1);
	else if (g_strcmp0 (stats_type, GPM_STATS_CHARGE_DATA_VALUE) == 0)
		ctk_combo_box_set_active (CTK_COMBO_BOX (widget), 2);
	else if (g_strcmp0 (stats_type, GPM_STATS_CHARGE_DATA_VALUE) == 0)
		ctk_combo_box_set_active (CTK_COMBO_BOX (widget), 0);
	else
		ctk_combo_box_set_active (CTK_COMBO_BOX (widget), 3);
	g_signal_connect (G_OBJECT (widget), "changed",
			  G_CALLBACK (cpm_stats_type_combo_changed_cb), NULL);

	widget = CTK_WIDGET (ctk_builder_get_object (builder, "combobox_history_time"));
	ctk_combo_box_text_append_text (CTK_COMBO_BOX_TEXT (widget), GPM_HISTORY_MINUTE_TEXT);
	ctk_combo_box_text_append_text (CTK_COMBO_BOX_TEXT (widget), GPM_HISTORY_HOUR_TEXT);
	ctk_combo_box_text_append_text (CTK_COMBO_BOX_TEXT (widget), GPM_HISTORY_HOURS_TEXT);
	ctk_combo_box_text_append_text (CTK_COMBO_BOX_TEXT (widget), GPM_HISTORY_DAY_TEXT);
	ctk_combo_box_text_append_text (CTK_COMBO_BOX_TEXT (widget), GPM_HISTORY_WEEK_TEXT);
	ctk_combo_box_set_active (CTK_COMBO_BOX (widget), 1);
	if (history_time == GPM_HISTORY_MINUTE_VALUE)
		ctk_combo_box_set_active (CTK_COMBO_BOX (widget), 0);
	else if (history_time == GPM_HISTORY_HOUR_VALUE)
		ctk_combo_box_set_active (CTK_COMBO_BOX (widget), 1);
	else
		ctk_combo_box_set_active (CTK_COMBO_BOX (widget), 2);
	g_signal_connect (G_OBJECT (widget), "changed",
			  G_CALLBACK (cpm_stats_range_combo_changed), NULL);

	client = up_client_new ();

	devices = up_client_get_devices2 (client);

	/* add devices in visually pleasing order */
	for (j=0; j<UP_DEVICE_KIND_LAST; j++) {
		for (i=0; i < devices->len; i++) {
			device = g_ptr_array_index (devices, i);
			g_object_get (device, "kind", &kind, NULL);
			if (kind == j)
				/* NULL == do not add it to ptr array */
				cpm_stats_add_device (device, NULL);
		}
	}

	/* connect now the coldplug is done */
	g_signal_connect (client, "device-added", G_CALLBACK (cpm_stats_device_added_cb), devices);
	g_signal_connect (client, "device-removed", G_CALLBACK (cpm_stats_device_removed_cb), devices);

	/* set current device */
	if (devices->len > 0) {
		device = g_ptr_array_index (devices, 0);
		cpm_stats_update_info_data (device);
		current_device = g_strdup (up_device_get_object_path (device));
	}

	if (last_device == NULL)
		last_device = g_settings_get_string (settings, GPM_SETTINGS_INFO_LAST_DEVICE);

	/* set the correct focus on the last device */
	if (last_device != NULL)
		cpm_stats_highlight_device (last_device);

	/* set axis */
	widget = CTK_WIDGET (ctk_builder_get_object (builder, "combobox_history_type"));
	cpm_stats_history_type_combo_changed_cb (widget, NULL);
	widget = CTK_WIDGET (ctk_builder_get_object (builder, "combobox_stats_type"));
	cpm_stats_type_combo_changed_cb (widget, NULL);

	widget = CTK_WIDGET (ctk_builder_get_object (builder, "dialog_stats"));

	status = g_application_run (G_APPLICATION (app), argc, argv);
	if (devices != NULL)
		g_ptr_array_unref (devices);

	g_object_unref (settings);
	g_object_unref (client);
	g_object_unref (builder);
	g_object_unref (list_store_info);
	g_object_unref (app);
	g_free (last_device);
	return status;
}
