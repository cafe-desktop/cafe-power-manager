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
#include <ctk/ctk.h>
#include <pango/pangocairo.h>
#include <glib/gi18n.h>
#include <stdlib.h>
#include <math.h>

#include "cpm-common.h"
#include "cpm-point-obj.h"
#include "cpm-graph-widget.h"

#include "egg-debug.h"
#include "egg-color.h"
#include "egg-precision.h"

#define CPM_GRAPH_WIDGET_FONT "Sans 8"

struct CpmGraphWidgetPrivate
{
	gboolean		 use_grid;
	gboolean		 use_legend;
	gboolean		 autorange_x;
	gboolean		 autorange_y;

	GSList			*key_data; /* lines */

	gint			 stop_x;
	gint			 stop_y;
	gint			 start_x;
	gint			 start_y;
	gint			 box_x; /* size of the white box, not the widget */
	gint			 box_y;
	gint			 box_width;
	gint			 box_height;

	gfloat			 unit_x; /* 10th width of graph */
	gfloat			 unit_y; /* 10th width of graph */

	CpmGraphWidgetType	 type_x;
	CpmGraphWidgetType	 type_y;
	gchar			*title;

	cairo_t			*cr;
	PangoLayout 		*layout;

	GPtrArray		*data_list;
	GPtrArray		*plot_list;
};

G_DEFINE_TYPE_WITH_PRIVATE (CpmGraphWidget, cpm_graph_widget, CTK_TYPE_DRAWING_AREA);

static gboolean cpm_graph_widget_draw (CtkWidget *graph, cairo_t *cr);
static void	cpm_graph_widget_finalize (GObject *object);

enum
{
	PROP_0,
	PROP_USE_LEGEND,
	PROP_USE_GRID,
	PROP_TYPE_X,
	PROP_TYPE_Y,
	PROP_AUTORANGE_X,
	PROP_AUTORANGE_Y,
	PROP_START_X,
	PROP_START_Y,
	PROP_STOP_X,
	PROP_STOP_Y,
};

/**
 * cpm_graph_widget_key_data_clear:
 **/
static gboolean
cpm_graph_widget_key_data_clear (CpmGraphWidget *graph)
{
	CpmGraphWidgetKeyData *keyitem;
	guint i;

	g_return_val_if_fail (CPM_IS_GRAPH_WIDGET (graph), FALSE);

	/* remove items in list and free */
	for (i=0; i<g_slist_length (graph->priv->key_data); i++) {
		keyitem = (CpmGraphWidgetKeyData *) g_slist_nth_data (graph->priv->key_data, i);
		g_free (keyitem->desc);
		g_free (keyitem);
	}
	g_slist_free (graph->priv->key_data);
	graph->priv->key_data = NULL;

	return TRUE;
}

/**
 * cpm_graph_widget_key_data_add:
 **/
gboolean
cpm_graph_widget_key_data_add (CpmGraphWidget *graph, guint32 color, const gchar *desc)
{
	CpmGraphWidgetKeyData *keyitem;

	g_return_val_if_fail (CPM_IS_GRAPH_WIDGET (graph), FALSE);

	egg_debug ("add to list %s", desc);
	keyitem = g_new0 (CpmGraphWidgetKeyData, 1);

	keyitem->color = color;
	keyitem->desc = g_strdup (desc);

	graph->priv->key_data = g_slist_append (graph->priv->key_data, (gpointer) keyitem);
	return TRUE;
}

/**
 * up_graph_get_property:
 **/
