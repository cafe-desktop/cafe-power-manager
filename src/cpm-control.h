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

#ifndef __CPM_CONTROL_H
#define __CPM_CONTROL_H

#include <glib-object.h>

G_BEGIN_DECLS

#define CPM_TYPE_CONTROL		(cpm_control_get_type ())
#define CPM_CONTROL(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), CPM_TYPE_CONTROL, CpmControl))
#define CPM_CONTROL_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), CPM_TYPE_CONTROL, CpmControlClass))
#define CPM_IS_CONTROL(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), CPM_TYPE_CONTROL))
#define CPM_IS_CONTROL_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), CPM_TYPE_CONTROL))
#define CPM_CONTROL_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), CPM_TYPE_CONTROL, CpmControlClass))

typedef struct CpmControlPrivate CpmControlPrivate;

typedef struct
{
	GObject		      parent;
	CpmControlPrivate *priv;
} CpmControl;

typedef enum
{
	 CPM_CONTROL_ACTION_SUSPEND,
	 CPM_CONTROL_ACTION_HIBERNATE,
	 CPM_CONTROL_ACTION_LAST
} CpmControlAction;

typedef enum
{
	 CPM_CONTROL_ERROR_GENERAL,
	 CPM_CONTROL_ERROR_LAST
} CpmControlError;

typedef struct
{
	GObjectClass	parent_class;
	void		(* resume)			(CpmControl	*control,
							 CpmControlAction action);
	void		(* sleep)			(CpmControl	*control,
							 CpmControlAction action);
	void		(* sleep_failure)		(CpmControl	*control,
							 CpmControlAction action);
	void		(* request)			(CpmControl	*control,
							 const gchar	**type);
} CpmControlClass;

#define CPM_CONTROL_ERROR cpm_control_error_quark ()

GQuark		 cpm_control_error_quark		(void);
GType		 cpm_control_get_type			(void);
CpmControl	*cpm_control_new			(void);
gboolean	 cpm_control_suspend			(CpmControl	*control,
							 GError		**error);
gboolean	 cpm_control_hibernate			(CpmControl	*control,
							 GError		**error);
gboolean	 cpm_control_shutdown			(CpmControl	*control,
						 	 GError		**error);
gboolean	 cpm_control_get_lock_policy		(CpmControl	*control,
							 const gchar	*policy);

G_END_DECLS

#endif /* __CPM_CONTROL_H */
