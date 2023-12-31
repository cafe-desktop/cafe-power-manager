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

#ifndef __CPM_MANAGER_H
#define __CPM_MANAGER_H

#include <glib-object.h>
#include <dbus/dbus-glib.h>

G_BEGIN_DECLS

#define CPM_TYPE_MANAGER	 (cpm_manager_get_type ())
#define CPM_MANAGER(o)		 (G_TYPE_CHECK_INSTANCE_CAST ((o), CPM_TYPE_MANAGER, CpmManager))
#define CPM_MANAGER_CLASS(k)	 (G_TYPE_CHECK_CLASS_CAST((k), CPM_TYPE_MANAGER, CpmManagerClass))
#define CPM_IS_MANAGER(o)	 (G_TYPE_CHECK_INSTANCE_TYPE ((o), CPM_TYPE_MANAGER))
#define CPM_IS_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CPM_TYPE_MANAGER))
#define CPM_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CPM_TYPE_MANAGER, CpmManagerClass))
#define CPM_MANAGER_ERROR	 (cpm_manager_error_quark ())
#define CPM_MANAGER_TYPE_ERROR	 (cpm_manager_error_get_type ()) 

typedef struct CpmManagerPrivate CpmManagerPrivate;

typedef struct
{
	 GObject		 parent;
	 CpmManagerPrivate	*priv;
} CpmManager;

typedef struct
{
	GObjectClass	parent_class;
} CpmManagerClass;

typedef enum
{
	CPM_MANAGER_ERROR_DENIED,
	CPM_MANAGER_ERROR_NO_HW,
	CPM_MANAGER_ERROR_LAST
} CpmManagerError;


GQuark		 cpm_manager_error_quark		(void);
GType		 cpm_manager_error_get_type		(void);
GType		 cpm_manager_get_type		  	(void);
CpmManager	*cpm_manager_new			(void);

gboolean	 cpm_manager_suspend			(CpmManager	*manager,
							 GError		**error);
gboolean	 cpm_manager_hibernate			(CpmManager	*manager,
							 GError		**error);
gboolean	 cpm_manager_can_suspend		(CpmManager	*manager,
							 gboolean	*can_suspend,
							 GError		**error);
gboolean	 cpm_manager_can_hibernate		(CpmManager	*manager,
							 gboolean	*can_hibernate,
							 GError		**error);

G_END_DECLS

#endif /* __CPM_MANAGER_H */
