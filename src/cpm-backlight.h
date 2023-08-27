/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2005-2007 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2004-2005 William Jon McCann <mccann@jhu.edu>
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

#ifndef __CPM_BACKLIGHT_H
#define __CPM_BACKLIGHT_H

#include <glib-object.h>

G_BEGIN_DECLS

#define CPM_TYPE_BACKLIGHT		(cpm_backlight_get_type ())
#define CPM_BACKLIGHT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), CPM_TYPE_BACKLIGHT, CpmBacklight))
#define CPM_BACKLIGHT_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), CPM_TYPE_BACKLIGHT, CpmBacklightClass))
#define CPM_IS_BACKLIGHT(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), CPM_TYPE_BACKLIGHT))
#define CPM_IS_BACKLIGHT_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), CPM_TYPE_BACKLIGHT))
#define CPM_BACKLIGHT_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), CPM_TYPE_BACKLIGHT, CpmBacklightClass))

typedef struct CpmBacklightPrivate CpmBacklightPrivate;

typedef struct
{
	GObject		         parent;
	CpmBacklightPrivate *priv;
} CpmBacklight;

typedef struct
{
	GObjectClass	parent_class;
	void		(* brightness_changed)		(CpmBacklight	*backlight,
							 gint		 brightness);
} CpmBacklightClass;

typedef enum
{
	 CPM_BACKLIGHT_ERROR_GENERAL,
	 CPM_BACKLIGHT_ERROR_DATA_NOT_AVAILABLE,
	 CPM_BACKLIGHT_ERROR_HARDWARE_NOT_PRESENT
} CpmBacklightError;

GType		 cpm_backlight_get_type			(void);
GQuark		 cpm_backlight_error_quark		(void);
CpmBacklight	*cpm_backlight_new			(void);

gboolean	 cpm_backlight_get_brightness		(CpmBacklight	*backlight,
							 guint		*brightness,
							 GError		**error);
gboolean	 cpm_backlight_set_brightness		(CpmBacklight	*backlight,
							 guint		 brightness,
							 GError		**error);

G_END_DECLS

#endif /* __CPM_BACKLIGHT_H */

