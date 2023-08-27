/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

#ifndef __CPM_ENGINE_H
#define __CPM_ENGINE_H

#include <glib-object.h>
#include <libupower-glib/upower.h>

G_BEGIN_DECLS

#define CPM_TYPE_ENGINE		(cpm_engine_get_type ())
#define CPM_ENGINE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), CPM_TYPE_ENGINE, CpmEngine))
#define CPM_ENGINE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), CPM_TYPE_ENGINE, CpmEngineClass))
#define CPM_IS_ENGINE(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), CPM_TYPE_ENGINE))
#define CPM_IS_ENGINE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), CPM_TYPE_ENGINE))
#define CPM_ENGINE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), CPM_TYPE_ENGINE, CpmEngineClass))

typedef struct CpmEnginePrivate CpmEnginePrivate;

typedef struct
{
	GObject		 parent;
	CpmEnginePrivate *priv;
} CpmEngine;

typedef struct
{
	GObjectClass	parent_class;
	void		(* icon_changed)	(CpmEngine	*engine,
						 gchar		*icon);
	void		(* summary_changed)	(CpmEngine	*engine,
						 gchar		*status);
	void		(* low_capacity)	(CpmEngine	*engine,
						 UpDevice	*device);
	void		(* charge_low)		(CpmEngine	*engine,
						 UpDevice	*device);
	void		(* charge_critical)	(CpmEngine	*engine,
						 UpDevice	*device);
	void		(* charge_action)	(CpmEngine	*engine,
						 UpDevice	*device);
	void		(* fully_charged)	(CpmEngine	*engine,
						 UpDevice	*device);
	void		(* discharging)		(CpmEngine	*engine,
						 UpDevice	*device);
	void		(* devices_changed)	(CpmEngine	*engine);
} CpmEngineClass;

GType		 cpm_engine_get_type		(void);
CpmEngine	*cpm_engine_new			(void);
gchar		*cpm_engine_get_icon		(CpmEngine	*engine);
gchar		*cpm_engine_get_summary		(CpmEngine	*engine);
GPtrArray	*cpm_engine_get_devices		(CpmEngine	*engine);
UpDevice	*cpm_engine_get_primary_device	(CpmEngine	*engine);

G_END_DECLS

#endif	/* __CPM_ENGINE_H */

