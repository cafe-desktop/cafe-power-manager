/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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

#ifndef __GPMPHONE_H
#define __GPMPHONE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GPM_TYPE_PHONE		(cpm_phone_get_type ())
#define GPM_PHONE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPM_TYPE_PHONE, GpmPhone))
#define GPM_PHONE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GPM_TYPE_PHONE, GpmPhoneClass))
#define GPM_IS_PHONE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPM_TYPE_PHONE))
#define GPM_IS_PHONE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPM_TYPE_PHONE))
#define GPM_PHONE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPM_TYPE_PHONE, GpmPhoneClass))

#define CAFE_PHONE_MANAGER_DBUS_SERVICE	"org.gnome.phone"
#define CAFE_PHONE_MANAGER_DBUS_PATH		"/org/gnome/phone/Manager"
#define CAFE_PHONE_MANAGER_DBUS_INTERFACE	"org.gnome.phone.Manager"

typedef struct GpmPhonePrivate GpmPhonePrivate;

typedef struct
{
	GObject		       parent;
	GpmPhonePrivate *priv;
} GpmPhone;

typedef struct
{
	GObjectClass	parent_class;
	void		(* device_added)		(GpmPhone	*phone,
							 guint		 idx);
	void		(* device_removed)		(GpmPhone	*phone,
							 guint		 idx);
	void		(* device_refresh)		(GpmPhone	*phone,
							 guint		 idx);
} GpmPhoneClass;

GType		 cpm_phone_get_type			(void);
GpmPhone	*cpm_phone_new				(void);

gboolean	 cpm_phone_get_present			(GpmPhone	*phone,
							 guint		 idx);
guint		 cpm_phone_get_percentage		(GpmPhone	*phone,
							 guint		 idx);
gboolean	 cpm_phone_get_on_ac			(GpmPhone	*phone,
							 guint		 idx);
guint		 cpm_phone_get_num_batteries		(GpmPhone	*phone);
gboolean	 cpm_phone_coldplug			(GpmPhone	*phone);
#ifdef EGG_TEST
void		 cpm_phone_test				(gpointer	 data);
#endif

G_END_DECLS

#endif	/* __GPMPHONE_H */
