/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
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

#ifndef __CPMPREFS_H
#define __CPMPREFS_H

#include <glib-object.h>

G_BEGIN_DECLS

#define CPM_TYPE_PREFS		(cpm_prefs_get_type ())
#define CPM_PREFS(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), CPM_TYPE_PREFS, GpmPrefs))
#define CPM_PREFS_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), CPM_TYPE_PREFS, GpmPrefsClass))
#define CPM_IS_PREFS(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), CPM_TYPE_PREFS))
#define CPM_IS_PREFS_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), CPM_TYPE_PREFS))
#define CPM_PREFS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), CPM_TYPE_PREFS, GpmPrefsClass))

typedef struct GpmPrefsPrivate GpmPrefsPrivate;

typedef struct
{
	GObject		 parent;
	GpmPrefsPrivate *priv;
} GpmPrefs;

typedef struct
{
	GObjectClass	parent_class;
	void		(* action_help)			(GpmPrefs	*prefs);
	void		(* action_close)		(GpmPrefs	*prefs);
} GpmPrefsClass;

GType		 cpm_prefs_get_type			(void);
GpmPrefs	*cpm_prefs_new				(void);
CtkWidget	*cpm_window				(GpmPrefs	*prefs);
void		 cpm_prefs_activate_window		(CtkApplication *app, GpmPrefs	*prefs);

G_END_DECLS

#endif	/* __CPMPREFS_H */
