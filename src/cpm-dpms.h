/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2005 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2006-2009 Richard Hughes <richard@hughsie.com>
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

#ifndef __CPM_DPMS_H
#define __CPM_DPMS_H

G_BEGIN_DECLS

#define CPM_TYPE_DPMS		(cpm_dpms_get_type ())
#define CPM_DPMS(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), CPM_TYPE_DPMS, GpmDpms))
#define CPM_DPMS_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), CPM_TYPE_DPMS, GpmDpmsClass))
#define CPM_IS_DPMS(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), CPM_TYPE_DPMS))
#define CPM_IS_DPMS_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), CPM_TYPE_DPMS))
#define CPM_DPMS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), CPM_TYPE_DPMS, GpmDpmsClass))

typedef enum {
	CPM_DPMS_MODE_ON,
	CPM_DPMS_MODE_STANDBY,
	CPM_DPMS_MODE_SUSPEND,
	CPM_DPMS_MODE_OFF,
	CPM_DPMS_MODE_UNKNOWN
} GpmDpmsMode;

typedef struct GpmDpmsPrivate GpmDpmsPrivate;

typedef struct
{
	GObject	 	parent;
	GpmDpmsPrivate *priv;
} GpmDpms;

typedef struct
{
	GObjectClass	parent_class;
	void		(* mode_changed)	(GpmDpms 	*dpms,
						 GpmDpmsMode	 mode);
} GpmDpmsClass;

typedef enum
{
	CPM_DPMS_ERROR_GENERAL
} GpmDpmsError;

#define CPM_DPMS_ERROR cpm_dpms_error_quark ()

GQuark		 cpm_dpms_error_quark		(void);
GType		 cpm_dpms_get_type		(void);
GpmDpms		*cpm_dpms_new			(void);
gboolean	 cpm_dpms_get_mode	 	(GpmDpms	*dpms,
						 GpmDpmsMode	*mode,
						 GError		**error);
gboolean	 cpm_dpms_set_mode	 	(GpmDpms	*dpms,
						 GpmDpmsMode	 mode,
						 GError		**error);
void		 cpm_dpms_test			(gpointer	 data);

G_END_DECLS

#endif /* __CPM_DPMS_H */
