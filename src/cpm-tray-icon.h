/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2005 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2005-2007 Richard Hughes <richard@hughsie.com>
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

#ifndef __CPM_TRAY_ICON_H
#define __CPM_TRAY_ICON_H

#include <glib-object.h>

G_BEGIN_DECLS

#define CPM_TYPE_TRAY_ICON		(cpm_tray_icon_get_type ())
#define CPM_TRAY_ICON(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), CPM_TYPE_TRAY_ICON, CpmTrayIcon))
#define CPM_TRAY_ICON_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), CPM_TYPE_TRAY_ICON, CpmTrayIconClass))
#define CPM_IS_TRAY_ICON(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), CPM_TYPE_TRAY_ICON))
#define CPM_IS_TRAY_ICON_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), CPM_TYPE_TRAY_ICON))
#define CPM_TRAY_ICON_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), CPM_TYPE_TRAY_ICON, CpmTrayIconClass))

typedef struct CpmTrayIconPrivate CpmTrayIconPrivate;

typedef struct
{
	GObject		    parent;
	CpmTrayIconPrivate *priv;
} CpmTrayIcon;

typedef struct
{
	GObjectClass	parent_class;
	void	(* suspend)				(CpmTrayIcon	*tray_icon);
	void	(* hibernate)				(CpmTrayIcon	*tray_icon);
} CpmTrayIconClass;

GType		 cpm_tray_icon_get_type			(void);
CpmTrayIcon	*cpm_tray_icon_new			(void);

gboolean	 cpm_tray_icon_set_tooltip		(CpmTrayIcon	*icon,
							 const gchar	*tooltip);
gboolean	 cpm_tray_icon_set_icon			(CpmTrayIcon	*icon,
							 const gchar	*icon_name);
CtkStatusIcon	*cpm_tray_icon_get_status_icon		(CpmTrayIcon	*icon);

G_END_DECLS

#endif /* __CPM_TRAY_ICON_H */
