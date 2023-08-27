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

#ifndef __CPM_GRAPH_WIDGET_H__
#define __CPM_GRAPH_WIDGET_H__

#include <ctk/ctk.h>
#include "cpm-point-obj.h"

G_BEGIN_DECLS

#define CPM_TYPE_GRAPH_WIDGET		(cpm_graph_widget_get_type ())
#define CPM_GRAPH_WIDGET(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), CPM_TYPE_GRAPH_WIDGET, CpmGraphWidget))
#define CPM_GRAPH_WIDGET_CLASS(obj)	(G_TYPE_CHECK_CLASS_CAST ((obj), CPM_GRAPH_WIDGET, CpmGraphWidgetClass))
#define CPM_IS_GRAPH_WIDGET(obj)	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), CPM_TYPE_GRAPH_WIDGET))
#define CPM_IS_GRAPH_WIDGET_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE ((obj), EFF_TYPE_GRAPH_WIDGET))
#define CPM_GRAPH_WIDGET_GET_CLASS	(G_TYPE_INSTANCE_GET_CLASS ((obj), CPM_TYPE_GRAPH_WIDGET, CpmGraphWidgetClass))

#define CPM_GRAPH_WIDGET_LEGEND_SPACING		17

typedef struct CpmGraphWidget		CpmGraphWidget;
typedef struct CpmGraphWidgetClass	CpmGraphWidgetClass;
typedef struct CpmGraphWidgetPrivate	CpmGraphWidgetPrivate;

typedef enum {
	CPM_GRAPH_WIDGET_TYPE_INVALID,
	CPM_GRAPH_WIDGET_TYPE_PERCENTAGE,
	CPM_GRAPH_WIDGET_TYPE_FACTOR,
	CPM_GRAPH_WIDGET_TYPE_TIME,
	CPM_GRAPH_WIDGET_TYPE_POWER,
	CPM_GRAPH_WIDGET_TYPE_VOLTAGE,
	CPM_GRAPH_WIDGET_TYPE_UNKNOWN
} CpmGraphWidgetType;

typedef enum {
	CPM_GRAPH_WIDGET_PLOT_LINE,
	CPM_GRAPH_WIDGET_PLOT_POINTS,
	CPM_GRAPH_WIDGET_PLOT_BOTH
} CpmGraphWidgetPlot;

/* the different kinds of lines in the key */
typedef struct {
	guint32			 color;
	gchar			*desc;
} CpmGraphWidgetKeyData;

struct CpmGraphWidget
{
	CtkDrawingArea		 parent;
	CpmGraphWidgetPrivate	*priv;
};

struct CpmGraphWidgetClass
{
	CtkDrawingAreaClass parent_class;
};

GType		 cpm_graph_widget_get_type		(void);
CtkWidget	*cpm_graph_widget_new			(void);

gboolean	 cpm_graph_widget_data_clear		(CpmGraphWidget		*graph);
gboolean	 cpm_graph_widget_data_assign		(CpmGraphWidget		*graph,
							 CpmGraphWidgetPlot	 plot,
							 GPtrArray		*array);
gboolean	 cpm_graph_widget_key_data_add		(CpmGraphWidget		*graph,
							 guint32		 color,
							 const gchar		*desc);

G_END_DECLS

#endif
