/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
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

#ifndef __CPMSCREENSAVER_H
#define __CPMSCREENSAVER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define CPM_TYPE_SCREENSAVER		(cpm_screensaver_get_type ())
#define CPM_SCREENSAVER(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), CPM_TYPE_SCREENSAVER, CpmScreensaver))
#define CPM_SCREENSAVER_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), CPM_TYPE_SCREENSAVER, CpmScreensaverClass))
#define CPM_IS_SCREENSAVER(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), CPM_TYPE_SCREENSAVER))
#define CPM_IS_SCREENSAVER_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), CPM_TYPE_SCREENSAVER))
#define CPM_SCREENSAVER_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), CPM_TYPE_SCREENSAVER, CpmScreensaverClass))

typedef struct CpmScreensaverPrivate CpmScreensaverPrivate;

typedef struct
{
	GObject		       parent;
	CpmScreensaverPrivate *priv;
} CpmScreensaver;

typedef struct
{
	GObjectClass	parent_class;
} CpmScreensaverClass;

GType		 cpm_screensaver_get_type		(void);
CpmScreensaver	*cpm_screensaver_new			(void);
void		 cpm_screensaver_test			(gpointer	 data);

gboolean	 cpm_screensaver_lock			(CpmScreensaver	*screensaver);
guint32 	 cpm_screensaver_add_throttle    	(CpmScreensaver	*screensaver,
							 const gchar	*reason);
gboolean 	 cpm_screensaver_remove_throttle    	(CpmScreensaver	*screensaver,
							 guint32         cookie);
gboolean	 cpm_screensaver_check_running		(CpmScreensaver	*screensaver);
gboolean	 cpm_screensaver_poke			(CpmScreensaver	*screensaver);

G_END_DECLS

#endif	/* __CPMSCREENSAVER_H */
