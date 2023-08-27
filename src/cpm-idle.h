/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2005 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2005-2008 Richard Hughes <richard@hughsie.com>
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

#ifndef __CPM_IDLE_H
#define __CPM_IDLE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define CPM_TYPE_IDLE		(cpm_idle_get_type ())
#define CPM_IDLE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), CPM_TYPE_IDLE, CpmIdle))
#define CPM_IDLE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), CPM_TYPE_IDLE, CpmIdleClass))
#define CPM_IS_IDLE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), CPM_TYPE_IDLE))
#define CPM_IS_IDLE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), CPM_TYPE_IDLE))
#define CPM_IDLE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), CPM_TYPE_IDLE, CpmIdleClass))

typedef enum {
	CPM_IDLE_MODE_NORMAL,
	CPM_IDLE_MODE_DIM,
	CPM_IDLE_MODE_BLANK,
	CPM_IDLE_MODE_SLEEP
} CpmIdleMode;

typedef struct CpmIdlePrivate CpmIdlePrivate;

typedef struct
{
	GObject		parent;
	CpmIdlePrivate *priv;
} CpmIdle;

typedef struct
{
	GObjectClass	parent_class;
	void		(* idle_changed)		(CpmIdle	*idle,
							 CpmIdleMode	 mode);
} CpmIdleClass;

GType		 cpm_idle_get_type			(void);
CpmIdle		*cpm_idle_new				(void);
CpmIdleMode	 cpm_idle_get_mode			(CpmIdle	*idle);
void		 cpm_idle_set_check_cpu			(CpmIdle	*idle,
							 gboolean	 check_type_cpu);
gboolean	 cpm_idle_set_timeout_dim		(CpmIdle	*idle,
							 guint		 timeout);
gboolean	 cpm_idle_set_timeout_blank		(CpmIdle	*idle,
							 guint		 timeout);
gboolean	 cpm_idle_set_timeout_sleep		(CpmIdle	*idle,
							 guint		 timeout);
void		 cpm_idle_test				(gpointer	 data);

G_END_DECLS

#endif /* __CPM_IDLE_H */