static void
up_graph_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	CpmGraphWidget *graph = CPM_GRAPH_WIDGET (object);
	switch (prop_id) {
	case PROP_USE_LEGEND:
		g_value_set_boolean (value, graph->priv->use_legend);
		break;
	case PROP_USE_GRID:
		g_value_set_boolean (value, graph->priv->use_grid);
		break;
	case PROP_TYPE_X:
		g_value_set_uint (value, graph->priv->type_x);
		break;
	case PROP_TYPE_Y:
		g_value_set_uint (value, graph->priv->type_y);
		break;
	case PROP_AUTORANGE_X:
		g_value_set_boolean (value, graph->priv->autorange_x);
		break;
	case PROP_AUTORANGE_Y:
		g_value_set_boolean (value, graph->priv->autorange_y);
		break;
	case PROP_START_X:
		g_value_set_int (value, graph->priv->start_x);
		break;
	case PROP_START_Y:
		g_value_set_int (value, graph->priv->start_y);
		break;
	case PROP_STOP_X:
		g_value_set_int (value, graph->priv->stop_x);
		break;
	case PROP_STOP_Y:
		g_value_set_int (value, graph->priv->stop_y);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * up_graph_set_property:
 **/
static void
up_graph_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	CpmGraphWidget *graph = CPM_GRAPH_WIDGET (object);

	switch (prop_id) {
	case PROP_USE_LEGEND:
		graph->priv->use_legend = g_value_get_boolean (value);
		break;
	case PROP_USE_GRID:
		graph->priv->use_grid = g_value_get_boolean (value);
		break;
	case PROP_TYPE_X:
		graph->priv->type_x = g_value_get_uint (value);
		break;
	case PROP_TYPE_Y:
		graph->priv->type_y = g_value_get_uint (value);
		break;
	case PROP_AUTORANGE_X:
		graph->priv->autorange_x = g_value_get_boolean (value);
		break;
	case PROP_AUTORANGE_Y:
		graph->priv->autorange_y = g_value_get_boolean (value);
		break;
	case PROP_START_X:
		graph->priv->start_x = g_value_get_int (value);
		break;
	case PROP_START_Y:
		graph->priv->start_y = g_value_get_int (value);
		break;
	case PROP_STOP_X:
		graph->priv->stop_x = g_value_get_int (value);
		break;
	case PROP_STOP_Y:
		graph->priv->stop_y = g_value_get_int (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}

	/* refresh widget */
	ctk_widget_hide (CTK_WIDGET (graph));
	ctk_widget_show (CTK_WIDGET (graph));
}

/**
 * cpm_graph_widget_class_init:
 * @class: This graph class instance
 **/
static void
cpm_graph_widget_class_init (CpmGraphWidgetClass *class)
{
	CtkWidgetClass *widget_class = CTK_WIDGET_CLASS (class);
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	widget_class->draw = cpm_graph_widget_draw;
	object_class->get_property = up_graph_get_property;
	object_class->set_property = up_graph_set_property;
	object_class->finalize = cpm_graph_widget_finalize;

	/* properties */
	g_object_class_install_property (object_class,
					 PROP_USE_LEGEND,
					 g_param_spec_boolean ("use-legend", NULL, NULL,
							       FALSE,
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_USE_GRID,
					 g_param_spec_boolean ("use-grid", NULL, NULL,
							       TRUE,
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_TYPE_X,
					 g_param_spec_uint ("type-x", NULL, NULL,
							    CPM_GRAPH_WIDGET_TYPE_INVALID,
							    CPM_GRAPH_WIDGET_TYPE_UNKNOWN,
							    CPM_GRAPH_WIDGET_TYPE_TIME,
							    G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_TYPE_Y,
					 g_param_spec_uint ("type-y", NULL, NULL,
							    CPM_GRAPH_WIDGET_TYPE_INVALID,
							    CPM_GRAPH_WIDGET_TYPE_UNKNOWN,
							    CPM_GRAPH_WIDGET_TYPE_PERCENTAGE,
							    G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_AUTORANGE_X,
					 g_param_spec_boolean ("autorange-x", NULL, NULL,
							       TRUE,
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_AUTORANGE_Y,
					 g_param_spec_boolean ("autorange-y", NULL, NULL,
							       TRUE,
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_START_X,
					 g_param_spec_int ("start-x", NULL, NULL,
							   G_MININT, G_MAXINT, 0,
							   G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_START_Y,
					 g_param_spec_int ("start-y", NULL, NULL,
							   G_MININT, G_MAXINT, 0,
							   G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_STOP_X,
					 g_param_spec_int ("stop-x", NULL, NULL,
							   G_MININT, G_MAXINT, 60,
							   G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_STOP_Y,
					 g_param_spec_int ("stop-y", NULL, NULL,
							   G_MININT, G_MAXINT, 100,
							   G_PARAM_READWRITE));
}

/**
 * cpm_graph_widget_init:
 * @graph: This class instance
 **/
static void
cpm_graph_widget_init (CpmGraphWidget *graph)
{
	PangoFontMap *fontmap;
	PangoContext *context;
	PangoFontDescription *desc;

	graph->priv = cpm_graph_widget_get_instance_private (graph);
	graph->priv->start_x = 0;
	graph->priv->start_y = 0;
	graph->priv->stop_x = 60;
	graph->priv->stop_y = 100;
	graph->priv->use_grid = TRUE;
	graph->priv->use_legend = FALSE;
	graph->priv->data_list = g_ptr_array_new_with_free_func ((GDestroyNotify) g_ptr_array_unref);
	graph->priv->plot_list = g_ptr_array_new ();
	graph->priv->key_data = NULL;
	graph->priv->type_x = CPM_GRAPH_WIDGET_TYPE_TIME;
	graph->priv->type_y = CPM_GRAPH_WIDGET_TYPE_PERCENTAGE;

	/* do pango stuff */
	fontmap = pango_cairo_font_map_get_default ();
	context = pango_font_map_create_context (PANGO_FONT_MAP (fontmap));
	pango_context_set_base_gravity (context, PANGO_GRAVITY_AUTO);

	graph->priv->layout = pango_layout_new (context);
	desc = pango_font_description_from_string (CPM_GRAPH_WIDGET_FONT);
	pango_layout_set_font_description (graph->priv->layout, desc);
	pango_font_description_free (desc);
}

/**
 * cpm_graph_widget_data_clear:
 **/
gboolean
cpm_graph_widget_data_clear (CpmGraphWidget *graph)
{
	g_return_val_if_fail (CPM_IS_GRAPH_WIDGET (graph), FALSE);

	g_ptr_array_set_size (graph->priv->data_list, 0);
	g_ptr_array_set_size (graph->priv->plot_list, 0);

	return TRUE;
}

/**
 * cpm_graph_widget_finalize:
 * @object: This graph class instance
 **/
static void
cpm_graph_widget_finalize (GObject *object)
{
	PangoContext *context;
	CpmGraphWidget *graph = (CpmGraphWidget*) object;

	/* clear key and data */
	cpm_graph_widget_key_data_clear (graph);
	cpm_graph_widget_data_clear (graph);

	/* free data */
	g_ptr_array_unref (graph->priv->data_list);
	g_ptr_array_unref (graph->priv->plot_list);

	context = pango_layout_get_context (graph->priv->layout);
	g_object_unref (graph->priv->layout);
	g_object_unref (context);
	G_OBJECT_CLASS (cpm_graph_widget_parent_class)->finalize (object);
}

/**
 * cpm_graph_widget_data_assign:
 * @graph: This class instance
 * @data: an array of CpmPointObj's
 *
 * Sets the data for the graph
 **/
gboolean
cpm_graph_widget_data_assign (CpmGraphWidget *graph, CpmGraphWidgetPlot plot, GPtrArray *data)
{
	GPtrArray *copy;
	CpmPointObj *obj;
	guint i;

	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (CPM_IS_GRAPH_WIDGET (graph), FALSE);

	/* make a deep copy */
	copy = g_ptr_array_new_with_free_func ((GDestroyNotify) cpm_point_obj_free);
	for (i=0; i<data->len; i++) {
		obj = cpm_point_obj_copy (g_ptr_array_index (data, i));
		g_ptr_array_add (copy, obj);
	}

	/* get the new data */
	g_ptr_array_add (graph->priv->data_list, copy);
	g_ptr_array_add (graph->priv->plot_list, GUINT_TO_POINTER(plot));

	/* refresh */
	ctk_widget_queue_draw (CTK_WIDGET (graph));

	return TRUE;
}

/**
 * cpm_get_axis_label:
 * @axis: The axis type, e.g. CPM_GRAPH_WIDGET_TYPE_TIME
 * @value: The data value, e.g. 120
 *
 * Unit is:
 * CPM_GRAPH_WIDGET_TYPE_TIME:		seconds
 * CPM_GRAPH_WIDGET_TYPE_POWER: 	Wh (not Ah)
 * CPM_GRAPH_WIDGET_TYPE_PERCENTAGE:	%
 *
 * Return value: a string value depending on the axis type and the value.
 **/
static gchar *
cpm_get_axis_label (CpmGraphWidgetType axis, gfloat value)
{
	gchar *text = NULL;
	if (axis == CPM_GRAPH_WIDGET_TYPE_TIME) {
		gint time_s = abs((gint) value);
		gint minutes = time_s / 60;
		gint seconds = time_s - (minutes * 60);
		gint hours = minutes / 60;
		gint days = hours / 24;
		minutes = minutes - (hours * 60);
		hours = hours - (days * 24);
		if (days > 0) {
			if (hours == 0) {
				/*Translators: This is %i days*/
				text = g_strdup_printf (_("%id"), days);
			} else {
				/*Translators: This is %i days %02i hours*/
				text = g_strdup_printf (_("%id%02ih"), days, hours);
			}
		} else if (hours > 0) {
			if (minutes == 0) {
				/*Translators: This is %i hours*/
				text = g_strdup_printf (_("%ih"), hours);
			} else {
				/*Translators: This is %i hours %02i minutes*/
				text = g_strdup_printf (_("%ih%02im"), hours, minutes);
			}
		} else if (minutes > 0) {
			if (seconds == 0) {
				/*Translators: This is %2i minutes*/
				text = g_strdup_printf (_("%2im"), minutes);
			} else {
				/*Translators: This is %2i minutes %02i seconds*/
				text = g_strdup_printf (_("%2im%02i"), minutes, seconds);
			}
		} else {
			/*Translators: This is %2i seconds*/
			text = g_strdup_printf (_("%2is"), seconds);
		}
	} else if (axis == CPM_GRAPH_WIDGET_TYPE_PERCENTAGE) {
		/*Translators: This is %i Percentage*/
		text = g_strdup_printf (_("%i%%"), (gint) value);
	} else if (axis == CPM_GRAPH_WIDGET_TYPE_POWER) {
		/*Translators: This is %.1f Watts*/
		text = g_strdup_printf (_("%.1fW"), value);
	} else if (axis == CPM_GRAPH_WIDGET_TYPE_FACTOR) {
		text = g_strdup_printf ("%.1f", value);
	} else if (axis == CPM_GRAPH_WIDGET_TYPE_VOLTAGE) {
		/*Translators: This is %.1f Volts*/
		text = g_strdup_printf (_("%.1fV"), value);
	} else {
		text = g_strdup_printf ("%i", (gint) value);
	}
	return text;
}

/**
 * cpm_graph_widget_draw_grid:
 * @graph: This class instance
 * @cr: Cairo drawing context
 *
 * Draw the 10x10 dotted grid onto the graph.
 **/
static void
cpm_graph_widget_draw_grid (CpmGraphWidget *graph, cairo_t *cr)
{
	guint i;
	gfloat b;
	gdouble dotted[] = {1., 2.};
	gfloat divwidth  = (gfloat)graph->priv->box_width / 10.0f;
	gfloat divheight = (gfloat)graph->priv->box_height / 10.0f;

	cairo_save (cr);

	cairo_set_line_width (cr, 1);
	cairo_set_dash (cr, dotted, 2, 0.0);

	/* do vertical lines */
	cairo_set_source_rgb (cr, 0.1, 0.1, 0.1);
	for (i=1; i<10; i++) {
		b = graph->priv->box_x + ((gfloat) i * divwidth);
		cairo_move_to (cr, (gint)b + 0.5f, graph->priv->box_y);
		cairo_line_to (cr, (gint)b + 0.5f, graph->priv->box_y + graph->priv->box_height);
		cairo_stroke (cr);
	}

	/* do horizontal lines */
	for (i=1; i<10; i++) {
		b = graph->priv->box_y + ((gfloat) i * divheight);
		cairo_move_to (cr, graph->priv->box_x, (gint)b + 0.5f);
		cairo_line_to (cr, graph->priv->box_x + graph->priv->box_width, (int)b + 0.5f);
		cairo_stroke (cr);
	}

	cairo_restore (cr);
}

/**
 * cpm_graph_widget_draw_labels:
 * @graph: This class instance
 * @cr: Cairo drawing context
 *
 * Draw the X and the Y labels onto the graph.
 **/
static void
cpm_graph_widget_draw_labels (CpmGraphWidget *graph, cairo_t *cr)
{
	guint i;
	gfloat b;
	gchar *text;
	gfloat value;
	gfloat divwidth  = (gfloat)graph->priv->box_width / 10.0f;
	gfloat divheight = (gfloat)graph->priv->box_height / 10.0f;
	gint length_x = graph->priv->stop_x - graph->priv->start_x;
	gint length_y = graph->priv->stop_y - graph->priv->start_y;
	PangoRectangle ink_rect, logical_rect;
	gfloat offsetx = 0;
	gfloat offsety = 0;

	cairo_save (cr);

	/* do x text */
	cairo_set_source_rgb (cr, 0, 0, 0);
	for (i=0; i<11; i++) {
		b = graph->priv->box_x + ((gfloat) i * divwidth);
		value = ((length_x / 10.0f) * (gfloat) i) + (gfloat) graph->priv->start_x;
		text = cpm_get_axis_label (graph->priv->type_x, value);

		pango_layout_set_text (graph->priv->layout, text, -1);
		pango_layout_get_pixel_extents (graph->priv->layout, &ink_rect, &logical_rect);
		/* have data points 0 and 10 bounded, but 1..9 centered */
		if (i == 0)
			offsetx = 2.0;
		else if (i == 10)
			offsetx = ink_rect.width;
		else
			offsetx = (ink_rect.width / 2.0f);

		cairo_move_to (cr, b - offsetx,
			       graph->priv->box_y + graph->priv->box_height + 2.0);

		pango_cairo_show_layout (cr, graph->priv->layout);
		g_free (text);
	}

	/* do y text */
	for (i=0; i<11; i++) {
		b = graph->priv->box_y + ((gfloat) i * divheight);
		value = ((gfloat) length_y / 10.0f) * (10 - (gfloat) i) + graph->priv->start_y;
		text = cpm_get_axis_label (graph->priv->type_y, value);

		pango_layout_set_text (graph->priv->layout, text, -1);
		pango_layout_get_pixel_extents (graph->priv->layout, &ink_rect, &logical_rect);

		/* have data points 0 and 10 bounded, but 1..9 centered */
		if (i == 10)
			offsety = 0;
		else if (i == 0)
			offsety = ink_rect.height;
		else
			offsety = (ink_rect.height / 2.0f);
		offsetx = ink_rect.width + 7;
		offsety -= 10;
		cairo_move_to (cr, graph->priv->box_x - offsetx - 2, b + offsety);
		pango_cairo_show_layout (cr, graph->priv->layout);
		g_free (text);
	}

	cairo_restore (cr);
}

/**
 * cpm_graph_widget_get_y_label_max_width:
 * @graph: This class instance
 * @cr: Cairo drawing context
 *
 * Draw the X and the Y labels onto the graph.
 **/
static guint
cpm_graph_widget_get_y_label_max_width (CpmGraphWidget *graph,
					cairo_t        *cr G_GNUC_UNUSED)
{
	guint i;
	gchar *text;
	gint value;
	gint length_y = graph->priv->stop_y - graph->priv->start_y;
	PangoRectangle ink_rect, logical_rect;
	guint biggest = 0;

	/* do y text */
	for (i=0; i<11; i++) {
		value = (length_y / 10) * (10 - (gfloat) i) + graph->priv->start_y;
		text = cpm_get_axis_label (graph->priv->type_y, value);
		pango_layout_set_text (graph->priv->layout, text, -1);
		pango_layout_get_pixel_extents (graph->priv->layout, &ink_rect, &logical_rect);
		if (ink_rect.width > (gint) biggest)
			biggest = ink_rect.width;
		g_free (text);
	}
	return biggest;
}

/**
 * cpm_graph_widget_autorange_x:
 * @graph: This class instance
 *
 * Autoranges the graph axis depending on the axis type, and the maximum
 * value of the data. We have to be careful to choose a number that gives good
 * resolution but also a number that scales "well" to a 10x10 grid.
 **/
static void
cpm_graph_widget_autorange_x (CpmGraphWidget *graph)
{
	gfloat biggest_x = G_MINFLOAT;
	gfloat smallest_x = G_MAXFLOAT;
	guint rounding_x = 1;
	GPtrArray *data;
	CpmPointObj *point;
	guint i, j;
	guint len = 0;
	GPtrArray *array;

	array = graph->priv->data_list;

	/* find out if we have no data */
	for (j=0; j<array->len; j++) {
		data = g_ptr_array_index (array, j);
		len = data->len;
		if (len > 0)
			break;
	}

	/* no data in any array */
	if (len == 0) {
		egg_debug ("no data");
		graph->priv->start_x = 0;
		graph->priv->stop_x = 10;
		return;
	}

	/* get the range for the graph */
	for (j=0; j<array->len; j++) {
		data = g_ptr_array_index (array, j);
		for (i=0; i < data->len; i++) {
			point = (CpmPointObj *) g_ptr_array_index (data, i);
			if (point->x > biggest_x)
				biggest_x = point->x;
			if (point->x < smallest_x)
				smallest_x = point->x;
		}
	}
	egg_debug ("Data range is %f<x<%f", smallest_x, biggest_x);
	/* don't allow no difference */
	if (biggest_x - smallest_x < 0.0001) {
		biggest_x++;
		smallest_x--;
	}

	if (graph->priv->type_x == CPM_GRAPH_WIDGET_TYPE_PERCENTAGE) {
		rounding_x = 10;
	} else if (graph->priv->type_x == CPM_GRAPH_WIDGET_TYPE_FACTOR) {
		rounding_x = 1;
	} else if (graph->priv->type_x == CPM_GRAPH_WIDGET_TYPE_POWER) {
		rounding_x = 10;
	} else if (graph->priv->type_x == CPM_GRAPH_WIDGET_TYPE_VOLTAGE) {
		rounding_x = 1000;
	} else if (graph->priv->type_x == CPM_GRAPH_WIDGET_TYPE_TIME) {
		if (biggest_x-smallest_x < 150)
			rounding_x = 150;
		else if (biggest_x-smallest_x < 5*60)
			rounding_x = 5 * 60;
		else
			rounding_x = 10 * 60;
	}

	graph->priv->start_x = egg_precision_round_down (smallest_x, rounding_x);
	graph->priv->stop_x = egg_precision_round_up (biggest_x, rounding_x);

	egg_debug ("Processed(1) range is %i<x<%i",
		   graph->priv->start_x, graph->priv->stop_x);

	/* if percentage, and close to the end points, then extend */
	if (graph->priv->type_x == CPM_GRAPH_WIDGET_TYPE_PERCENTAGE) {
		if (graph->priv->stop_x >= 90)
			graph->priv->stop_x = 100;
		if (graph->priv->start_x > 0 && graph->priv->start_x <= 10)
			graph->priv->start_x = 0;
	} else if (graph->priv->type_x == CPM_GRAPH_WIDGET_TYPE_TIME) {
		if (graph->priv->start_x > 0 && graph->priv->start_x <= 60*10)
			graph->priv->start_x = 0;
	}

	egg_debug ("Processed range is %i<x<%i",
		   graph->priv->start_x, graph->priv->stop_x);
}

/**
 * cpm_graph_widget_autorange_y:
 * @graph: This class instance
 *
 * Autoranges the graph axis depending on the axis type, and the maximum
 * value of the data. We have to be careful to choose a number that gives good
 * resolution but also a number that scales "well" to a 10x10 grid.
 **/
static void
cpm_graph_widget_autorange_y (CpmGraphWidget *graph)
{
	gfloat biggest_y = G_MINFLOAT;
	gfloat smallest_y = G_MAXFLOAT;
	guint rounding_y = 1;
	GPtrArray *data;
	CpmPointObj *point;
	guint i, j;
	guint len = 0;
	GPtrArray *array;

	array = graph->priv->data_list;

	/* find out if we have no data */
	for (j=0; j<array->len; j++) {
		data = g_ptr_array_index (array, j);
		len = data->len;
		if (len > 0)
			break;
	}

	/* no data in any array */
	if (len == 0) {
		egg_debug ("no data");
		graph->priv->start_y = 0;
		graph->priv->stop_y = 10;
		return;
	}

	/* get the range for the graph */
	for (j=0; j<array->len; j++) {
		data = g_ptr_array_index (array, j);
		for (i=0; i < data->len; i++) {
			point = (CpmPointObj *) g_ptr_array_index (data, i);
			if (point->y > biggest_y)
				biggest_y = point->y;
			if (point->y < smallest_y)
				smallest_y = point->y;
		}
	}
	egg_debug ("Data range is %f<y<%f", smallest_y, biggest_y);
	/* don't allow no difference */
	if (biggest_y - smallest_y < 0.0001) {
		biggest_y++;
		smallest_y--;
	}

	if (graph->priv->type_y == CPM_GRAPH_WIDGET_TYPE_PERCENTAGE) {
		rounding_y = 10;
	} else if (graph->priv->type_y == CPM_GRAPH_WIDGET_TYPE_FACTOR) {
		rounding_y = 1;
	} else if (graph->priv->type_y == CPM_GRAPH_WIDGET_TYPE_POWER) {
		rounding_y = 10;
	} else if (graph->priv->type_y == CPM_GRAPH_WIDGET_TYPE_VOLTAGE) {
		rounding_y = 1000;
	} else if (graph->priv->type_y == CPM_GRAPH_WIDGET_TYPE_TIME) {
		if (biggest_y-smallest_y < 150)
			rounding_y = 150;
		else if (biggest_y < 5*60)
			rounding_y = 5 * 60;
		else
			rounding_y = 10 * 60;
	}

	graph->priv->start_y = egg_precision_round_down (smallest_y, rounding_y);
	graph->priv->stop_y = egg_precision_round_up (biggest_y, rounding_y);

	/* a factor graph always is centered around zero */
	if (graph->priv->type_y == CPM_GRAPH_WIDGET_TYPE_FACTOR) {
		if (abs (graph->priv->stop_y) > abs (graph->priv->start_y))
			graph->priv->start_y = -graph->priv->stop_y;
		else
			graph->priv->stop_y = -graph->priv->start_y;
	}

	egg_debug ("Processed(1) range is %i<y<%i",
		   graph->priv->start_y, graph->priv->stop_y);

	if (graph->priv->type_y == CPM_GRAPH_WIDGET_TYPE_PERCENTAGE) {
		if (graph->priv->stop_y >= 90)
			graph->priv->stop_y = 100;
		if (graph->priv->start_y > 0 && graph->priv->start_y <= 10)
			graph->priv->start_y = 0;
	} else if (graph->priv->type_y == CPM_GRAPH_WIDGET_TYPE_TIME) {
		if (graph->priv->start_y <= 60*10)
			graph->priv->start_y = 0;
	}

	egg_debug ("Processed range is %i<y<%i",
		   graph->priv->start_y, graph->priv->stop_y);
}

/**
 * cpm_graph_widget_set_color:
 * @cr: Cairo drawing context
 * @color: The color enum
 **/
static void
cpm_graph_widget_set_color (cairo_t *cr, guint32 color)
{
	guint8 r, g, b;
	egg_color_to_rgb (color, &r, &g, &b);
	cairo_set_source_rgb (cr, ((gdouble) r)/256.0f, ((gdouble) g)/256.0f, ((gdouble) b)/256.0f);
}

/**
 * cpm_graph_widget_draw_legend_line:
 * @cr: Cairo drawing context
 * @x: The X-coordinate for the center
 * @y: The Y-coordinate for the center
 * @color: The color enum
 *
 * Draw the legend line on the graph of a specified color
 **/
static void
cpm_graph_widget_draw_legend_line (cairo_t *cr, gfloat x, gfloat y, guint32 color)
{
	gfloat width = 10;
	gfloat height = 2;
	/* background */
	cairo_rectangle (cr, (int) (x - (width/2)) + 0.5, (int) (y - (height/2)) + 0.5, width, height);
	cpm_graph_widget_set_color (cr, color);
	cairo_fill (cr);
	/* solid outline box */
	cairo_rectangle (cr, (int) (x - (width/2)) + 0.5, (int) (y - (height/2)) + 0.5, width, height);
	cairo_set_source_rgb (cr, 0.1, 0.1, 0.1);
	cairo_set_line_width (cr, 1);
	cairo_stroke (cr);
}

/**
 * cpm_graph_widget_get_pos_on_graph:
 * @graph: This class instance
 * @data_x: The data X-coordinate
 * @data_y: The data Y-coordinate
 * @x: The returned X position on the cairo surface
 * @y: The returned Y position on the cairo surface
 **/
static void
cpm_graph_widget_get_pos_on_graph (CpmGraphWidget *graph, gfloat data_x, gfloat data_y, float *x, float *y)
{
	*x = graph->priv->box_x + (graph->priv->unit_x * (data_x - graph->priv->start_x)) + 1;
	*y = graph->priv->box_y + (graph->priv->unit_y * (gfloat)(graph->priv->stop_y - data_y)) + 1.5;
}

/**
 * cpm_graph_widget_draw_dot:
 **/
static void
cpm_graph_widget_draw_dot (cairo_t *cr, gfloat x, gfloat y, guint32 color)
{
	gfloat width;
	/* box */
	width = 2.0;
	cairo_rectangle (cr, (gint)x + 0.5f - (width/2), (gint)y + 0.5f - (width/2), width, width);
	cpm_graph_widget_set_color (cr, color);
	cairo_fill (cr);
	cairo_rectangle (cr, (gint)x + 0.5f - (width/2), (gint)y + 0.5f - (width/2), width, width);
	cairo_set_source_rgb (cr, 0, 0, 0);
	cairo_set_line_width (cr, 1);
	cairo_stroke (cr);
}

/**
 * cpm_graph_widget_draw_line:
 * @graph: This class instance
 * @cr: Cairo drawing context
 *
 * Draw the data line onto the graph with a big green line. We should already
 * limit the data to < ~100 values, so this shouldn't take too long.
 **/
static void
cpm_graph_widget_draw_line (CpmGraphWidget *graph, cairo_t *cr)
{
	gfloat oldx, oldy;
	gfloat newx, newy;
	GPtrArray *data;
	GPtrArray *array;
	CpmGraphWidgetPlot plot;
	CpmPointObj *point;
	guint i, j;

	if (graph->priv->data_list->len == 0) {
		egg_debug ("no data");
		return;
	}
	cairo_save (cr);

	array = graph->priv->data_list;

	/* do each line */
	for (j=0; j<array->len; j++) {
		data = g_ptr_array_index (array, j);
		if (data->len == 0)
			continue;
		plot = GPOINTER_TO_UINT (g_ptr_array_index (graph->priv->plot_list, j));

		/* get the very first point so we can work out the old */
		point = (CpmPointObj *) g_ptr_array_index (data, 0);
		oldx = 0;
		oldy = 0;
		cpm_graph_widget_get_pos_on_graph (graph, point->x, point->y, &oldx, &oldy);
		if (plot == CPM_GRAPH_WIDGET_PLOT_POINTS || plot == CPM_GRAPH_WIDGET_PLOT_BOTH)
			cpm_graph_widget_draw_dot (cr, oldx, oldy, point->color);

		for (i=1; i < data->len; i++) {
			point = (CpmPointObj *) g_ptr_array_index (data, i);

			cpm_graph_widget_get_pos_on_graph (graph, point->x, point->y, &newx, &newy);

			/* ignore white lines */
			if (point->color == 0xffffff) {
				oldx = newx;
				oldy = newy;
				continue;
			}

			/* draw line */
			if (plot == CPM_GRAPH_WIDGET_PLOT_LINE || plot == CPM_GRAPH_WIDGET_PLOT_BOTH) {
				cairo_move_to (cr, oldx, oldy);
				cairo_line_to (cr, newx, newy);
				cairo_set_line_width (cr, 1.5);
				cpm_graph_widget_set_color (cr, point->color);
				cairo_stroke (cr);
			}

			/* draw data dot */
			if (plot == CPM_GRAPH_WIDGET_PLOT_POINTS || plot == CPM_GRAPH_WIDGET_PLOT_BOTH)
				cpm_graph_widget_draw_dot (cr, newx, newy, point->color);

			/* save old */
			oldx = newx;
			oldy = newy;
		}
	}

	cairo_restore (cr);
}

/**
 * cpm_graph_widget_draw_bounding_box:
 * @cr: Cairo drawing context
 * @x: The X-coordinate for the top-left
 * @y: The Y-coordinate for the top-left
 * @width: The item width
 * @height: The item height
 **/
static void
cpm_graph_widget_draw_bounding_box (cairo_t *cr, gint x, gint y, gint width, gint height)
{
	/* background */
	cairo_rectangle (cr, x, y, width, height);
	cairo_set_source_rgb (cr, 1, 1, 1);
	cairo_fill (cr);
	/* solid outline box */
	cairo_rectangle (cr, x + 0.5f, y + 0.5f, width - 1, height - 1);
	cairo_set_source_rgb (cr, 0.1, 0.1, 0.1);
	cairo_set_line_width (cr, 1);
	cairo_stroke (cr);
}

/**
 * cpm_graph_widget_draw_legend:
 * @cr: Cairo drawing context
 * @x: The X-coordinate for the top-left
 * @y: The Y-coordinate for the top-left
 * @width: The item width
 * @height: The item height
 **/
static void
cpm_graph_widget_draw_legend (CpmGraphWidget *graph, gint x, gint y, gint width, gint height)
{
	cairo_t *cr = graph->priv->cr;
	gint y_count;
	guint i;
	CpmGraphWidgetKeyData *keydataitem;

	cpm_graph_widget_draw_bounding_box (cr, x, y, width, height);
	y_count = y + 10;

	/* add the line colors to the legend */
	for (i=0; i<g_slist_length (graph->priv->key_data); i++) {
		keydataitem = (CpmGraphWidgetKeyData *) g_slist_nth_data (graph->priv->key_data, i);
		if (keydataitem == NULL) {
			/* this shouldn't ever happen */
			egg_warning ("keydataitem NULL!");
			break;
		}
		cpm_graph_widget_draw_legend_line (cr, x + 8, y_count, keydataitem->color);
		cairo_move_to (cr, x + 8 + 10, y_count - 6);
		cairo_set_source_rgb (cr, 0, 0, 0);
		pango_layout_set_text (graph->priv->layout, keydataitem->desc, -1);
		pango_cairo_show_layout (cr, graph->priv->layout);
		y_count = y_count + CPM_GRAPH_WIDGET_LEGEND_SPACING;
	}
}

/**
 * cpm_graph_widget_legend_calculate_width:
 * @graph: This class instance
 * @cr: Cairo drawing context
 * Return value: The width of the legend, including borders.
 *
 * We have to find the maximum size of the text so we know the width of the
 * legend box. We can't hardcode this as the dpi or font size might differ
 * from machine to machine.
 **/
static gboolean
cpm_graph_widget_legend_calculate_size (CpmGraphWidget *graph,
					cairo_t        *cr G_GNUC_UNUSED,
					guint          *width,
					guint          *height)
{
	guint i;
	PangoRectangle ink_rect, logical_rect;
	CpmGraphWidgetKeyData *keydataitem;

	g_return_val_if_fail (CPM_IS_GRAPH_WIDGET (graph), FALSE);

	/* set defaults */
	*width = 0;
	*height = 0;

	/* add the line colors to the legend */
	for (i=0; i<g_slist_length (graph->priv->key_data); i++) {
		keydataitem = (CpmGraphWidgetKeyData *) g_slist_nth_data (graph->priv->key_data, i);
		*height = *height + CPM_GRAPH_WIDGET_LEGEND_SPACING;
		pango_layout_set_text (graph->priv->layout, keydataitem->desc, -1);
		pango_layout_get_pixel_extents (graph->priv->layout, &ink_rect, &logical_rect);
		if ((gint) *width < ink_rect.width)
			*width = ink_rect.width;
	}

	/* have we got no entries? */
	if (*width == 0 && *height == 0)
		return TRUE;

	/* add for borders */
	*width += 25;
	*height += 3;

	return TRUE;
}

/**
 * cpm_graph_widget_draw:
 * @graph: This class instance
 * @event: The expose event
 *
 * Just repaint the entire graph widget on expose.
 **/
static gboolean
cpm_graph_widget_draw (CtkWidget *widget, cairo_t *cr)
{
	CtkAllocation allocation;
	gint legend_x = 0;
	gint legend_y = 0;
	guint legend_height = 0;
	guint legend_width = 0;
	gfloat data_x;
	gfloat data_y;

	CpmGraphWidget *graph = (CpmGraphWidget*) widget;
	g_return_val_if_fail (graph != NULL, FALSE);
	g_return_val_if_fail (CPM_IS_GRAPH_WIDGET (graph), FALSE);

	cpm_graph_widget_legend_calculate_size (graph, cr, &legend_width, &legend_height);
	cairo_save (cr);

	/* we need this so we know the y text */
	if (graph->priv->autorange_x)
		cpm_graph_widget_autorange_x (graph);
	if (graph->priv->autorange_y)
		cpm_graph_widget_autorange_y (graph);

	graph->priv->box_x = cpm_graph_widget_get_y_label_max_width (graph, cr) + 10;
	graph->priv->box_y = 5;

	ctk_widget_get_allocation (widget, &allocation);
	graph->priv->box_height = allocation.height - (20 + graph->priv->box_y);

	/* make size adjustment for legend */
	if (graph->priv->use_legend && legend_height > 0) {
		graph->priv->box_width = allocation.width -
					 (3 + legend_width + 5 + graph->priv->box_x);
		legend_x = graph->priv->box_x + graph->priv->box_width + 6;
		legend_y = graph->priv->box_y;
	} else {
		graph->priv->box_width = allocation.width -
					 (3 + graph->priv->box_x);
	}

	/* graph background */
	cpm_graph_widget_draw_bounding_box (cr, graph->priv->box_x, graph->priv->box_y,
				     graph->priv->box_width, graph->priv->box_height);
	if (graph->priv->use_grid)
		cpm_graph_widget_draw_grid (graph, cr);

	/* -3 is so we can keep the lines inside the box at both extremes */
	data_x = graph->priv->stop_x - graph->priv->start_x;
	data_y = graph->priv->stop_y - graph->priv->start_y;
	graph->priv->unit_x = (float)(graph->priv->box_width - 3) / (float) data_x;
	graph->priv->unit_y = (float)(graph->priv->box_height - 3) / (float) data_y;

	cpm_graph_widget_draw_labels (graph, cr);
	cpm_graph_widget_draw_line (graph, cr);

	if (graph->priv->use_legend && legend_height > 0)
		cpm_graph_widget_draw_legend (graph, legend_x, legend_y, legend_width, legend_height);

	cairo_restore (cr);
	return FALSE;
}

/**
 * cpm_graph_widget_new:
 * Return value: A new CpmGraphWidget object.
 **/
CtkWidget *
cpm_graph_widget_new (void)
{
	return g_object_new (CPM_TYPE_GRAPH_WIDGET, NULL);
}

