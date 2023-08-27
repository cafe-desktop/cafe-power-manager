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

#ifndef __CPM_BRIGHTNESS_H
#define __CPM_BRIGHTNESS_H

#include <glib-object.h>

G_BEGIN_DECLS

#define CPM_TYPE_BRIGHTNESS		(cpm_brightness_get_type ())
#define CPM_BRIGHTNESS(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), CPM_TYPE_BRIGHTNESS, CpmBrightness))
#define CPM_BRIGHTNESS_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), CPM_TYPE_BRIGHTNESS, CpmBrightnessClass))
#define CPM_IS_BRIGHTNESS(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), CPM_TYPE_BRIGHTNESS))
#define CPM_IS_BRIGHTNESS_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), CPM_TYPE_BRIGHTNESS))
#define CPM_BRIGHTNESS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), CPM_TYPE_BRIGHTNESS, CpmBrightnessClass))

#define CPM_BRIGHTNESS_DIM_INTERVAL	5 /* ms */

typedef struct CpmBrightnessPrivate CpmBrightnessPrivate;

typedef struct
{
	GObject		         parent;
	CpmBrightnessPrivate	*priv;
} CpmBrightness;

typedef struct
{
	GObjectClass	parent_class;
	void		(* brightness_changed)	(CpmBrightness		*brightness,
						 guint			 percentage);
} CpmBrightnessClass;

GType		 cpm_brightness_get_type	(void);
CpmBrightness	*cpm_brightness_new		(void);

gboolean	 cpm_brightness_has_hw		(CpmBrightness		*brightness);
gboolean	 cpm_brightness_up		(CpmBrightness		*brightness,
						 gboolean		*hw_changed);
gboolean	 cpm_brightness_down		(CpmBrightness		*brightness,
						 gboolean		*hw_changed);
gboolean	 cpm_brightness_get		(CpmBrightness		*brightness,
						 guint			*percentage);
gboolean	 cpm_brightness_set		(CpmBrightness		*brightness,
						 guint			 percentage,
						 gboolean		*hw_changed);

G_END_DECLS

#endif /* __CPM_BRIGHTNESS_H */
