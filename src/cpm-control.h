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
#define CPM_CONTROL(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), CPM_TYPE_CONTROL, GpmControl))
#define CPM_CONTROL_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), CPM_TYPE_CONTROL, GpmControlClass))
#define CPM_IS_CONTROL(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), CPM_TYPE_CONTROL))
#define CPM_IS_CONTROL_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), CPM_TYPE_CONTROL))
#define CPM_CONTROL_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), CPM_TYPE_CONTROL, GpmControlClass))

typedef struct GpmControlPrivate GpmControlPrivate;

typedef struct
{
	GObject		      parent;
	GpmControlPrivate *priv;
} GpmControl;

typedef enum
{
	 CPM_CONTROL_ACTION_SUSPEND,
	 CPM_CONTROL_ACTION_HIBERNATE,
	 CPM_CONTROL_ACTION_LAST
} GpmControlAction;

typedef enum
{
	 CPM_CONTROL_ERROR_GENERAL,
	 CPM_CONTROL_ERROR_LAST
} GpmControlError;

typedef struct
{
	GObjectClass	parent_class;
	void		(* resume)			(GpmControl	*control,
							 GpmControlAction action);
	void		(* sleep)			(GpmControl	*control,
							 GpmControlAction action);
	void		(* sleep_failure)		(GpmControl	*control,
							 GpmControlAction action);
	void		(* request)			(GpmControl	*control,
							 const gchar	**type);
} GpmControlClass;

#define CPM_CONTROL_ERROR cpm_control_error_quark ()

GQuark		 cpm_control_error_quark		(void);
GType		 cpm_control_get_type			(void);
GpmControl	*cpm_control_new			(void);
gboolean	 cpm_control_suspend			(GpmControl	*control,
							 GError		**error);
gboolean	 cpm_control_hibernate			(GpmControl	*control,
							 GError		**error);
gboolean	 cpm_control_shutdown			(GpmControl	*control,
						 	 GError		**error);
gboolean	 cpm_control_get_lock_policy		(GpmControl	*control,
							 const gchar	*policy);

G_END_DECLS

#endif /* __CPM_CONTROL_H */
